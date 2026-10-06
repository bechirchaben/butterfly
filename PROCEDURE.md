# 🦋 Butterfly — Procédure de travail

> Référence : [PROJET.md](PROJET.md) (vision, décisions) · [SPRINTS.md](SPRINTS.md) (planning)

## 1. Rôles

| Qui | Rôle |
|---|---|
| **Toi** | Porteur du projet (tu décides des priorités et valides les choix) et développeur (tu compiles, testes dans OBS, valides chaque étape) |
| **Claude** | Assistant : pose les questions de clarification, propose l'architecture, écrit et **explique** le code, aide à déboguer, tient la documentation à jour |

## 2. Déroulement d'un sprint

1. **Planification** (début de sprint)
   - On relit la section du sprint dans [SPRINTS.md](SPRINTS.md).
   - Claude pose les questions qui bloquent encore, puis on ajuste la liste des tâches si besoin.
2. **Développement** : par petites étapes (voir §3).
3. **Revue** (fin de sprint)
   - On vérifie la Definition of Done du sprint dans OBS.
   - Les tâches cochées `[x]` dans SPRINTS.md, celles qui ne sont pas finies passent au sprint suivant.
4. **Rétrospective** (5 minutes)
   - Ce qui a bien marché, ce qui a bloqué, ce qu'on change.
   - Ajout d'une ligne au journal (PROJET.md §11).

## 3. Cycle d'une étape (boucle courte)

Chaque tâche est découpée en étapes **testables dans OBS en moins d'une heure** :

```
Expliquer → Coder → Compiler → Tester dans OBS → Commit Git
    ▲                                  │
    └────────── si ça ne marche pas ───┘
```

1. **Expliquer** : Claude explique ce qu'on va faire et pourquoi (D8 : débutant en C++).
2. **Coder** : un changement à la fois, code simple et commenté.
3. **Compiler** : voir §5.
4. **Tester** : vérifier dans OBS et lire les logs.
5. **Commit** : un commit par étape qui fonctionne.

Règle : on ne passe **jamais** à l'étape suivante tant que la précédente ne marche pas.

## 4. Conventions

### Git
- Branche `main` : toujours compilable.
- Une branche par tâche : `feature/s0-squelette`, `fix/masque-scintille`…
- Messages de commit courts, au présent : `Ajoute le filtre passe-plat`, `Corrige le flou en mode CPU`.
- Fusion dans `main` quand l'étape est testée.
- Dépôt public : https://github.com/bechirchaben/butterfly
- **Confidentialité** : seul le nom de l'auteur est public. Les commits utilisent l'adresse GitHub *noreply* (`git config user.email` local au projet) et aucune adresse e-mail ne doit apparaître dans les fichiers.

### Code C++
- C++17, style proche de celui d'OBS : `snake_case` pour les fonctions et variables, `PascalCase` pour les classes.
- Préfixe `butterfly_` pour les identifiants OBS (filtres, réglages).
- Logs : `blog(LOG_INFO, "[butterfly] ...")`.
- Un fichier = une responsabilité ; pas d'abstraction tant qu'elle n'est pas nécessaire.
- Commentaires en français, qui expliquent le **pourquoi**.

### Textes affichés
- Jamais de texte en dur : tout passe par `data/locale/fr-FR.ini` et `en-US.ini` (`obs_module_text`).

## 5. Compiler et tester

**Prérequis** : Visual Studio 2022 (charge « Développement Desktop en C++ » + composant **Windows 11 SDK 10.0.22621**), CMake ≥ 3.28, Git.

```powershell
# Compiler (la 1re fois, CMake télécharge les sources d'OBS : plusieurs minutes)
.\scripts\build.ps1

# Tout recompiler depuis zéro
.\scripts\build.ps1 -Clean

# Installer dans OBS (OBS doit être fermé)
.\scripts\install-dev.ps1
```

**Où est installé le plugin** : `C:\ProgramData\obs-studio\plugins\butterfly\` (`bin\64bit\butterfly.dll` + `data\`). Pas besoin de toucher au dossier `Program Files` d'OBS. Pour désinstaller : supprimer ce dossier.

**Lire les logs** : `%APPDATA%\obs-studio\logs\` (le fichier le plus récent), ou dans OBS : Aide → Fichiers journaux → Afficher le journal actuel. Chercher `[butterfly]`.

**Checklist de test manuel** (à chaque fin de sprint) :
- [ ] OBS démarre sans erreur ni avertissement `[butterfly]`
- [ ] Ajouter / supprimer le filtre sur une caméra
- [ ] Changer chaque réglage en direct
- [ ] Fermer / rouvrir OBS → les réglages sont conservés
- [ ] Vérifier FPS et utilisation CPU/GPU (Statistiques OBS)
- [ ] Tester en mode GPU **et** CPU

## 6. Definition of Done (toutes tâches)

Une tâche est terminée quand :
- [ ] elle compile sans nouvel avertissement ;
- [ ] elle a été testée dans OBS 32.2.2 ;
- [ ] les textes sont traduits (fr/en) ;
- [ ] elle n'a pas fait baisser les performances de façon visible ;
- [ ] elle est commitée sur `main` ;
- [ ] la documentation est à jour si besoin.

## 7. Gestion des décisions et questions

- **Nouvelle décision** → ligne `Dx` dans PROJET.md §7, avec ses conséquences.
- **Nouvelle question** → PROJET.md §6 (❓), retirée une fois tranchée.
- **Chaque échange important** → ligne dans le journal PROJET.md §11.
- **Changement de planning** → mise à jour de SPRINTS.md.
- **Bugs et idées** → issues GitHub (dès le S0), avec les étiquettes `bug`, `idée`, `sprint-X`.

## 8. Versions et releases

- Versionnage sémantique : `MAJEUR.MINEUR.CORRECTIF` (`0.1.0`, `0.2.0`…, `1.0.0`).
- Releases prévues : v0.1.0 (S5), v0.2.0 (S9), v0.3.0 (S11), v1.0.0 (S12).
- Chaque release contient : zip/installeur, notes de version, licences des modèles ML.

## 9. Licences

- Butterfly : **GPL-2.0-or-later** (D3).
- Avant d'ajouter un modèle ou une bibliothèque : vérifier sa licence et l'ajouter dans `THIRD_PARTY_LICENSES.md`.
- Effets communautaires : chaque `effect.json` déclare sa licence (champ `license`).
