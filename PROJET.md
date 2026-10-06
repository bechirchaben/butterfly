# 🦋 Butterfly — Plugin OBS de filtres visuels temps réel (style Snapchat / Google Meet)

> Document de cadrage. Statut : **cadrage validé — prêt pour le Sprint 0**.
> Les points marqués ❓ sont à valider.
>
> Documents liés : [SPRINTS.md](SPRINTS.md) (planning) · [PROCEDURE.md](PROCEDURE.md) (méthode de travail)

## 1. Idée

Un plugin OBS Studio qui ajoute des **filtres vidéo** applicables à une source caméra (ou n'importe quelle source vidéo), répartis en quatre familles :

| Famille | Description | Exemples |
|---|---|---|
| **Filtres visuels** (style Snapchat) | Effets et overlays qui suivent le visage | Lunettes, chapeaux, oreilles de chat, masques, maquillage virtuel, paillettes |
| **Modification du visage** | Déformation / retouche du visage détecté | Grands yeux, petit menton, lissage de peau, affinage du visage, "face swap", effet vieillissement |
| **Environnement** | Effets appliqués à toute l'image | LUT / étalonnage, grain, VHS, glitch, pluie, neige, particules, lumière volumétrique |
| **Arrière-plan seul** (style Google Meet) | Segmentation personne / fond | Flou d'arrière-plan, remplacement par image / vidéo, suppression (fond transparent), fond animé |

## 2. Architecture technique (proposition)

```
Source caméra OBS
      │
      ▼
┌───────────────────────────────────────────────┐
│  Filtre OBS (C++, obs_source_info FILTER)      │
│                                               │
│  1. Récupération de la frame (GPU → CPU si ML) │
│  2. Inférence ML (thread séparé, asynchrone)   │
│     ├─ Détection visage + 468 landmarks        │
│     └─ Segmentation personne (masque alpha)    │
│  3. Rendu GPU (shaders .effect HLSL/GLSL)      │
│     ├─ Warp du visage (mesh deformation)       │
│     ├─ Overlays 2D/3D ancrés sur landmarks     │
│     ├─ Composition fond via masque             │
│     └─ Effets plein écran (LUT, glitch…)       │
└───────────────────────────────────────────────┘
      │
      ▼
   Sortie OBS
```

### Stack envisagée
- **Langage** : C++17, API `libobs` (filtres de type `OBS_SOURCE_TYPE_FILTER`).
- **Build** : CMake, basé sur le template officiel `obs-plugintemplate`.
- **ML / Vision** :
  - **ONNX Runtime** (CPU + DirectML sur Windows / CUDA en option) pour exécuter les modèles.
  - Modèles candidats : MediaPipe Face Mesh (landmarks), MediaPipe Selfie Segmentation / RVM (Robust Video Matting) / MODNet pour le fond.
  - OpenCV pour le pré/post-traitement.
- **Rendu** : shaders OBS (`.effect`), tout le compositing reste sur le GPU.
- **UI** : propriétés natives OBS (`obs_properties_t`) au départ ; éventuellement un dock Qt plus tard pour une galerie d'effets.

### Principes
- **Inférence asynchrone** : le ML tourne dans un thread dédié, le rendu utilise le dernier résultat disponible → pas de blocage du thread vidéo d'OBS.
- **Lissage temporel** des landmarks et du masque (filtre One-Euro / EMA) pour éviter le tremblement.
- **Partage des résultats ML** : un seul calcul de landmarks/masque par source, partagé entre plusieurs filtres empilés.
- **Effets data-driven** : chaque effet "Snapchat" décrit dans un fichier (JSON + textures) pour pouvoir en ajouter sans recompiler.

## 3. Découpage en filtres OBS

Plutôt qu'un filtre monolithique, plusieurs filtres combinables dans la chaîne OBS :

1. `Arrière-plan IA` — flou / image / vidéo / transparent / couleur.
2. `Masques & accessoires` — overlays ancrés sur le visage.
3. `Retouche visage` — déformations et beauté.
4. `Effets d'ambiance` — effets plein écran.

## 4. Feuille de route

> Le détail sprint par sprint est dans [SPRINTS.md](SPRINTS.md).

| Phase | Contenu | Livrable |
|---|---|---|
| **0 — Socle** | Template plugin, build Windows, filtre "passe-plat", chargement ONNX Runtime | Plugin qui s'installe et apparaît dans OBS |
| **1 — Arrière-plan** | Segmentation, flou, image, vidéo / source OBS, transparence | MVP type Google Meet |
| **2 — Visage : tracking** | Face mesh + lissage, affichage debug des landmarks | Tracking stable |
| **3 — Accessoires 3D** | Pose 6 DoF, chargement glTF, rendu avec depth + occlusion, format d'effet JSON | Premiers filtres "Snapchat" 3D |
| **4 — Déformation** | Warp du visage, lissage de peau | Retouche visage |
| **5 — Ambiance** | LUT, glitch, particules, météo | Pack d'effets |
| **6 — Finition** | Galerie d'effets (dock Qt), presets, raccourcis clavier, installeur | Version publiable |

## 5. Risques identifiés
- **Performance** : ML + stream + jeu simultanés → besoin du GPU (DirectML) et de modèles légers.
- **Licences des modèles** : vérifier chaque modèle (Apache 2.0 pour MediaPipe, GPL-3 pour RVM…) vs la licence du plugin (GPL-2.0-or-later, comme OBS).
- **Multi-visages** : hors périmètre pour l'instant (D7), mais structures prévues pour.
- **Rendu 3D** : chantier le plus lourd (pose, glTF, occlusion) → découpé sur 3 sprints.
- **Courbe d'apprentissage C++** : sprints du début volontairement plus légers et pédagogiques.
- **Licence DirectML** : `DirectML.dll` est un binaire Microsoft non open source, redistribuable selon sa licence. **Vérifier sa compatibilité avec la GPL avant la release v0.1 (S5)** ; alternative : backend CPU seul ou WinML.
- **DLL système en conflit** : Windows fournit un ancien `onnxruntime.dll` dans System32 → Butterfly charge le sien par chemin complet (`/DELAYLOAD` + `src/ml/onnx-loader.cpp`).
- **Format d'effet public** : une fois publié, difficile à casser → versionner (`format_version`) dès la v1.
- **Existant** : `obs-backgroundremoval` (royshil) couvre déjà la partie fond → s'en inspirer, se différencier sur les effets visage.

## 6. Questions ouvertes ❓
- Distribution des effets communautaires : simple dossier à copier, ou galerie en ligne intégrée ?
- Effets scriptables (logique/animations en Lua) ou uniquement déclaratifs (JSON) ?
- Éditeur visuel d'effets : nécessaire, ou l'édition du JSON + aperçu en direct dans OBS suffit ?

## 7. Décisions prises

| # | Sujet | Décision | Conséquences |
|---|---|---|---|
| D1 | Plateformes | **Windows d'abord**, macOS/Linux plus tard | DirectML comme backend GPU principal ; code ML/rendu isolé derrière des interfaces pour faciliter le portage |
| D2 | MVP | **Arrière-plan (style Google Meet)** en premier | Phase 1 de la roadmap = livrable public |
| D3 | Licence | **Open source** | Licence GPL-2.0-or-later (comme OBS) ; modèles Apache-2.0 / MIT / GPL-3 acceptables (GPL-3 compatible avec GPL-2-or-later) |
| D4 | Accessoires | **3D dès le départ** | Voir §8 |
| D5 | Options du MVP fond | **Flou, image, vidéo / source OBS, transparent** — les 4 | Le masque de segmentation est commun ; seul le mode de composition change (1 shader, 4 techniques) |
| D6 | Matériel | **GPU conseillé, CPU possible** | DirectML par défaut, repli automatique sur CPU avec un modèle plus léger / résolution d'inférence réduite ; réglage "Qualité / Performance" dans l'UI |
| D7 | Visages | **1 visage** pour l'instant | Les structures de données gardent une liste de visages pour pouvoir passer à N plus tard |
| D8 | Approche pédagogique | Développeur **débutant en C++** | Code simple et commenté, étapes courtes testables dans OBS à chaque fois, pas d'abstraction prématurée |
| D9 | Nom | **Butterfly** | Identifiant technique `butterfly` (préfixe des filtres : `butterfly_background`, `butterfly_face_fx`…), dossier d'installation `obs-plugins/64bit/butterfly.dll` |
| D10 | Version OBS | Tests sur **32.2.2** (version de dev) | Compilation contre les sources OBS **31.1.1** fournies par le template officiel → Butterfly marche sur OBS 31.1+ et 32.x. Qt 6, Direct3D 11 sur Windows |
| D11 | Effets utilisateurs | **Oui, format ouvert** | Voir §10 ; le format d'effet devient une API publique → à versionner dès le début |

## 8. Accessoires 3D — implications

Le choix 3D change la phase 3 de la roadmap :
- **Pose de la tête (6 DoF)** : calculée à partir des landmarks (Face Mesh + `solvePnP`, ou matrice de transformation fournie par MediaPipe Face Landmarker).
- **Chargement de modèles** : format **glTF 2.0 / .glb** via `cgltf` (header-only, MIT).
- **Rendu** : via l'API graphique d'OBS (`gs_*`) avec un depth buffer dédié, rendu dans une texture puis composée sur la frame.
- **Occlusion** : un "occluder" invisible (mesh du visage/tête) écrit dans le depth buffer pour que les branches de lunettes passent *derrière* la tête.
- **Éclairage** : PBR simplifié (base color + normal map + lumière d'ambiance estimée depuis l'image) pour rester léger.
- **Format d'effet** : un dossier par effet = `effect.json` (ancrage, échelle, offsets, animations) + `.glb` + textures.

## 9. MVP détaillé — Filtre « Arrière-plan IA »

### 9.1 Ce que voit l'utilisateur (propriétés du filtre dans OBS)

| Réglage | Type | Valeurs |
|---|---|---|
| Mode | Liste | Flou · Image · Vidéo / Source OBS · Transparent |
| Intensité du flou | Curseur | 0 – 100 (mode Flou) |
| Image de fond | Fichier | .png / .jpg (mode Image) |
| Source de fond | Liste des sources OBS | n'importe quelle source : vidéo, navigateur, scène… (mode Vidéo) |
| Adoucissement des bords | Curseur | 0 – 100 (feather du masque) |
| Seuil | Curseur | sensibilité de la détection personne |
| Qualité | Liste | Performance · Équilibré · Qualité (taille d'inférence / modèle) |
| Matériel | Liste | Auto · GPU (DirectML) · CPU |

### 9.2 Pipeline d'une frame

1. OBS rend la source caméra dans une texture.
2. La texture est copiée en mémoire CPU, réduite (ex. 256×256) → envoyée au **thread d'inférence**.
3. Le modèle produit un **masque** (0 = fond, 1 = personne).
4. Le masque est lissé dans le temps (évite le scintillement) et renvoyé au GPU.
5. Un shader compose : `résultat = mix(fond_traité, caméra, masque)` où `fond_traité` = caméra floutée / image / autre source / transparent.

Le thread vidéo d'OBS **n'attend jamais** l'IA : il utilise toujours le dernier masque disponible.

### 9.3 Modèle de segmentation (à évaluer)

| Modèle | Licence | Qualité | Vitesse | Remarque |
|---|---|---|---|---|
| MediaPipe Selfie Segmentation | Apache-2.0 | ★★☆ | ★★★ | Très léger, bon candidat CPU |
| RVM (Robust Video Matting) | GPL-3.0 | ★★★ | ★★☆ | Gère bien les cheveux, mémoire temporelle intégrée |
| MODNet | Apache-2.0 | ★★★ | ★★☆ | Bon compromis |

Proposition : **MediaPipe en mode Performance/CPU**, **RVM en mode Qualité/GPU**.

### 9.4 Structure du dépôt (prévue)

```
plugin-obs/
├── CMakeLists.txt
├── PROJET.md
├── data/
│   ├── effects/            # shaders .effect (composition, flou…)
│   ├── models/             # modèles .onnx
│   └── locale/             # traductions (fr-FR.ini, en-US.ini)
├── src/
│   ├── plugin-main.cpp     # point d'entrée, enregistrement des filtres
│   ├── background-filter.cpp/.h
│   ├── ml/
│   │   ├── inference-worker.cpp/.h   # thread d'inférence
│   │   └── segmenter.cpp/.h          # wrapper ONNX Runtime
│   └── utils/
└── deps/                   # ONNX Runtime, OpenCV (téléchargés par CMake)
```

### 9.5 Étapes de la phase 0 + 1 (petits pas testables)

1. Installer l'environnement : Visual Studio 2022, CMake, Git, sources OBS / template.
2. Plugin vide qui se charge dans OBS (message dans les logs).
3. Filtre « passe-plat » visible dans la liste des filtres.
4. Filtre qui teinte l'image en rouge (premier shader).
5. Flou plein écran (shader de flou).
6. Intégration ONNX Runtime + chargement du modèle.
7. Affichage du masque brut en noir et blanc (debug).
8. Composition : flou du fond seulement → **premier résultat type Meet** 🎉
9. Modes Image, Source OBS, Transparent.
10. Lissage, réglages qualité, repli CPU, packaging.

## 10. Effets créés par les utilisateurs

### 10.1 Principe
Un effet Butterfly = **un dossier** (ou une archive `.bfx`, qui est un simple zip renommé) contenant un fichier `effect.json` et ses ressources. Butterfly scanne deux emplacements :

- `data/effects-builtin/` — effets livrés avec le plugin
- `%APPDATA%/obs-studio/plugin_config/butterfly/effects/` — effets de l'utilisateur

Pas besoin de savoir coder en C++ pour créer un effet.

### 10.2 Exemple de structure

```
lunettes-coeur/
├── effect.json
├── preview.png          # vignette affichée dans la galerie
├── models/
│   └── lunettes.glb     # modèle 3D
└── textures/
    └── reflet.png
```

### 10.3 Exemple de `effect.json` (brouillon)

```json
{
  "format_version": 1,
  "id": "lunettes-coeur",
  "name": "Lunettes cœur",
  "author": "Pseudo",
  "license": "CC-BY-4.0",
  "category": "face_accessory",
  "layers": [
    {
      "type": "model3d",
      "file": "models/lunettes.glb",
      "anchor": "nose_bridge",
      "offset": [0.0, 0.01, 0.0],
      "rotation": [0, 0, 0],
      "scale": 1.0,
      "occlusion": "head"
    },
    {
      "type": "particles",
      "texture": "textures/reflet.png",
      "anchor": "head_top",
      "rate": 5
    }
  ]
}
```

### 10.4 Types de calques envisagés

| Type | Rôle |
|---|---|
| `model3d` | Modèle glTF ancré sur un point du visage |
| `sprite2d` | Image plate ancrée sur le visage |
| `face_texture` | Texture plaquée sur le mesh du visage (maquillage, peinture, masque) |
| `face_warp` | Déformation (grands yeux, affinage…) avec paramètres |
| `background` | Remplacement / flou du fond |
| `fullscreen_shader` | Shader `.effect` personnalisé plein écran (LUT, glitch…) |
| `particles` | Système de particules simple |

### 10.5 Points d'ancrage
Liste nommée et stable (ex. `nose_tip`, `nose_bridge`, `left_eye`, `right_eye`, `mouth`, `chin`, `forehead`, `head_top`, `left_ear`, `right_ear`) mappée vers les indices du Face Mesh. Les créateurs utilisent les noms, jamais les indices bruts.

### 10.6 Règles
- **`format_version`** obligatoire : Butterfly refuse proprement un format trop récent au lieu de planter.
- **Validation** au chargement (champs manquants, fichiers absents, tailles max) avec message clair dans les logs OBS.
- **Sécurité** : les effets sont des données, pas du code exécutable. Les shaders personnalisés passent par le compilateur d'effets d'OBS ; pas de chemins hors du dossier de l'effet (`../` interdit).
- **Rechargement à chaud** : modifier `effect.json` met à jour l'aperçu dans OBS sans redémarrer → c'est l'"éditeur" minimal pour les créateurs.
- **Documentation** : un guide `docs/creer-un-effet.md` + 2-3 effets exemples commentés.

## 11. Journal des échanges

| Date | Sujet | Résumé |
|---|---|---|
| 2026-10-06 | Idée initiale | Plugin OBS avec filtres type Snapchat, modification du visage, effets d'environnement, arrière-plan type Google Meet. Création de ce document. |
| 2026-10-06 | Questions 1 | Windows d'abord (D1) · MVP = arrière-plan (D2) · Open source (D3) · Accessoires 3D dès le départ (D4). Ajout §8. |
| 2026-10-06 | Questions 2 | 4 modes de fond dans le MVP (D5) · GPU conseillé / CPU possible (D6) · 1 visage (D7) · Débutant C++ → approche pédagogique (D8). Ajout §9. |
| 2026-10-06 | Questions 3 | Nom **Butterfly** (D9) · OBS **32.2.2** (D10) · Effets créés par les utilisateurs, format ouvert (D11). Ajout §10. |
| 2026-10-06 | Planification | Découpage en 13 sprints ([SPRINTS.md](SPRINTS.md)) et méthode de travail ([PROCEDURE.md](PROCEDURE.md)). |
| 2026-10-06 | Sprint 0 — démarrage | Template officiel `obs-plugintemplate` intégré et renommé en Butterfly ; code passé en C++ ; filtre passe-plat `butterfly_background` ; traductions fr/en ; scripts `build.ps1` / `install-dev.ps1`. Précision D10 : compilation contre OBS 31.1.1. |
| 2026-10-06 | Sprint 0 — test | Première compilation réussie (0 avertissement). Butterfly chargé dans OBS 32.2.2, filtre passe-plat ajouté sur la webcam : OK. Reste : envoi sur GitHub. |
| 2026-10-06 | Sprint 0 — clôture | Seul le nom de l'auteur est public : e-mail retiré de `buildspec.json` et commits signés avec l'adresse GitHub *noreply* (historique local réécrit avant le premier envoi). Code publié sur https://github.com/bechirchaben/butterfly. **Sprint 0 terminé.** |
| 2026-10-06 | Sprint 1 — clôture | Teinte rouge et flou plein écran en deux passes, réglables (Mode + Intensité). Référence perf : 0,5 ms / image à 60 FPS. Machine de dev = **GPU AMD intégré** → confirme DirectML (D6) et impose des modèles IA légers. Rappel : le flou de *l'arrière-plan seul* arrive au S3, une fois le masque IA (S2) disponible. **Sprint 1 terminé.** |
| 2026-10-06 | Sprint 2 — étape 1 | ONNX Runtime 1.24.4 + DirectML 1.15.4 intégrés (dernière version d'ORT avec DirectML). Modèle `selfie_segmentation.onnx` (Apache-2.0). IA chargée sur le GPU AMD : 3,82 ms / analyse. OpenCV abandonné (réduction d'image sur GPU). Risques ajoutés : licence DirectML, DLL System32. |
| 2026-10-06 | Sprint 2 — étapes 2 à 4 | Thread d'IA (`InferenceWorker`, mutex), réduction 256×256 sur GPU + copie vers CPU, mode Debug (masque) : **silhouette correcte**. Choix GPU/CPU testé. Constat : sur ce GPU AMD intégré, CPU (5,2 ms) ≈ GPU (5,4 ms) pour ce petit modèle ; on garde le GPU par défaut pour laisser le CPU à l'encodage. Correctif : l'IA analysait jusqu'à 120 images/s (filtre dessiné plusieurs fois par image) → limitée à 1 par image d'OBS. |
| 2026-10-06 | Sprint 2 — clôture | Correctif validé : 60 analyses/s au lieu de 120. Rendu 2,6 ms / image (copie GPU→CPU synchrone), CPU OBS 3 %. Optimisations reportées au S5 : copie sans attente, analyse au rythme de la caméra. **Sprint 2 terminé.** |
| 2026-10-06 | Sprint 3 — flou d'arrière-plan | Nouveau mode « Flou d'arrière-plan » (par défaut) : **premier rendu type Google Meet validé**. Réglages Seuil, Adoucissement, Lissage dans le temps ; mode Debug = masque final. Défaut constaté : auréole sombre autour des cheveux → corrigé par un **flou masqué** (les pixels de la personne ne comptent pas dans le flou du fond). |
