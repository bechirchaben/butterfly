# Met en forme le code C++ de Butterfly selon les règles d'OBS (.clang-format).
# La CI GitHub refuse le code mal formaté : lance ce script avant chaque commit.
# Usage : .\scripts\format.ps1

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$clangFormat = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-format.exe'
if (-not (Test-Path $clangFormat)) {
    throw "clang-format introuvable : installe le composant « Outils C++ Clang » dans Visual Studio Installer."
}

$files = Get-ChildItem -Path "$root\src" -Recurse -Include *.cpp, *.h, *.c
& $clangFormat -i $files.FullName
if ($LASTEXITCODE -ne 0) { throw "clang-format a échoué." }

Write-Host "$($files.Count) fichiers mis en forme."
