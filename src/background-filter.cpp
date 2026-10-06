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
#include "ml/inference-worker.h"
#include "ml/onnx-loader.h"

#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <plugin-support.h>

#include <memory>
#include <vector>

// Noms des réglages, tels qu'ils sont enregistrés dans la scène OBS.
// Ne pas les changer une fois publiés : les scènes existantes ne retrouveraient plus leurs valeurs.
#define SETTING_MODE "mode"
#define SETTING_INTENSITY "intensity"
#define SETTING_THRESHOLD "threshold"
#define SETTING_FEATHER "feather"
#define SETTING_SMOOTHING "temporal_smoothing"
#define SETTING_DEVICE "device"

// Modes du filtre.
// Tint et Blur sont des modes d'apprentissage (Sprint 1) qui disparaîtront au Sprint 4,
// quand les modes Image / Source OBS / Transparent arriveront.
enum FilterMode {
	MODE_TINT = 0,            // teinte rouge
	MODE_BLUR = 1,            // flou de toute l'image
	MODE_DEBUG_MASK = 2,      // affiche le masque final (blanc = personne, noir = fond)
	MODE_BACKGROUND_BLUR = 3, // flou de l'arrière-plan seulement (style Google Meet)
};

// Matériel utilisé par l'IA.
enum Device {
	DEVICE_AUTO = 0, // carte graphique (DirectML) si possible, sinon CPU
	DEVICE_CPU = 1,  // processeur uniquement
};

// Rayon du flou (en pixels) quand l'intensité est à 100 %.
static const float MAX_BLUR_RADIUS = 32.0f;

// Rayon de l'adoucissement du masque (en pixels du masque 256 x 256) quand il est à 100 %.
static const float MAX_FEATHER_RADIUS = 8.0f;

// Nombre de voisins lus de chaque côté dans blur.effect (doit correspondre au shader).
static const float BLUR_SAMPLES_PER_SIDE = 16.0f;

// Données d'une instance du filtre.
// OBS crée une instance à chaque fois que l'utilisateur ajoute le filtre sur une source.
struct BackgroundFilter {
	obs_source_t *context = nullptr; // le filtre lui-même, vu par OBS

	// --- Shaders (data/effects/*.effect) et leurs variables ---
	gs_effect_t *tint_effect = nullptr;
	gs_eparam_t *tint_param_image = nullptr;
	gs_eparam_t *tint_param_intensity = nullptr;

	gs_effect_t *blur_effect = nullptr;
	gs_eparam_t *blur_param_image = nullptr;
	gs_eparam_t *blur_param_step = nullptr;
	gs_eparam_t *blur_param_mask = nullptr;

	gs_effect_t *composite_effect = nullptr;
	gs_eparam_t *composite_param_image = nullptr;
	gs_eparam_t *composite_param_background = nullptr;
	gs_eparam_t *composite_param_mask = nullptr;
	gs_eparam_t *composite_param_threshold = nullptr;
	gs_eparam_t *composite_param_edge_width = nullptr;

	// --- Textures sur la carte graphique ---
	gs_texrender_t *source_render = nullptr;        // l'image de la caméra, capturée une fois par image
	gs_texrender_t *horizontal_pass = nullptr;      // passe de flou horizontale (taille caméra)
	gs_texrender_t *blurred_render = nullptr;       // caméra floutée (taille caméra)
	gs_texrender_t *small_render = nullptr;         // image réduite à la taille du modèle (ex. 256 x 256)
	gs_stagesurf_t *stage_surface = nullptr;        // pour copier small_render vers la mémoire du CPU
	gs_texture_t *mask_texture = nullptr;           // dernier masque de l'IA, renvoyé sur la carte graphique
	gs_texrender_t *mask_horizontal_pass = nullptr; // adoucissement du masque, passe horizontale
	gs_texrender_t *mask_feathered = nullptr;       // masque adouci

	// --- IA ---
	std::unique_ptr<InferenceWorker> worker; // nullptr si l'IA n'est pas disponible
	std::vector<uint8_t> mask_buffer;        // copie du dernier masque (côté CPU)
	uint64_t mask_version = 0;               // version du masque déjà envoyé dans mask_texture

	// OBS peut dessiner le filtre plusieurs fois par image (aperçu des propriétés, multiview...).
	// On n'envoie qu'une image à l'IA par image d'OBS : video_tick() remet ce drapeau à true.
	bool frame_needed = true;

	// --- Réglages, recopiés depuis OBS par update() ---
	int mode = MODE_BACKGROUND_BLUR;
	float intensity = 0.5f;          // force du flou, 0.0 à 1.0
	float threshold = 0.5f;          // seuil personne / fond, 0.0 à 1.0
	float feather = 0.5f;            // adoucissement des bords, 0.0 à 1.0
	float temporal_smoothing = 0.5f; // lissage dans le temps, 0.0 à 1.0
	bool use_gpu = true;
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

// Dessine une texture à la taille width x height, à travers un shader.
static void draw_texture(gs_effect_t *effect, gs_eparam_t *image_param, gs_texture_t *texture, uint32_t width,
			 uint32_t height, const char *technique = "Draw")
{
	gs_effect_set_texture(image_param, texture);
	while (gs_effect_loop(effect, technique)) {
		gs_draw_sprite(texture, 0, width, height);
	}
}

// Dessine une texture telle quelle (shader par défaut d'OBS).
static void draw_plain(gs_texture_t *texture, uint32_t width, uint32_t height)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	draw_texture(effect, gs_effect_get_param_by_name(effect, "image"), texture, width, height);
}

// Commence le dessin dans une texture intermédiaire de taille width x height.
// Renvoie false si c'est impossible. Si true : dessiner, puis appeler end_texrender().
static bool begin_texrender(gs_texrender_t *texrender, uint32_t width, uint32_t height)
{
	gs_texrender_reset(texrender);
	if (!gs_texrender_begin(texrender, width, height)) {
		return false;
	}

	// On remplace les pixels au lieu de les mélanger avec ce qui est déjà dessiné.
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);

	// Repère en pixels : (0,0) en haut à gauche, (width,height) en bas à droite.
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	return true;
}

static void end_texrender(gs_texrender_t *texrender)
{
	gs_blend_state_pop();
	gs_texrender_end(texrender);
}

// Floute une texture en deux passes (horizontale puis verticale).
//   source           : la texture à flouter (taille width x height)
//   horizontal_pass  : texture intermédiaire pour la 1re passe
//   destination      : où écrire le résultat ; nullptr = directement à l'écran
//   step_pixels      : distance entre deux voisins lus par le shader
//   mask             : si fourni, seul le FOND est flouté (les pixels de la personne ne comptent pas)
// Renvoie la texture floutée (ou nullptr si dessinée à l'écran / en cas d'erreur).
static gs_texture_t *blur_texture(BackgroundFilter *filter, gs_texture_t *source, gs_texrender_t *horizontal_pass,
				  gs_texrender_t *destination, uint32_t width, uint32_t height, float step_pixels,
				  gs_texture_t *mask = nullptr)
{
	const char *technique = mask ? "DrawMasked" : "Draw";
	gs_effect_set_texture(filter->blur_param_mask, mask);
	vec2 step;

	// Passe 1 : flou horizontal -> horizontal_pass
	if (!begin_texrender(horizontal_pass, width, height)) {
		return nullptr;
	}
	vec2_set(&step, step_pixels / (float)width, 0.0f);
	gs_effect_set_vec2(filter->blur_param_step, &step);
	draw_texture(filter->blur_effect, filter->blur_param_image, source, width, height, technique);
	end_texrender(horizontal_pass);

	// Passe 2 : flou vertical -> destination (ou écran)
	vec2_set(&step, 0.0f, step_pixels / (float)height);
	gs_effect_set_vec2(filter->blur_param_step, &step);
	gs_texture_t *horizontal_result = gs_texrender_get_texture(horizontal_pass);

	if (!destination) {
		draw_texture(filter->blur_effect, filter->blur_param_image, horizontal_result, width, height,
			     technique);
		return nullptr;
	}

	if (!begin_texrender(destination, width, height)) {
		return nullptr;
	}
	draw_texture(filter->blur_effect, filter->blur_param_image, horizontal_result, width, height, technique);
	end_texrender(destination);
	return gs_texrender_get_texture(destination);
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

	// Les curseurs vont de 0 à 100 (plus lisible pour l'utilisateur), les shaders attendent 0.0 à 1.0.
	filter->intensity = (float)obs_data_get_int(settings, SETTING_INTENSITY) / 100.0f;
	filter->threshold = (float)obs_data_get_int(settings, SETTING_THRESHOLD) / 100.0f;
	filter->feather = (float)obs_data_get_int(settings, SETTING_FEATHER) / 100.0f;
	filter->temporal_smoothing = (float)obs_data_get_int(settings, SETTING_SMOOTHING) / 100.0f;

	filter->use_gpu = obs_data_get_int(settings, SETTING_DEVICE) == DEVICE_AUTO;
	if (filter->worker) {
		filter->worker->set_use_gpu(filter->use_gpu);
		filter->worker->set_temporal_smoothing(filter->temporal_smoothing);
	}
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
		filter->tint_param_image = gs_effect_get_param_by_name(filter->tint_effect, "image");
		filter->tint_param_intensity = gs_effect_get_param_by_name(filter->tint_effect, "intensity");
	}

	filter->blur_effect = load_effect("effects/blur.effect");
	if (filter->blur_effect) {
		filter->blur_param_image = gs_effect_get_param_by_name(filter->blur_effect, "image");
		filter->blur_param_step = gs_effect_get_param_by_name(filter->blur_effect, "sample_step");
		filter->blur_param_mask = gs_effect_get_param_by_name(filter->blur_effect, "mask");
	}

	filter->composite_effect = load_effect("effects/composite.effect");
	if (filter->composite_effect) {
		gs_effect_t *e = filter->composite_effect;
		filter->composite_param_image = gs_effect_get_param_by_name(e, "image");
		filter->composite_param_background = gs_effect_get_param_by_name(e, "background");
		filter->composite_param_mask = gs_effect_get_param_by_name(e, "mask");
		filter->composite_param_threshold = gs_effect_get_param_by_name(e, "threshold");
		filter->composite_param_edge_width = gs_effect_get_param_by_name(e, "edge_width");
	}

	filter->source_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->horizontal_pass = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->blurred_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->small_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	// Le masque n'a qu'un canal : GS_R8 (1 octet par pixel).
	filter->mask_horizontal_pass = gs_texrender_create(GS_R8, GS_ZS_NONE);
	filter->mask_feathered = gs_texrender_create(GS_R8, GS_ZS_NONE);

	obs_leave_graphics();

	background_filter_update(filter, settings);

	// L'IA démarre dans son propre thread : le chargement du modèle ne bloque pas OBS.
	if (onnx_runtime_is_loaded()) {
		char *model_path = obs_module_file("models/selfie_segmentation.onnx");
		if (model_path) {
			filter->worker = std::make_unique<InferenceWorker>(model_path, filter->use_gpu);
			filter->worker->set_temporal_smoothing(filter->temporal_smoothing);
			bfree(model_path);
		} else {
			obs_log(LOG_ERROR, "model file not found: models/selfie_segmentation.onnx");
		}
	}

	obs_log(LOG_INFO, "background filter created");
	return filter;
}

// Appelée quand le filtre est supprimé : on libère ce qu'on a créé dans create().
static void background_filter_destroy(void *data)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	// Arrête d'abord le thread de l'IA.
	filter->worker.reset();

	obs_enter_graphics();
	gs_effect_destroy(filter->tint_effect);
	gs_effect_destroy(filter->blur_effect);
	gs_effect_destroy(filter->composite_effect);
	gs_texrender_destroy(filter->source_render);
	gs_texrender_destroy(filter->horizontal_pass);
	gs_texrender_destroy(filter->blurred_render);
	gs_texrender_destroy(filter->small_render);
	gs_stagesurface_destroy(filter->stage_surface);
	gs_texture_destroy(filter->mask_texture);
	gs_texrender_destroy(filter->mask_horizontal_pass);
	gs_texrender_destroy(filter->mask_feathered);
	obs_leave_graphics();

	delete filter;

	obs_log(LOG_INFO, "background filter destroyed");
}

// Valeurs par défaut des réglages (quand on ajoute le filtre pour la première fois).
static void background_filter_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_MODE, MODE_BACKGROUND_BLUR);
	obs_data_set_default_int(settings, SETTING_INTENSITY, 50);
	obs_data_set_default_int(settings, SETTING_THRESHOLD, 50);
	obs_data_set_default_int(settings, SETTING_FEATHER, 50);
	obs_data_set_default_int(settings, SETTING_SMOOTHING, 50);
	obs_data_set_default_int(settings, SETTING_DEVICE, DEVICE_AUTO);
}

// Appelée quand l'utilisateur change de mode : affiche seulement les réglages utiles à ce mode.
static bool mode_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	int mode = (int)obs_data_get_int(settings, SETTING_MODE);
	bool uses_ai = mode == MODE_BACKGROUND_BLUR || mode == MODE_DEBUG_MASK;

	obs_property_set_visible(obs_properties_get(props, SETTING_INTENSITY), mode != MODE_DEBUG_MASK);
	obs_property_set_visible(obs_properties_get(props, SETTING_THRESHOLD), uses_ai);
	obs_property_set_visible(obs_properties_get(props, SETTING_FEATHER), uses_ai);
	obs_property_set_visible(obs_properties_get(props, SETTING_SMOOTHING), uses_ai);
	obs_property_set_visible(obs_properties_get(props, SETTING_DEVICE), uses_ai);

	return true; // true = OBS doit rafraîchir la fenêtre des réglages
}

// Ajoute un curseur de 0 à 100 %.
static void add_percent_slider(obs_properties_t *props, const char *setting, const char *text_key)
{
	obs_property_t *slider = obs_properties_add_int_slider(props, setting, obs_module_text(text_key), 0, 100, 1);
	obs_property_int_set_suffix(slider, " %");
}

// Construit la fenêtre de réglages du filtre.
static obs_properties_t *background_filter_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(props, SETTING_MODE, obs_module_text("BackgroundFilter.Mode"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("BackgroundFilter.Mode.BackgroundBlur"), MODE_BACKGROUND_BLUR);
	obs_property_list_add_int(mode, obs_module_text("BackgroundFilter.Mode.DebugMask"), MODE_DEBUG_MASK);
	obs_property_list_add_int(mode, obs_module_text("BackgroundFilter.Mode.Blur"), MODE_BLUR);
	obs_property_list_add_int(mode, obs_module_text("BackgroundFilter.Mode.Tint"), MODE_TINT);
	obs_property_set_modified_callback(mode, mode_modified);

	add_percent_slider(props, SETTING_INTENSITY, "BackgroundFilter.Intensity");
	add_percent_slider(props, SETTING_THRESHOLD, "BackgroundFilter.Threshold");
	add_percent_slider(props, SETTING_FEATHER, "BackgroundFilter.Feather");
	add_percent_slider(props, SETTING_SMOOTHING, "BackgroundFilter.TemporalSmoothing");

	obs_property_t *device = obs_properties_add_list(props, SETTING_DEVICE,
							 obs_module_text("BackgroundFilter.Device"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(device, obs_module_text("BackgroundFilter.Device.Auto"), DEVICE_AUTO);
	obs_property_list_add_int(device, obs_module_text("BackgroundFilter.Device.CPU"), DEVICE_CPU);

	return props;
}

// Capture l'image de la source (la caméra) dans filter->source_render.
static bool capture_source(BackgroundFilter *filter, uint32_t width, uint32_t height)
{
	// OBS_NO_DIRECT_RENDERING : on veut que la source soit rendue dans une texture.
	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		return false;
	}

	if (!begin_texrender(filter->source_render, width, height)) {
		return false;
	}
	// Dessin simple, sans effet (shader par défaut d'OBS).
	obs_source_process_filter_end(filter->context, obs_get_base_effect(OBS_EFFECT_DEFAULT), width, height);
	end_texrender(filter->source_render);
	return true;
}

// Envoie une version réduite de l'image à l'IA (si elle est libre).
static void send_frame_to_ai(BackgroundFilter *filter, gs_texture_t *source)
{
	if (!filter->frame_needed || !filter->worker || !filter->worker->wants_frame()) {
		return;
	}
	filter->frame_needed = false;

	uint32_t width = (uint32_t)filter->worker->get_input_width();
	uint32_t height = (uint32_t)filter->worker->get_input_height();

	// 1. Réduction de l'image à la taille du modèle, sur la carte graphique (rapide).
	if (!begin_texrender(filter->small_render, width, height)) {
		return;
	}
	draw_plain(source, width, height);
	end_texrender(filter->small_render);

	// 2. Copie de la carte graphique vers la mémoire du CPU, via une « stage surface ».
	if (!filter->stage_surface || gs_stagesurface_get_width(filter->stage_surface) != width ||
	    gs_stagesurface_get_height(filter->stage_surface) != height) {
		gs_stagesurface_destroy(filter->stage_surface);
		filter->stage_surface = gs_stagesurface_create(width, height, GS_RGBA);
	}
	gs_stage_texture(filter->stage_surface, gs_texrender_get_texture(filter->small_render));

	uint8_t *pixels = nullptr;
	uint32_t linesize = 0;
	if (gs_stagesurface_map(filter->stage_surface, &pixels, &linesize)) {
		// 3. Envoi au thread de l'IA (copie rapide, puis on rend la main à OBS).
		filter->worker->submit_frame(pixels, linesize, (int)width, (int)height);
		gs_stagesurface_unmap(filter->stage_surface);
	}
}

// Récupère le dernier masque calculé par l'IA et l'envoie dans filter->mask_texture.
static void update_mask_texture(BackgroundFilter *filter)
{
	if (!filter->worker) {
		return;
	}

	int width = 0;
	int height = 0;
	if (!filter->worker->get_mask(filter->mask_buffer, width, height, filter->mask_version)) {
		return; // pas de nouveau masque
	}

	if (!filter->mask_texture || gs_texture_get_width(filter->mask_texture) != (uint32_t)width ||
	    gs_texture_get_height(filter->mask_texture) != (uint32_t)height) {
		gs_texture_destroy(filter->mask_texture);
		// GS_R8 : un seul canal de 8 bits par pixel. GS_DYNAMIC : contenu mis à jour souvent.
		filter->mask_texture = gs_texture_create(width, height, GS_R8, 1, nullptr, GS_DYNAMIC);
	}
	gs_texture_set_image(filter->mask_texture, filter->mask_buffer.data(), (uint32_t)width, false);
}

// Fait tourner l'IA et renvoie le masque adouci (ou nullptr s'il n'y a pas encore de masque).
static gs_texture_t *get_feathered_mask(BackgroundFilter *filter, gs_texture_t *source)
{
	send_frame_to_ai(filter, source);
	update_mask_texture(filter);

	if (!filter->mask_texture) {
		return nullptr;
	}
	if (filter->feather <= 0.0f) {
		return filter->mask_texture;
	}

	// Adoucissement : un petit flou sur le masque (256 x 256, donc très rapide).
	uint32_t width = gs_texture_get_width(filter->mask_texture);
	uint32_t height = gs_texture_get_height(filter->mask_texture);
	float step_pixels = filter->feather * MAX_FEATHER_RADIUS / BLUR_SAMPLES_PER_SIDE;
	gs_texture_t *feathered = blur_texture(filter, filter->mask_texture, filter->mask_horizontal_pass,
					       filter->mask_feathered, width, height, step_pixels);
	return feathered ? feathered : filter->mask_texture;
}

// Règle le seuil et la largeur de la transition dans le shader de composition.
static void set_composite_mask(BackgroundFilter *filter, gs_texture_t *mask)
{
	gs_effect_set_texture(filter->composite_param_mask, mask);
	gs_effect_set_float(filter->composite_param_threshold, filter->threshold);
	// Plus l'adoucissement est fort, plus la transition autour du seuil est large.
	gs_effect_set_float(filter->composite_param_edge_width, 0.02f + filter->feather * 0.3f);
}

// Mode Teinte : la caméra est dessinée à travers tint.effect.
static void render_tint(BackgroundFilter *filter, gs_texture_t *source, uint32_t width, uint32_t height)
{
	gs_effect_set_float(filter->tint_param_intensity, filter->intensity);
	draw_texture(filter->tint_effect, filter->tint_param_image, source, width, height);
}

// Mode Flou : toute l'image est floutée, directement à l'écran.
static void render_blur(BackgroundFilter *filter, gs_texture_t *source, uint32_t width, uint32_t height)
{
	float step_pixels = filter->intensity * MAX_BLUR_RADIUS / BLUR_SAMPLES_PER_SIDE;
	blur_texture(filter, source, filter->horizontal_pass, nullptr, width, height, step_pixels);
}

// Mode Debug : affiche le masque final (après seuil et adoucissement).
static void render_debug_mask(BackgroundFilter *filter, gs_texture_t *source, uint32_t width, uint32_t height)
{
	gs_texture_t *mask = get_feathered_mask(filter, source);
	if (!mask) {
		draw_plain(source, width, height); // pas encore de masque : la caméra telle quelle
		return;
	}
	set_composite_mask(filter, mask);
	draw_texture(filter->composite_effect, filter->composite_param_image, source, width, height, "DrawMask");
}

// Mode Flou d'arrière-plan : toi net, le fond flou.
static void render_background_blur(BackgroundFilter *filter, gs_texture_t *source, uint32_t width, uint32_t height)
{
	gs_texture_t *mask = get_feathered_mask(filter, source);
	if (!mask) {
		draw_plain(source, width, height); // pas encore de masque : la caméra telle quelle
		return;
	}

	// 1. Le fond : l'image floutée en ignorant les pixels de la personne (pas d'auréole).
	float step_pixels = filter->intensity * MAX_BLUR_RADIUS / BLUR_SAMPLES_PER_SIDE;
	gs_texture_t *blurred = blur_texture(filter, source, filter->horizontal_pass, filter->blurred_render, width,
					     height, step_pixels, mask);
	if (!blurred) {
		draw_plain(source, width, height);
		return;
	}

	// 2. Composition : caméra nette là où il y a une personne, floutée ailleurs.
	set_composite_mask(filter, mask);
	gs_effect_set_texture(filter->composite_param_background, blurred);
	draw_texture(filter->composite_effect, filter->composite_param_image, source, width, height);
}

// Appelée exactement une fois par image d'OBS (avant le dessin).
static void background_filter_video_tick(void *data, float)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);
	filter->frame_needed = true;
}

// Appelée à chaque dessin (au moins 30 ou 60 fois par seconde) pour dessiner le résultat.
static void background_filter_video_render(void *data, gs_effect_t *)
{
	BackgroundFilter *filter = static_cast<BackgroundFilter *>(data);

	// Taille de l'image de la source sur laquelle le filtre est appliqué.
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);

	// Un shader nécessaire au mode choisi est manquant (voir le journal), ou rien à dessiner :
	// on affiche l'image sans modification.
	bool uses_composite = filter->mode == MODE_DEBUG_MASK || filter->mode == MODE_BACKGROUND_BLUR;
	bool uses_blur = filter->mode != MODE_TINT;
	bool effects_ok = (filter->mode != MODE_TINT || filter->tint_effect) && (!uses_blur || filter->blur_effect) &&
			  (!uses_composite || filter->composite_effect);
	if (!effects_ok || width == 0 || height == 0) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	// 1. On capture la caméra une seule fois, puis tous les modes travaillent sur cette texture.
	if (!capture_source(filter, width, height)) {
		return;
	}
	gs_texture_t *source = gs_texrender_get_texture(filter->source_render);

	// 2. On dessine le résultat selon le mode.
	switch (filter->mode) {
	case MODE_TINT:
		render_tint(filter, source, width, height);
		break;
	case MODE_BLUR:
		render_blur(filter, source, width, height);
		break;
	case MODE_DEBUG_MASK:
		render_debug_mask(filter, source, width, height);
		break;
	case MODE_BACKGROUND_BLUR:
	default:
		render_background_blur(filter, source, width, height);
		break;
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
	info.video_tick = background_filter_video_tick;
	info.video_render = background_filter_video_render;

	// OBS copie la structure : la variable locale peut disparaître ensuite.
	obs_register_source(&info);
}
