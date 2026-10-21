# whisper.cpp (licencia MIT): subtítulos automáticos en local. Se compila con SharkCut
# como biblioteca estática. Por defecto transcribe con el procesador; con la GPU:
#   -DSHARKCUT_WHISPER_CUDA=ON    (NVIDIA; necesita el CUDA Toolkit 12.8 o posterior,
#                                  que es el primero con las RTX 50)
#   -DSHARKCUT_WHISPER_VULKAN=ON  (cualquier GPU; necesita el Vulkan SDK)
include(FetchContent)

option(SHARKCUT_WHISPER_CUDA "Transcribir con la GPU NVIDIA (CUDA)" OFF)
option(SHARKCUT_WHISPER_VULKAN "Transcribir con la GPU (Vulkan)" OFF)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
# Instrucciones del procesador: una base fija (AVX2, la de casi todos los PC de los
# últimos ~10 años) en vez de detectarlas al compilar. Con MSVC, la detección de ggml
# (GGML_NATIVE) da AVX-512 por bueno aunque el procesador no lo tenga, y la app se
# cerraba con «instrucción no válida» al transcribir (p. ej. en un Ryzen Zen 3). Así,
# además, lo compilado sirve en otros PC. transcriber.cpp comprueba AVX2 antes de usarlo.
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_SSE42 ON CACHE BOOL "" FORCE)
set(GGML_AVX ON CACHE BOOL "" FORCE)
set(GGML_AVX2 ON CACHE BOOL "" FORCE)
set(GGML_BMI2 ON CACHE BOOL "" FORCE)
set(GGML_FMA ON CACHE BOOL "" FORCE)
set(GGML_F16C ON CACHE BOOL "" FORCE)
set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
set(GGML_AVX512_VBMI OFF CACHE BOOL "" FORCE)
set(GGML_AVX512_VNNI OFF CACHE BOOL "" FORCE)
set(GGML_AVX512_BF16 OFF CACHE BOOL "" FORCE)
set(GGML_AVX_VNNI OFF CACHE BOOL "" FORCE)
set(GGML_CUDA ${SHARKCUT_WHISPER_CUDA} CACHE BOOL "" FORCE)
set(GGML_VULKAN ${SHARKCUT_WHISPER_VULKAN} CACHE BOOL "" FORCE)

FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG d1be6fde11ac6e0407606b4e42fe72d34add8037 # v1.9.5
    EXCLUDE_FROM_ALL
    SYSTEM
)
# Biblioteca estática y siempre optimizada: sin optimizar (Debug), transcribir unos
# segundos tarda minutos. Solo afecta a whisper y ggml (las variables se restauran).
# whisper.cpp pide CMake 3.5, así que su option(BUILD_SHARED_LIBS) seguía la política
# antigua (CMP0077): ignoraba el OFF de aquí y creaba ggml y whisper como DLL en bin/,
# donde sharkcut_tests.exe no las encontraba (0xc0000135 en Windows). Con la política
# nueva respeta el OFF. Las compilaciones anteriores guardaron ese ON en la caché: fuera.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
get_property(SHARKCUT_SHARED_HELP CACHE BUILD_SHARED_LIBS PROPERTY HELPSTRING)
if(SHARKCUT_SHARED_HELP STREQUAL "build shared libraries")
    unset(BUILD_SHARED_LIBS CACHE)
endif()
set(SHARKCUT_SAVED_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(SHARKCUT_SAVED_C_DEBUG ${CMAKE_C_FLAGS_DEBUG})
set(SHARKCUT_SAVED_CXX_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
set(BUILD_SHARED_LIBS OFF)
if(MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/O2 /Ob2 /Zi")
    set(CMAKE_CXX_FLAGS_DEBUG "/O2 /Ob2 /Zi")
else()
    set(CMAKE_C_FLAGS_DEBUG "-O2 -g")
    set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g")
endif()
FetchContent_MakeAvailable(whisper)
set(BUILD_SHARED_LIBS ${SHARKCUT_SAVED_SHARED_LIBS})
set(CMAKE_C_FLAGS_DEBUG ${SHARKCUT_SAVED_C_DEBUG})
set(CMAKE_CXX_FLAGS_DEBUG ${SHARKCUT_SAVED_CXX_DEBUG})
unset(CMAKE_POLICY_DEFAULT_CMP0077)

# Sus avisos al compilar (p. ej. «pragma GCC desconocida» con MSVC) no son nuestros: fuera.
foreach(target IN ITEMS whisper ggml ggml-base ggml-cpu)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /w)
        else()
            target_compile_options(${target} PRIVATE -w)
        endif()
    endif()
endforeach()
