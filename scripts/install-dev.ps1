# Installe la version compilée de Butterfly dans OBS pour la tester.
# OBS charge les plugins placés dans C:\ProgramData\obs-studio\plugins\<nom>\
#   bin\64bit\butterfly.dll  et  data\ (traductions, shaders, modèles)
# Usage : .\scripts\install-dev.ps1   (OBS doit être fermé)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$prefix = Join-Path $env:ProgramData 'obs-studio\plugins'

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw "OBS est ouvert : ferme-le avant d'installer (la DLL est verrouillée)."
}

if (-not (Test-Path "$root\build_x64\CMakeCache.txt")) {
    throw "Projet non compilé : lance d'abord .\scripts\build.ps1"
}

cmake --install "$root\build_x64" --prefix $prefix --config RelWithDebInfo
if ($LASTEXITCODE -ne 0) { throw "L'installation a échoué." }

Write-Host "Butterfly installé dans $prefix\butterfly"
Write-Host "Lance OBS, puis Aide > Fichiers journaux > Afficher le journal actuel et cherche [butterfly]."
