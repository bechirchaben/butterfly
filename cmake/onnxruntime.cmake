# Butterfly - téléchargement d'ONNX Runtime (avec DirectML) depuis NuGet.
#
# Crée la cible importée "Butterfly::onnxruntime" et la liste BUTTERFLY_ONNX_DLLS
# (les .dll à installer à côté de butterfly.dll).
#
# Les paquets sont téléchargés une seule fois dans .deps/ et vérifiés par leur empreinte SHA-256 :
# si le fichier téléchargé n'est pas exactement celui attendu, la configuration s'arrête.

include_guard(GLOBAL)

set(_ort_version "1.24.4")
set(_ort_hash "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681")
set(_dml_version "1.15.4")
set(_dml_hash "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9")

set(_deps_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")

# Télécharge un paquet NuGet (si absent) puis l'extrait (si pas encore extrait).
function(_butterfly_fetch_nuget name version hash out_dir)
  set(_archive "${_deps_dir}/${name}.${version}.nupkg")
  set(_dir "${_deps_dir}/${name}.${version}")

  if(NOT EXISTS "${_archive}")
    message(STATUS "Butterfly: téléchargement de ${name} ${version}...")
    file(
      DOWNLOAD "https://api.nuget.org/v3-flatcontainer/${name}/${version}/${name}.${version}.nupkg" "${_archive}"
      EXPECTED_HASH SHA256=${hash}
      SHOW_PROGRESS
    )
  endif()

  if(NOT EXISTS "${_dir}/.extracted")
    file(REMOVE_RECURSE "${_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_dir}")
    file(TOUCH "${_dir}/.extracted")
  endif()

  set(${out_dir} "${_dir}" PARENT_SCOPE)
endfunction()

_butterfly_fetch_nuget(microsoft.ml.onnxruntime.directml ${_ort_version} ${_ort_hash} _ort_dir)
_butterfly_fetch_nuget(microsoft.ai.directml ${_dml_version} ${_dml_hash} _dml_dir)

add_library(Butterfly::onnxruntime SHARED IMPORTED)
set_target_properties(
  Butterfly::onnxruntime
  PROPERTIES
    IMPORTED_LOCATION "${_ort_dir}/runtimes/win-x64/native/onnxruntime.dll"
    IMPORTED_IMPLIB "${_ort_dir}/runtimes/win-x64/native/onnxruntime.lib"
    # dml_provider_factory.h (ONNX Runtime) inclut DirectML.h (paquet DirectML).
    INTERFACE_INCLUDE_DIRECTORIES "${_ort_dir}/build/native/include;${_dml_dir}/include"
    # L'API C++ d'ONNX Runtime est initialisée à la main (Ort::InitApi) après avoir chargé
    # NOTRE onnxruntime.dll : voir src/ml/onnx-loader.cpp.
    INTERFACE_COMPILE_DEFINITIONS "ORT_API_MANUAL_INIT"
)

set(
  BUTTERFLY_ONNX_DLLS
  "${_ort_dir}/runtimes/win-x64/native/onnxruntime.dll"
  "${_ort_dir}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
  "${_dml_dir}/bin/x64-win/DirectML.dll"
)

set(
  BUTTERFLY_ONNX_LICENSES
  "${_ort_dir}/LICENSE"
  "${_ort_dir}/ThirdPartyNotices.txt"
  "${_dml_dir}/LICENSE.txt"
  "${_dml_dir}/ThirdPartyNotices.txt"
)

# Pour que les licences installées aient des noms distincts.
set(
  BUTTERFLY_ONNX_LICENSE_NAMES
  "onnxruntime-LICENSE.txt"
  "onnxruntime-ThirdPartyNotices.txt"
  "DirectML-LICENSE.txt"
  "DirectML-ThirdPartyNotices.txt"
)
