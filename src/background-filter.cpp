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

// Noms des réglages, tels qu'ils sont enregistrés dans la scène OBS.
// Ne pas les changer une fois publiés : les scènes existantes ne retrouveraient plus leurs valeurs.
#define SETTING_INTENSITY "intensity"

// Données d'une instance du filtre.
// OBS crée une instance à chaque fois que l'utilisateur ajoute le filtre sur une source.
struct BackgroundFilter {
	obs_source_t *context = nullptr; // le filtre lui-même, vu par OBS

	gs_effect_t *effect = nullptr;          // le shader chargé depuis data/effects/tint.effect
	gs_eparam_t *param_intensity = nullptr; // la variable "intensity" du shader

	float intensity = 0.5f; // valeur du curseur, ramenée entre 0.0 et 1.0
};

// Nom affiché dans le menu « Filtres » d'OBS (traduit via data/locale/*.ini).
static const char *background_filter_get_name(void *)
{
	return obs_module_text("BackgroundFilter.Name");
}

// Appelée à la création et à chaque fois que l'utilisateur touche un réglage.
// On recopie les réglages dans notre structure pour les utiliser pendant le rendu.
static void background_filter_update(void *data, obs_data_t *settings)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	// Le curseur va de 0 à 100 (plus lisible pour l'utilisateur), le shader attend 0.0 à 1.0.
	filter->intensity = (float)obs_data_get_int(settings, SETTING_INTENSITY) / 100.0f;
}

// Appelée quand l'utilisateur ajoute le filtre.
// On renvoie un pointeur vers nos données : OBS nous le redonnera dans toutes les autres fonctions.
static void *background_filter_create(obs_data_t *settings, obs_source_t *source)
{
	BackgroundFilter *filter = new BackgroundFilter();
	filter->context = source;

	// obs_module_file() donne le chemin complet d'un fichier du dossier data/ du plugin.
	char *effect_path = obs_module_file("effects/tint.effect");

	// Tout ce qui touche à la carte graphique doit être entouré de
	// obs_enter_graphics() / obs_leave_graphics().
	obs_enter_graphics();
	char *errors = nullptr;
	filter->effect = gs_effect_create_from_file(effect_path, &errors);
	if (filter->effect) {
		filter->param_intensity = gs_effect_get_param_by_name(filter->effect, "intensity");
	}
	obs_leave_graphics();

	if (!filter->effect) {
		obs_log(LOG_ERROR, "failed to load %s: %s", effect_path, errors ? errors : "file not found");
	}
	bfree(errors);
	bfree(effect_path);

	background_filter_update(filter, settings);

	obs_log(LOG_INFO, "background filter created");
	return filter;
}

// Appelée quand le filtre est supprimé : on libère ce qu'on a créé dans create().
static void background_filter_destroy(void *data)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();

	delete filter;

	obs_log(LOG_INFO, "background filter destroyed");
}

// Valeurs par défaut des réglages (quand on ajoute le filtre pour la première fois).
static void background_filter_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_INTENSITY, 50);
}

// Construit la fenêtre de réglages du filtre.
static obs_properties_t *background_filter_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *intensity = obs_properties_add_int_slider(
		props, SETTING_INTENSITY, obs_module_text("BackgroundFilter.Intensity"), 0, 100, 1);
	obs_property_int_set_suffix(intensity, " %");

	return props;
}

// Appelée à chaque image (30 ou 60 fois par seconde) pour dessiner le résultat.
static void background_filter_video_render(void *data, gs_effect_t *)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	// Si le shader n'a pas pu être chargé, on affiche l'image sans modification.
	if (!filter->effect) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	// 1. OBS prépare l'image de la source pour qu'on puisse la passer à notre shader.
	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
		return;
	}

	// 2. On envoie la valeur du curseur au shader.
	gs_effect_set_float(filter->param_intensity, filter->intensity);

	// 3. OBS dessine l'image en la faisant passer par notre shader (technique "Draw").
	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);
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
	info.update = background_filter_update;
	info.get_defaults = background_filter_get_defaults;
	info.get_properties = background_filter_get_properties;
	info.video_render = background_filter_video_render;

	// OBS copie la structure : la variable locale peut disparaître ensuite.
	obs_register_source(&info);
}
