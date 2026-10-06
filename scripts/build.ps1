# Compile Butterfly (configuration RelWithDebInfo).
# Usage : .\scripts\build.ps1            -> configure si besoin, puis compile
#         .\scripts\build.ps1 -Clean     -> supprime build_x64 et recompile tout

param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Clean -and (Test-Path "$root\build_x64")) {
    Write-Host "Suppression de build_x64..."
    Remove-Item -Recurse -Force "$root\build_x64"
}

# La configuration télécharge les sources d'OBS et ses dépendances (une seule fois, plusieurs minutes).
if (-not (Test-Path "$root\build_x64\CMakeCache.txt")) {
    Write-Host "Configuration CMake..."
    cmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw "La configuration CMake a échoué." }
}

Write-Host "Compilation..."
cmake --build --preset windows-x64
if ($LASTEXITCODE -ne 0) { throw "La compilation a échoué." }

Write-Host "OK : build_x64\rundir\RelWithDebInfo\butterfly.dll"
