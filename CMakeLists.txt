cmake_minimum_required(VERSION 3.25)

project(SharkCut
    VERSION 0.1.6
    DESCRIPTION "Editor de video nativo para Windows"
    LANGUAGES C CXX) # C: miniaudio

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SHARKCUT_BUILD_APP "Compilar la aplicación (requiere Qt 6)" ON)
option(SHARKCUT_BUILD_TESTS "Compilar los tests" ON)

# Avisos del compilador compartidos por todos los objetivos propios.
add_library(sharkcut_warnings INTERFACE)
if(MSVC)
    target_compile_options(sharkcut_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(sharkcut_warnings INTERFACE -Wall -Wextra -Wpedantic)
    # En Debug, la biblioteca estándar comprueba los accesos (optional vacío, índices
    # fuera de rango…) como hace MSVC en Windows: los fallos salen también en Linux.
    target_compile_definitions(sharkcut_warnings INTERFACE $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(FFmpeg)
include(miniaudio)
include(json)
include(whisper)

add_subdirectory(src/core)
add_subdirectory(src/media)
add_subdirectory(src/audio)
add_subdirectory(src/speech)
add_subdirectory(tools/bench)

if(SHARKCUT_BUILD_APP)
    # GuiPrivate: cabeceras de QRhi (rhi/qrhi.h), la capa gráfica que usa el visor.
    # Su uso es intencionado (API documentada con compatibilidad limitada): sin aviso.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2 ShaderTools GuiPrivate Network)
    qt_standard_project_setup(REQUIRES 6.10)
    add_subdirectory(src/render)
    add_subdirectory(src/app)
    add_subdirectory(tools/filmstrip)
endif()

if(SHARKCUT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
