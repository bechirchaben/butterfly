# 🦋 Butterfly — Planning des sprints

> Référence : [PROJET.md](PROJET.md) (vision, décisions D1–D11) · [PROCEDURE.md](PROCEDURE.md) (méthode de travail)
>
> **Hypothèse** : sprints de **2 semaines**. Durée à ajuster selon ton temps disponible : si un sprint déborde, on le prolonge ou on reporte des tâches au suivant, sans sauter la Definition of Done.

## Vue d'ensemble

| Sprint | Thème | Phase | Jalon |
|---|---|---|---|
| S0 ✅ | Environnement & squelette du plugin | 0 — Socle | Butterfly apparaît dans OBS |
| S1 ✅ | Premiers shaders & propriétés | 0 — Socle | Flou plein écran réglable |
| S2 | Intégration de l'IA (ONNX Runtime) | 0 — Socle | Masque de segmentation affiché |
| S3 | Fond flou (cœur du MVP) | 1 — Arrière-plan | Premier rendu type Google Meet |
| S4 | Modes image, source OBS, transparent | 1 — Arrière-plan | Les 4 modes fonctionnent |
| S5 | Qualité, performance & release v0.1 | 1 — Arrière-plan | 🚀 **v0.1.0 publique** |
| S6 | Tracking du visage | 2 — Visage | Landmarks stables |
| S7 | Pose 3D de la tête & rendu 3D de base | 3 — Accessoires 3D | Un cube suit la tête |
| S8 | Modèles glTF, occlusion, éclairage | 3 — Accessoires 3D | Lunettes 3D réalistes |
| S9 | Format d'effet v1 (effets utilisateurs) | 3 — Accessoires 3D | 🚀 **v0.2.0** — effets communautaires |
| S10 | Retouche & déformation du visage | 4 — Déformation | Grands yeux, lissage de peau |
| S11 | Effets d'ambiance | 5 — Ambiance | 🚀 **v0.3.0** — pack d'effets |
| S12 | Galerie, presets & v1.0 | 6 — Finition | 🚀 **v1.0.0** |

---

## S0 — Environnement & squelette du plugin

**Objectif** : avoir un plugin Butterfly qui compile et se charge dans OBS 32.2.2.

**Tâches**
- [x] Git (déjà installé)
- [x] Installer Visual Studio 2022 (charge « Développement Desktop en C++ » + Windows 11 SDK 10.0.22621) et CMake ≥ 3.28
- [x] Créer le dépôt Git local (licence GPL-2.0-or-later)
- [x] Créer le dépôt GitHub public et y pousser le code : https://github.com/bechirchaben/butterfly
- [x] Récupérer `obs-plugintemplate`, le renommer en `butterfly`, passer le code en C++
- [x] Configurer et compiler avec les presets CMake Windows x64 (aucun avertissement)
- [x] Plugin vide : message `[butterfly] plugin loaded successfully` dans les logs OBS
- [x] Filtre « passe-plat » `butterfly_background` visible dans la liste des filtres (l'image passe sans modification) — testé dans OBS 32.2.2
- [x] Scripts `scripts/build.ps1` et `scripts/install-dev.ps1` (voir PROCEDURE §5)
- [x] Fichiers de traduction `fr-FR.ini` et `en-US.ini`

**À apprendre** : bases de C++ (fichiers .h/.cpp, pointeurs, structs), CMake, cycle de vie d'un module OBS (`obs_module_load`, `obs_source_info`).

**Definition of Done** : OBS démarre sans erreur, le filtre « Butterfly – Arrière-plan IA » s'ajoute sur une caméra et l'image reste intacte.

---

## S1 — Premiers shaders & propriétés

**Objectif** : maîtriser le rendu GPU dans OBS.

**Tâches**
- [x] Premier fichier `.effect` : teinter l'image en rouge — testé
- [x] Premières propriétés du filtre (liste « Mode », curseur « Intensité »)
- [x] Flou plein écran en deux passes (horizontale + verticale) avec intensité réglable — testé
- [x] Rendu dans des textures intermédiaires (`gs_texrender`)
- [x] Première mesure du temps de rendu (Statistiques OBS) — voir « Mesures de performance » en bas de ce fichier

**À apprendre** : pipeline graphique d'OBS (`gs_*`), shaders HLSL, textures, propriétés `obs_properties_t`.

**Definition of Done** : le curseur fait varier le flou de toute l'image en direct, sans chute de FPS visible.

---

## S2 — Intégration de l'IA (ONNX Runtime)

**Objectif** : faire tourner un modèle de segmentation sur l'image de la caméra.

**Tâches**
- [ ] Téléchargement automatique d'ONNX Runtime (avec DirectML) et d'OpenCV par CMake dans `deps/`
- [ ] Choix et téléchargement du premier modèle (MediaPipe Selfie Segmentation, Apache-2.0)
- [ ] Copie de l'image GPU → CPU (`gs_stagesurface`) et réduction de taille
- [ ] Classe `Segmenter` : charge le modèle, lance l'inférence, renvoie un masque
- [ ] Thread d'inférence (`InferenceWorker`) : le thread vidéo d'OBS n'attend jamais
- [ ] Mode debug : afficher le masque en noir et blanc
- [ ] Choix GPU (DirectML) / CPU dans les propriétés, avec repli automatique

**À apprendre** : threads et mutex en C++, ONNX Runtime, format des tenseurs.

**Definition of Done** : le masque debug suit la personne en temps réel, en GPU **et** en CPU.

---

## S3 — Fond flou (cœur du MVP)

**Objectif** : le premier vrai effet « Google Meet ».

**Tâches**
- [ ] Shader de composition : `mix(fond_flou, caméra, masque)`
- [ ] Curseur « Adoucissement des bords » (feather du masque)
- [ ] Curseur « Seuil »
- [ ] Lissage temporel du masque (anti-scintillement)
- [ ] Mise à l'échelle propre du masque (basse résolution → résolution caméra)

**Definition of Done** : la personne est nette, le fond flou, les bords ne scintillent pas en mouvement normal.

---

## S4 — Modes image, source OBS, transparent

**Objectif** : compléter les 4 modes de fond (D5).

**Tâches**
- [ ] Mode **Image** : sélection d'un fichier .png/.jpg, mise à l'échelle « remplir »
- [ ] Mode **Vidéo / Source OBS** : liste des sources OBS, rendu de la source choisie en fond
- [ ] Mode **Transparent** : fond en alpha = 0
- [ ] Les propriétés s'affichent / se cachent selon le mode choisi
- [ ] Sauvegarde et rechargement des réglages avec la scène

**Definition of Done** : les 4 modes fonctionnent, se changent en direct et survivent au redémarrage d'OBS.

---

## S5 — Qualité, performance & release v0.1

**Objectif** : une version publique stable du MVP.

**Tâches**
- [ ] Préréglages Performance / Équilibré / Qualité (taille d'inférence, modèle)
- [ ] Évaluer RVM / MODNet pour le mode Qualité (GPU)
- [ ] Mesures de performance (FPS, temps d'inférence, CPU/GPU) sur ta machine
- [ ] Gestion des erreurs : modèle absent, pas de GPU, caméra débranchée
- [ ] CI GitHub Actions : compilation Windows à chaque push
- [ ] Installeur / archive zip, README (installation, captures), fichier des licences des modèles
- [ ] Publier la release **v0.1.0** sur GitHub

**Definition of Done** : quelqu'un d'autre peut installer Butterfly depuis la release et obtenir un fond flou en moins de 2 minutes.

---

## S6 — Tracking du visage

**Objectif** : détecter un visage (D7) et suivre ses points de façon stable.

**Tâches**
- [ ] Modèle de landmarks du visage (Face Mesh / Face Landmarker, ~468 points)
- [ ] **ML partagé par source** : un seul calcul landmarks + masque, réutilisé par tous les filtres Butterfly de la même caméra
- [ ] Lissage One-Euro des points
- [ ] Mode debug : dessiner les points sur l'image
- [ ] Nouveau filtre `butterfly_face_fx` (vide pour l'instant)
- [ ] Structure `FaceResult` sous forme de liste (prête pour plusieurs visages plus tard)

**Definition of Done** : les points suivent le visage sans trembler quand on reste immobile, et décrochent/reprennent proprement quand on sort du champ.

---

## S7 — Pose 3D de la tête & rendu 3D de base

**Objectif** : poser un objet 3D qui suit la tête (D4).

**Tâches**
- [ ] Calcul de la pose 6 DoF (position + rotation) depuis les landmarks
- [ ] Caméra virtuelle (perspective cohérente avec la webcam)
- [ ] Rendu d'un cube 3D avec depth buffer dans une texture, composé sur l'image
- [ ] Liste des **points d'ancrage nommés** (`nose_bridge`, `head_top`, `left_ear`…)

**Definition of Done** : un cube reste « collé » au front quand on tourne, penche ou avance la tête.

---

## S8 — Modèles glTF, occlusion, éclairage

**Objectif** : de vrais accessoires 3D crédibles.

**Tâches**
- [ ] Chargement `.glb` avec `cgltf` (mesh, matériaux, textures)
- [ ] Occluder de tête invisible → les branches des lunettes passent derrière la tête
- [ ] Éclairage simple (couleur de base + normal map + lumière ambiante estimée)
- [ ] 2 accessoires de test : lunettes, chapeau

**Definition of Done** : des lunettes 3D tiennent sur le nez, les branches disparaissent derrière la tête de profil.

---

## S9 — Format d'effet v1 (effets utilisateurs)

**Objectif** : les utilisateurs créent leurs propres effets (D11).

**Tâches**
- [ ] Lecture de `effect.json` (`format_version: 1`) et validation avec messages clairs
- [ ] Calques `model3d`, `sprite2d`, `background`
- [ ] Scan des dossiers d'effets (intégrés + `%APPDATA%/obs-studio/plugin_config/butterfly/effects/`)
- [ ] Support des archives `.bfx` (zip)
- [ ] Sécurité : chemins `../` interdits, tailles max
- [ ] Rechargement à chaud quand `effect.json` change
- [ ] Sélecteur d'effet dans les propriétés du filtre
- [ ] Guide `docs/creer-un-effet.md` + 3 effets exemples commentés
- [ ] Release **v0.2.0**

**Definition of Done** : un utilisateur qui suit le guide crée un effet lunettes sans toucher au C++, et le voit dans OBS sans redémarrer.

---

## S10 — Retouche & déformation du visage

**Tâches**
- [ ] Calque `face_warp` : grands yeux, affinage du visage, petit menton (paramétrables)
- [ ] Lissage de peau (flou bilatéral limité au masque du visage)
- [ ] Calque `face_texture` : maquillage / peinture plaquée sur le mesh du visage
- [ ] Nouveau filtre `butterfly_face_retouch`

**Definition of Done** : les déformations restent naturelles en mouvement, sans artefacts sur le fond.

---

## S11 — Effets d'ambiance

**Tâches**
- [ ] Calque `fullscreen_shader` (shaders personnalisés des utilisateurs)
- [ ] Effets intégrés : LUT (.cube), grain, VHS, glitch
- [ ] Calque `particles` : paillettes, neige, pluie
- [ ] Nouveau filtre `butterfly_ambience`
- [ ] Release **v0.3.0**

**Definition of Done** : chaque effet d'ambiance est réglable et combinable avec les autres filtres Butterfly.

---

## S12 — Galerie, presets & v1.0

**Tâches**
- [ ] Dock Qt « Butterfly » : galerie d'effets avec vignettes `preview.png`
- [ ] Presets (combinaisons de filtres sauvegardées)
- [ ] Raccourcis clavier OBS pour activer / changer d'effet
- [ ] Passe de performance globale
- [ ] Documentation complète, vidéo de démo
- [ ] Release **v1.0.0**

**Definition of Done** : un streamer installe Butterfly, choisit un effet dans la galerie et l'applique en direct en un clic.

---

## Backlog (après v1.0, non planifié)

- Portage macOS / Linux (D1)
- Plusieurs visages (D7)
- Galerie d'effets en ligne ❓
- Animations scriptables en Lua ❓
- Éditeur visuel d'effets ❓
- Face swap, effet vieillissement
- Support CUDA en option

---

## Mesures de performance

Machine de développement : AMD Radeon(TM) Graphics (GPU intégré), CPU 8 cœurs / 16 threads, OBS 32.2.2 (D3D11), canevas 2880×1620 → sortie 1920×1080 à 60 FPS, webcam 1280×720 à 30 FPS.

| Date | Sprint | Configuration du filtre | Temps moyen de rendu | Images manquées (rendu) | CPU OBS |
|---|---|---|---|---|---|
| 2026-10-06 | S1 | Flou plein écran 100 % (2 × 33 échantillons) | 0,5 ms | 3 / 13 695 (0,0 %) | 1,8 % |
