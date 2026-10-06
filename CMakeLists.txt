cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/background-filter.cpp
    src/background-filter.h
    src/ml/onnx-loader.cpp
    src/ml/onnx-loader.h
    src/ml/segmenter.cpp
    src/ml/segmenter.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

# --- IA : ONNX Runtime + DirectML (Windows) ---
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/onnxruntime.cmake")
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Butterfly::onnxruntime)

# Chargement différé : onnxruntime.dll n'est chargée qu'à sa première utilisation.
# Indispensable car Windows contient un ancien onnxruntime.dll dans System32 :
# Butterfly charge d'abord SA version par son chemin complet (src/ml/onnx-loader.cpp).
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE /DELAYLOAD:onnxruntime.dll)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE delayimp)

# Les DLL d'ONNX Runtime et DirectML sont installées à côté de butterfly.dll...
install(FILES ${BUTTERFLY_ONNX_DLLS} DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")

# ... avec leurs licences.
foreach(_license _license_name IN ZIP_LISTS BUTTERFLY_ONNX_LICENSES BUTTERFLY_ONNX_LICENSE_NAMES)
  install(FILES "${_license}" DESTINATION "${CMAKE_PROJECT_NAME}/data/licenses" RENAME "${_license_name}")
endforeach()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Copie aussi les DLL dans build_x64/rundir/<config>/ après chaque compilation
# (après set_target_properties_plugin, qui crée ce dossier).
add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME}
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${BUTTERFLY_ONNX_DLLS} "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
  COMMENT "Copy ONNX Runtime DLLs to rundir"
  VERBATIM
)
