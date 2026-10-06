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

#include "background-filter.h"

#include <obs-module.h>
#include <plugin-support.h>

// Données d'une instance du filtre.
// OBS crée une instance à chaque fois que l'utilisateur ajoute le filtre sur une source.
struct BackgroundFilter {
	obs_source_t *context = nullptr; // le filtre lui-même, vu par OBS
};

// Nom affiché dans le menu « Filtres » d'OBS (traduit via data/locale/*.ini).
static const char *background_filter_get_name(void *)
{
	return obs_module_text("BackgroundFilter.Name");
}

// Appelée quand l'utilisateur ajoute le filtre.
// On renvoie un pointeur vers nos données : OBS nous le redonnera dans toutes les autres fonctions.
static void *background_filter_create(obs_data_t *, obs_source_t *source)
{
	BackgroundFilter *filter = new BackgroundFilter();
	filter->context = source;

	obs_log(LOG_INFO, "background filter created");
	return filter;
}

// Appelée quand le filtre est supprimé : on libère ce qu'on a créé dans create().
static void background_filter_destroy(void *data)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);
	delete filter;

	obs_log(LOG_INFO, "background filter destroyed");
}

// Appelée à chaque image (30 ou 60 fois par seconde) pour dessiner le résultat.
static void background_filter_video_render(void *data, gs_effect_t *)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	// Passe-plat : on demande à OBS de dessiner la source telle quelle.
	// Au Sprint 1, on remplacera ceci par nos propres shaders.
	obs_source_skip_video_filter(filter->context);
}

void register_background_filter()
{
	// obs_source_info décrit le filtre à OBS : son identifiant, son type
	// et les fonctions à appeler pour chaque événement.
	obs_source_info info = {};
	info.id = "butterfly_background";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = background_filter_get_name;
	info.create = background_filter_create;
	info.destroy = background_filter_destroy;
	info.video_render = background_filter_video_render;

	// OBS copie la structure : la variable locale peut disparaître ensuite.
	obs_register_source(&info);
}
