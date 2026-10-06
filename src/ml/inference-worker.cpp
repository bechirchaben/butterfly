/*
Butterfly - filtres visuels temps réel pour OBS Studio
Copyright (C) 2026 Bechir Chaabane

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "ml/inference-worker.h"
#include "ml/segmenter.h"

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <algorithm>
#include <cstring>
#include <memory>

// Toutes les N analyses, on écrit le temps moyen dans le journal d'OBS.
static const int STATS_EVERY = 300;

InferenceWorker::InferenceWorker(const std::string &model_path_, bool use_gpu_)
	: model_path(model_path_),
	  use_gpu(use_gpu_)
{
	// Démarre le thread : il exécute thread_main() en parallèle du reste d'OBS.
	thread = std::thread(&InferenceWorker::thread_main, this);
}

InferenceWorker::~InferenceWorker()
{
	{
		// Les accolades limitent la durée du verrou : il est relâché à la fin du bloc.
		std::lock_guard<std::mutex> lock(mutex);
		stop_requested = true;
	}
	wake_up.notify_one();

	// Attend que le thread de l'IA ait terminé (au plus la fin de l'analyse en cours).
	thread.join();
}

bool InferenceWorker::wants_frame()
{
	if (input_width == 0) {
		return false; // modèle pas encore chargé
	}
	std::lock_guard<std::mutex> lock(mutex);
	return !has_frame;
}

void InferenceWorker::submit_frame(const uint8_t *rgba, uint32_t linesize, int width, int height)
{
	if (width != input_width || height != input_height) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mutex);

		// Copie ligne par ligne (la texture peut avoir des octets de marge en fin de ligne).
		size_t row_bytes = (size_t)width * 4;
		frame.resize(row_bytes * (size_t)height);
		for (int y = 0; y < height; y++) {
			memcpy(frame.data() + y * row_bytes, rgba + (size_t)y * linesize, row_bytes);
		}
		has_frame = true;
	}
	wake_up.notify_one();
}

bool InferenceWorker::get_mask(std::vector<uint8_t> &out_mask, int &width, int &height, uint64_t &version)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (mask_version == version || mask.empty()) {
		return false; // rien de nouveau
	}
	out_mask = mask;
	width = input_width;
	height = input_height;
	version = mask_version;
	return true;
}

void InferenceWorker::set_use_gpu(bool use_gpu_)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (use_gpu == use_gpu_) {
			return;
		}
		use_gpu = use_gpu_;
		reload_requested = true;
	}
	wake_up.notify_one();
}

void InferenceWorker::thread_main()
{
	std::unique_ptr<Segmenter> segmenter;
	bool gpu = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		gpu = use_gpu;
	}

	// Variables locales au thread de l'IA : pas besoin de mutex pour elles.
	std::vector<uint8_t> local_frame;
	std::vector<float> rgb;
	std::vector<float> result;
	std::vector<float> smoothed; // masque lissé dans le temps
	std::vector<uint8_t> local_mask;
	uint64_t total_ns = 0;
	int runs = 0;

	bool need_load = true;

	while (true) {
		// 1. (Re)chargement du modèle, sans tenir le verrou (ça peut prendre une seconde).
		if (need_load) {
			input_width = 0;
			input_height = 0;
			segmenter = std::make_unique<Segmenter>(model_path, gpu);
			if (segmenter->is_ready()) {
				input_width = segmenter->get_input_width();
				input_height = segmenter->get_input_height();
			}
			need_load = false;
			smoothed.clear();
			total_ns = 0;
			runs = 0;
		}

		// 2. On dort jusqu'à ce qu'il y ait quelque chose à faire.
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake_up.wait(lock, [this] { return stop_requested || reload_requested || has_frame; });

			if (stop_requested) {
				break;
			}
			if (reload_requested) {
				reload_requested = false;
				gpu = use_gpu;
				need_load = true;
				has_frame = false;
				mask.clear();
				continue;
			}

			// On récupère l'image (échange de contenu : instantané, pas de copie).
			local_frame.swap(frame);
			has_frame = false;
		}

		if (!segmenter->is_ready()) {
			continue;
		}

		// 3. Conversion RGBA octets (0-255) -> RGB float (0.0-1.0), le format du modèle.
		size_t pixels = (size_t)input_width * (size_t)input_height;
		if (local_frame.size() != pixels * 4) {
			continue; // image d'une autre taille (envoyée avant un rechargement du modèle)
		}
		rgb.resize(pixels * 3);
		for (size_t i = 0; i < pixels; i++) {
			rgb[i * 3 + 0] = local_frame[i * 4 + 0] / 255.0f;
			rgb[i * 3 + 1] = local_frame[i * 4 + 1] / 255.0f;
			rgb[i * 3 + 2] = local_frame[i * 4 + 2] / 255.0f;
		}

		// 4. L'IA calcule le masque.
		uint64_t start = os_gettime_ns();
		if (!segmenter->run(rgb, result)) {
			continue;
		}
		total_ns += os_gettime_ns() - start;

		if (++runs == STATS_EVERY) {
			obs_log(LOG_INFO, "inference: %.2f ms average on %s (%d runs)",
				(double)total_ns / runs / 1000000.0, segmenter->is_using_gpu() ? "GPU" : "CPU", runs);
			total_ns = 0;
			runs = 0;
		}

		// 5. Lissage dans le temps : on mélange le nouveau masque avec le précédent.
		//    keep = part de l'ancien masque qu'on garde (au plus 90 %, sinon le masque ne suivrait plus).
		float keep = temporal_smoothing * 0.9f;
		if (smoothed.size() != pixels) {
			smoothed = result; // premier masque : rien à mélanger
		} else {
			for (size_t i = 0; i < pixels; i++) {
				smoothed[i] = keep * smoothed[i] + (1.0f - keep) * result[i];
			}
		}

		// 6. Conversion du masque float (0.0-1.0) -> octets (0-255), puis publication.
		local_mask.resize(pixels);
		for (size_t i = 0; i < pixels; i++) {
			local_mask[i] = (uint8_t)(std::clamp(smoothed[i], 0.0f, 1.0f) * 255.0f);
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			mask.swap(local_mask);
			mask_version++;
		}
	}

	// Le modèle est détruit ici, dans le thread qui l'a créé.
	segmenter.reset();
}
