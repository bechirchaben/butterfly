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
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <plugin-support.h>

// Noms des réglages, tels qu'ils sont enregistrés dans la scène OBS.
// Ne pas les changer une fois publiés : les scènes existantes ne retrouveraient plus leurs valeurs.
#define SETTING_MODE "mode"
#define SETTING_INTENSITY "intensity"

// Modes du filtre. Sprint 1 : modes d'apprentissage.
// Au Sprint 3-4 ils deviendront Flou / Image / Source OBS / Transparent.
enum FilterMode {
	MODE_TINT = 0, // teinte rouge
	MODE_BLUR = 1, // flou de toute l'image
};

// Rayon du flou (en pixels) quand l'intensité est à 100 %.
static const float MAX_BLUR_RADIUS = 32.0f;

// Nombre de voisins lus de chaque côté dans blur.effect (doit correspondre au shader).
static const float BLUR_SAMPLES_PER_SIDE = 16.0f;

// Données d'une instance du filtre.
// OBS crée une instance à chaque fois que l'utilisateur ajoute le filtre sur une source.
struct BackgroundFilter {
	obs_source_t *context = nullptr; // le filtre lui-même, vu par OBS

	// Shader de teinte (data/effects/tint.effect) et sa variable "intensity".
	gs_effect_t *tint_effect = nullptr;
	gs_eparam_t *tint_param_intensity = nullptr;

	// Shader de flou (data/effects/blur.effect) et ses variables.
	gs_effect_t *blur_effect = nullptr;
	gs_eparam_t *blur_param_image = nullptr;
	gs_eparam_t *blur_param_step = nullptr;

	// Texture intermédiaire : résultat de la passe de flou horizontale.
	gs_texrender_t *horizontal_pass = nullptr;

	// Réglages, recopiés depuis OBS par update().
	int mode = MODE_TINT;
	float intensity = 0.5f; // ramenée entre 0.0 et 1.0
};

// Charge un shader du dossier data/ du plugin. Renvoie nullptr en cas d'erreur.
// À appeler entre obs_enter_graphics() et obs_leave_graphics().
static gs_effect_t *load_effect(const char *file)
{
	// obs_module_file() donne le chemin complet d'un fichier du dossier data/ du plugin.
	char *path = obs_module_file(file);
	if (!path) {
		obs_log(LOG_ERROR, "file not found: %s", file);
		return nullptr;
	}

	char *errors = nullptr;
	gs_effect_t *effect = gs_effect_create_from_file(path, &errors);
	if (!effect) {
		obs_log(LOG_ERROR, "failed to load %s: %s", path, errors ? errors : "unknown error");
	}

	bfree(errors);
	bfree(path);
	return effect;
}

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

	filter->mode = (int)obs_data_get_int(settings, SETTING_MODE);

	// Le curseur va de 0 à 100 (plus lisible pour l'utilisateur), les shaders attendent 0.0 à 1.0.
	filter->intensity = (float)obs_data_get_int(settings, SETTING_INTENSITY) / 100.0f;
}

// Appelée quand l'utilisateur ajoute le filtre.
// On renvoie un pointeur vers nos données : OBS nous le redonnera dans toutes les autres fonctions.
static void *background_filter_create(obs_data_t *settings, obs_source_t *source)
{
	BackgroundFilter *filter = new BackgroundFilter();
	filter->context = source;

	// Tout ce qui touche à la carte graphique doit être entouré de
	// obs_enter_graphics() / obs_leave_graphics().
	obs_enter_graphics();

	filter->tint_effect = load_effect("effects/tint.effect");
	if (filter->tint_effect) {
		filter->tint_param_intensity = gs_effect_get_param_by_name(filter->tint_effect, "intensity");
	}

	filter->blur_effect = load_effect("effects/blur.effect");
	if (filter->blur_effect) {
		filter->blur_param_image = gs_effect_get_param_by_name(filter->blur_effect, "image");
		filter->blur_param_step = gs_effect_get_param_by_name(filter->blur_effect, "sample_step");
	}

	filter->horizontal_pass = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	obs_leave_graphics();

	background_filter_update(filter, settings);

	obs_log(LOG_INFO, "background filter created");
	return filter;
}

// Appelée quand le filtre est supprimé : on libère ce qu'on a créé dans create().
static void background_filter_destroy(void *data)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	obs_enter_graphics();
	gs_effect_destroy(filter->tint_effect);
	gs_effect_destroy(filter->blur_effect);
	gs_texrender_destroy(filter->horizontal_pass);
	obs_leave_graphics();

	delete filter;

	obs_log(LOG_INFO, "background filter destroyed");
}

// Valeurs par défaut des réglages (quand on ajoute le filtre pour la première fois).
static void background_filter_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_MODE, MODE_TINT);
	obs_data_set_default_int(settings, SETTING_INTENSITY, 50);
}

// Construit la fenêtre de réglages du filtre.
static obs_properties_t *background_filter_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(props, SETTING_MODE, obs_module_text("BackgroundFilter.Mode"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("BackgroundFilter.Mode.Tint"), MODE_TINT);
	obs_property_list_add_int(mode, obs_module_text("BackgroundFilter.Mode.Blur"), MODE_BLUR);

	obs_property_t *intensity = obs_properties_add_int_slider(
		props, SETTING_INTENSITY, obs_module_text("BackgroundFilter.Intensity"), 0, 100, 1);
	obs_property_int_set_suffix(intensity, " %");

	return props;
}

// Mode Teinte : une seule passe, OBS dessine la source à travers tint.effect.
static void render_tint(BackgroundFilter *filter)
{
	// 1. OBS prépare l'image de la source pour qu'on puisse la passer à notre shader.
	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
		return;
	}

	// 2. On envoie la valeur du curseur au shader.
	gs_effect_set_float(filter->tint_param_intensity, filter->intensity);

	// 3. OBS dessine l'image en la faisant passer par notre shader (technique "Draw").
	obs_source_process_filter_end(filter->context, filter->tint_effect, 0, 0);
}

// Mode Flou : deux passes.
//   Passe 1 : source --(flou horizontal)--> texture intermédiaire "horizontal_pass"
//   Passe 2 : horizontal_pass --(flou vertical)--> écran
static void render_blur(BackgroundFilter *filter)
{
	// Taille de l'image de la source sur laquelle le filtre est appliqué.
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	// Rien à flouter : on affiche l'image telle quelle.
	if (width == 0 || height == 0 || filter->intensity <= 0.0f) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	// Distance (en pixels) entre deux voisins lus par le shader.
	float step_pixels = filter->intensity * MAX_BLUR_RADIUS / BLUR_SAMPLES_PER_SIDE;
	vec2 step;

	// --- Passe 1 : flou horizontal, dessiné dans la texture intermédiaire ---
	// OBS_NO_DIRECT_RENDERING : on veut que la source soit d'abord rendue dans une texture.
	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		return;
	}

	gs_texrender_reset(filter->horizontal_pass);

	// On remplace les pixels au lieu de les mélanger avec ce qui est déjà dessiné.
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(filter->horizontal_pass, width, height)) {
		vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);

		// Repère en pixels : (0,0) en haut à gauche, (width,height) en bas à droite.
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		vec2_set(&step, step_pixels / (float)width, 0.0f);
		gs_effect_set_vec2(filter->blur_param_step, &step);
		obs_source_process_filter_end(filter->context, filter->blur_effect, width, height);

		gs_texrender_end(filter->horizontal_pass);
	}

	gs_blend_state_pop();

	// --- Passe 2 : flou vertical, dessiné directement à l'écran ---
	gs_texture_t *horizontal_result = gs_texrender_get_texture(filter->horizontal_pass);
	if (!horizontal_result) {
		return;
	}

	gs_effect_set_texture(filter->blur_param_image, horizontal_result);
	vec2_set(&step, 0.0f, step_pixels / (float)height);
	gs_effect_set_vec2(filter->blur_param_step, &step);

	while (gs_effect_loop(filter->blur_effect, "Draw")) {
		gs_draw_sprite(horizontal_result, 0, width, height);
	}
}

// Appelée à chaque image (30 ou 60 fois par seconde) pour dessiner le résultat.
static void background_filter_video_render(void *data, gs_effect_t *)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	if (filter->mode == MODE_BLUR && filter->blur_effect) {
		render_blur(filter);
	} else if (filter->mode == MODE_TINT && filter->tint_effect) {
		render_tint(filter);
	} else {
		// Shader manquant (voir les erreurs dans le journal) : image sans modification.
		obs_source_skip_video_filter(filter->context);
	}
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
