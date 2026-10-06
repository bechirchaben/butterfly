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

#include "ml/onnx-loader.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <onnxruntime_cxx_api.h>

#include <windows.h>

#include <string>
#include <vector>

static bool loaded = false;

// Renvoie le dossier qui contient butterfly.dll (avec le « \ » final).
static std::wstring get_plugin_dir()
{
	// On demande à Windows « quel module contient cette fonction ? » : c'est butterfly.dll.
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			   reinterpret_cast<LPCWSTR>(&get_plugin_dir), &module);

	wchar_t path[MAX_PATH];
	DWORD length = GetModuleFileNameW(module, path, MAX_PATH);

	std::wstring dir(path, length);
	return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

// Charge une DLL par son chemin complet.
static bool load_dll(const std::wstring &dir, const wchar_t *name)
{
	std::wstring full_path = dir + name;

	// LOAD_WITH_ALTERED_SEARCH_PATH : les dépendances de cette DLL sont cherchées
	// d'abord dans SON dossier (ex. onnxruntime_providers_shared.dll).
	if (!LoadLibraryExW(full_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
		obs_log(LOG_ERROR, "failed to load %ls (Windows error %lu)", full_path.c_str(), GetLastError());
		return false;
	}
	return true;
}

bool onnx_runtime_load()
{
	if (loaded) {
		return true;
	}

	std::wstring dir = get_plugin_dir();

	// DirectML d'abord : onnxruntime.dll la cherche par son nom en se chargeant,
	// et Windows réutilise alors celle qui est déjà chargée (la nôtre, pas celle de System32).
	if (!load_dll(dir, L"DirectML.dll") || !load_dll(dir, L"onnxruntime.dll")) {
		return false;
	}

	// À partir d'ici, appeler une fonction d'ONNX Runtime est sans danger :
	// le chargement différé (/DELAYLOAD) trouvera la DLL déjà chargée.
	const OrtApiBase *api_base = OrtGetApiBase();
	const OrtApi *api = api_base->GetApi(ORT_API_VERSION);
	if (!api) {
		obs_log(LOG_ERROR, "ONNX Runtime %s is too old (API version %d required)", api_base->GetVersionString(),
			ORT_API_VERSION);
		return false;
	}

	// Initialise l'API C++ (voir ORT_API_MANUAL_INIT dans cmake/onnxruntime.cmake).
	Ort::InitApi(api);
	loaded = true;

	// Liste les « fournisseurs d'exécution » disponibles (DirectML = GPU, CPU...).
	std::string providers;
	for (const std::string &provider : Ort::GetAvailableProviders()) {
		providers += provider + " ";
	}
	obs_log(LOG_INFO, "ONNX Runtime %s loaded, providers: %s", api_base->GetVersionString(), providers.c_str());

	return true;
}

bool onnx_runtime_is_loaded()
{
	return loaded;
}
