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

// Chargement d'ONNX Runtime (le moteur qui fait tourner les modèles d'IA).
//
// Pourquoi un chargement « à la main » ?
// Windows contient déjà un ancien onnxruntime.dll dans C:\Windows\System32.
// Si on laissait Windows chercher la DLL tout seul, il pourrait prendre celle-là,
// trop ancienne pour Butterfly. On charge donc NOTRE version par son chemin complet
// (à côté de butterfly.dll), avant toute utilisation.

// Charge ONNX Runtime et DirectML. À appeler une fois, au chargement du plugin.
// Renvoie false si l'IA est indisponible (le plugin continue de fonctionner sans IA).
bool onnx_runtime_load();

// true si onnx_runtime_load() a réussi : on peut utiliser l'IA.
bool onnx_runtime_is_loaded();
