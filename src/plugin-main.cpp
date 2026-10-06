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

#include "background-filter.h"

// Déclare ce fichier .dll comme un module OBS.
OBS_DECLARE_MODULE()

// Charge les traductions depuis data/locale/ (anglais par défaut).
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// Appelée par OBS au démarrage : on y enregistre tous les filtres Butterfly.
bool obs_module_load(void)
{
	register_background_filter();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

// Appelée par OBS à la fermeture.
void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
