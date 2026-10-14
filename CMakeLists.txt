cmake_minimum_required(VERSION 3.25)

project(SharkCut
    VERSION 0.1.0
    DESCRIPTION "Editor de video nativo para Windows"
    LANGUAGES CXX)

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
endif()

add_subdirectory(src/core)

if(SHARKCUT_BUILD_APP)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
    qt_standard_project_setup(REQUIRES 6.10)
    add_subdirectory(src/app)
endif()

if(SHARKCUT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
