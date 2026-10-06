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

#include "ml/segmenter.h"
#include "ml/onnx-loader.h"

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <array>

// L'environnement ONNX Runtime : un seul pour tout le plugin, créé à la première utilisation.
static Ort::Env &get_env()
{
	static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "butterfly");
	return env;
}

Segmenter::Segmenter(const std::string &model_path, bool use_gpu)
{
	if (!onnx_runtime_is_loaded()) {
		obs_log(LOG_ERROR, "segmenter: ONNX Runtime is not available");
		return;
	}

	// On essaie d'abord le GPU, puis le CPU si le GPU échoue (repli automatique).
	if (use_gpu && create_session(model_path, true)) {
		return;
	}
	create_session(model_path, false);
}

// Le destructeur doit être ici (et pas dans le .h) car Ort::Session n'est complet que dans ce fichier.
Segmenter::~Segmenter() = default;

bool Segmenter::create_session(const std::string &model_path, bool gpu)
{
	try {
		Ort::SessionOptions options;
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

		if (gpu) {
			// DirectML impose ces deux réglages.
			options.DisableMemPattern();
			options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

			const OrtDmlApi *dml_api = nullptr;
			Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
				"DML", ORT_API_VERSION, reinterpret_cast<const void **>(&dml_api)));
			// 0 = première carte graphique de la machine.
			Ort::ThrowOnError(dml_api->SessionOptionsAppendExecutionProvider_DML(options, 0));
		}

		// Sous Windows, ONNX Runtime attend un chemin en wchar_t (UTF-16).
		wchar_t *wide_path = nullptr;
		os_utf8_to_wcs_ptr(model_path.c_str(), model_path.size(), &wide_path);
		session = std::make_unique<Ort::Session>(get_env(), wide_path, options);
		bfree(wide_path);

		// Noms et taille de l'entrée / de la sortie du modèle.
		Ort::AllocatorWithDefaultOptions allocator;
		input_name = session->GetInputNameAllocated(0, allocator).get();
		output_name = session->GetOutputNameAllocated(0, allocator).get();

		// Forme de l'entrée : [1, hauteur, largeur, 3] (format « NHWC »).
		std::vector<int64_t> shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
		if (shape.size() != 4 || shape[3] != 3) {
			obs_log(LOG_ERROR, "segmenter: unexpected model input shape");
			session.reset();
			return false;
		}
		input_height = (int)shape[1];
		input_width = (int)shape[2];
		using_gpu = gpu;

		obs_log(LOG_INFO, "segmenter: model loaded on %s, input %dx%d", gpu ? "GPU (DirectML)" : "CPU",
			input_width, input_height);
		return true;

	} catch (const Ort::Exception &e) {
		obs_log(LOG_WARNING, "segmenter: cannot use %s: %s", gpu ? "GPU (DirectML)" : "CPU", e.what());
		session.reset();
		return false;
	}
}

bool Segmenter::run(const std::vector<float> &rgb, std::vector<float> &mask)
{
	size_t pixel_count = (size_t)input_width * (size_t)input_height;
	if (!session || rgb.size() != pixel_count * 3) {
		return false;
	}

	try {
		// On « emballe » notre tableau rgb dans un tenseur ONNX, sans le copier.
		std::array<int64_t, 4> input_shape = {1, input_height, input_width, 3};
		Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		Ort::Value input = Ort::Value::CreateTensor<float>(memory_info, const_cast<float *>(rgb.data()),
								   rgb.size(), input_shape.data(), input_shape.size());

		const char *input_names[] = {input_name.c_str()};
		const char *output_names[] = {output_name.c_str()};

		std::vector<Ort::Value> outputs =
			session->Run(Ort::RunOptions{nullptr}, input_names, &input, 1, output_names, 1);

		// Sortie : [1, hauteur, largeur, 1] -> une probabilité « personne » par pixel.
		const float *result = outputs[0].GetTensorData<float>();
		mask.assign(result, result + pixel_count);
		return true;

	} catch (const Ort::Exception &e) {
		obs_log(LOG_ERROR, "segmenter: inference failed: %s", e.what());
		return false;
	}
}
