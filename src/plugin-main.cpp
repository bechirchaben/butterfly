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

#include <obs-module.h>
#include <plugin-support.h>

#include <util/platform.h>

#include "background-filter.h"
#include "ml/onnx-loader.h"
#include "ml/segmenter.h"

// Déclare ce fichier .dll comme un module OBS.
OBS_DECLARE_MODULE()

// Charge les traductions depuis data/locale/ (anglais par défaut).
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// AUTO-TEST TEMPORAIRE (Sprint 2, étape 1) : vérifie que l'IA tourne et mesure sa vitesse.
// Sera retiré quand le filtre utilisera le modèle sur les vraies images de la caméra.
static void segmenter_self_test()
{
	char *model_path = obs_module_file("models/selfie_segmentation.onnx");
	if (!model_path) {
		obs_log(LOG_ERROR, "self-test: model file not found");
		return;
	}

	Segmenter segmenter(model_path, true);
	bfree(model_path);
	if (!segmenter.is_ready()) {
		return;
	}

	// Image de test : gris moyen partout.
	size_t pixels = (size_t)segmenter.get_input_width() * (size_t)segmenter.get_input_height();
	std::vector<float> rgb(pixels * 3, 0.5f);
	std::vector<float> mask;

	// 1er passage : plus lent (le GPU prépare le modèle). On mesure le 2e.
	segmenter.run(rgb, mask);

	uint64_t start = os_gettime_ns();
	bool ok = segmenter.run(rgb, mask);
	double ms = (double)(os_gettime_ns() - start) / 1000000.0;

	obs_log(LOG_INFO, "self-test: inference %s on %s in %.2f ms", ok ? "OK" : "FAILED",
		segmenter.is_using_gpu() ? "GPU" : "CPU", ms);
}

// Appelée par OBS au démarrage : on y enregistre tous les filtres Butterfly.
bool obs_module_load(void)
{
	// L'IA est facultative : si elle ne se charge pas, les filtres marchent quand même (sans IA).
	if (onnx_runtime_load()) {
		segmenter_self_test();
	}

	register_background_filter();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

// Appelée par OBS à la fermeture.
void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
