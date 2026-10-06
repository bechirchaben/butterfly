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

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Fait tourner l'IA dans un thread séparé, pour que le rendu d'OBS n'attende jamais.
//
// Deux threads se partagent cet objet :
//   - le thread GRAPHIQUE d'OBS (60 fois/s) : envoie des images (submit_frame)
//     et récupère le dernier masque calculé (get_mask) ;
//   - le thread de l'IA (créé ici) : charge le modèle, puis calcule un masque
//     à chaque nouvelle image reçue.
//
// Un mutex (« verrou ») protège les données partagées : un seul thread à la fois peut
// les lire ou les modifier. Sans lui, un thread pourrait lire une image à moitié écrite.
class InferenceWorker {
public:
	// Démarre le thread de l'IA, qui charge le modèle en arrière-plan.
	InferenceWorker(const std::string &model_path, bool use_gpu);

	// Arrête le thread et attend qu'il ait fini.
	~InferenceWorker();

	// Taille d'image attendue par le modèle (0 tant que le modèle n'est pas chargé).
	int get_input_width() const { return input_width; }
	int get_input_height() const { return input_height; }

	// true si l'IA est prête et libre : inutile d'envoyer une image sinon.
	bool wants_frame();

	// Envoie une image RGBA (4 octets par pixel) de la taille attendue par le modèle.
	// linesize = nombre d'octets d'une ligne (peut être plus grand que width * 4).
	void submit_frame(const uint8_t *rgba, uint32_t linesize, int width, int height);

	// Si un masque plus récent que « version » est disponible, le copie dans mask
	// (1 octet par pixel : 0 = fond, 255 = personne), met à jour version et renvoie true.
	bool get_mask(std::vector<uint8_t> &mask, int &width, int &height, uint64_t &version);

	// Change le matériel utilisé (GPU ou CPU) : le modèle est rechargé.
	void set_use_gpu(bool use_gpu);

	// Lissage dans le temps, de 0.0 (aucun) à 1.0 (maximum).
	// Chaque nouveau masque est mélangé avec le précédent : moins de scintillement,
	// mais un léger retard quand on bouge vite.
	void set_temporal_smoothing(float amount) { temporal_smoothing = amount; }

private:
	// Fonction exécutée par le thread de l'IA.
	void thread_main();

	std::string model_path;
	std::thread thread;

	// --- Données partagées : toujours y accéder avec le mutex verrouillé ---
	std::mutex mutex;
	std::condition_variable wake_up; // pour réveiller le thread de l'IA
	bool stop_requested = false;
	bool use_gpu = true;
	bool reload_requested = false;
	bool has_frame = false;
	std::vector<uint8_t> frame; // dernière image reçue (RGBA, sans marge en fin de ligne)
	std::vector<uint8_t> mask;  // dernier masque calculé
	uint64_t mask_version = 0;  // augmente à chaque nouveau masque

	// Lisibles sans mutex grâce à std::atomic (lecture/écriture en une seule opération).
	std::atomic<int> input_width{0};
	std::atomic<int> input_height{0};
	std::atomic<float> temporal_smoothing{0.5f};
};
