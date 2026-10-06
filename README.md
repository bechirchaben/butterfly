# 🦋 Butterfly

Filtres visuels temps réel pour OBS Studio : arrière-plan IA (style Google Meet), accessoires 3D sur le visage (style Snapchat), retouche du visage et effets d'ambiance.

> 🚧 En développement — voir [SPRINTS.md](SPRINTS.md).

## Compatibilité

- Windows 10/11 x64
- OBS Studio 31.1 ou plus récent (testé sur 32.2.2)

## Compiler

Prérequis : Visual Studio 2022 (« Développement Desktop en C++ » + Windows 11 SDK 10.0.22621), CMake ≥ 3.28, Git.

```powershell
.\scripts\build.ps1         # compile
.\scripts\install-dev.ps1   # installe dans OBS (OBS fermé)
```

## Documentation

- [PROJET.md](PROJET.md) — vision, architecture, décisions
- [SPRINTS.md](SPRINTS.md) — planning
- [PROCEDURE.md](PROCEDURE.md) — méthode de travail

## Licence

GPL-2.0-or-later — voir [LICENSE](LICENSE).
