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

#include <memory>
#include <string>
#include <vector>

namespace Ort {
struct Session;
}

// Segmentation personne / fond avec un modèle ONNX (MediaPipe Selfie Segmentation).
//
// Entrée  : une petite image RGB (ex. 256 x 256), 3 valeurs float par pixel entre 0 et 1,
//           rangées ligne par ligne : r, g, b, r, g, b...
// Sortie  : un masque de la même taille, 1 valeur par pixel :
//           0 = fond, 1 = personne.
class Segmenter {
public:
	// Charge le modèle. use_gpu = true : essaie DirectML (carte graphique),
	// et passe automatiquement sur le CPU si ça ne marche pas.
	Segmenter(const std::string &model_path, bool use_gpu);
	~Segmenter();

	// true si le modèle est chargé et prêt.
	bool is_ready() const { return session != nullptr; }

	// true si le modèle tourne sur la carte graphique (DirectML).
	bool is_using_gpu() const { return using_gpu; }

	// Taille d'image attendue par le modèle.
	int get_input_width() const { return input_width; }
	int get_input_height() const { return input_height; }

	// Lance le modèle. rgb doit contenir input_width * input_height * 3 valeurs.
	// Remplit mask avec input_width * input_height valeurs. Renvoie false en cas d'erreur.
	bool run(const std::vector<float> &rgb, std::vector<float> &mask);

private:
	bool create_session(const std::string &model_path, bool gpu);

	std::unique_ptr<Ort::Session> session;
	bool using_gpu = false;

	std::string input_name;
	std::string output_name;
	int input_width = 0;
	int input_height = 0;
};
