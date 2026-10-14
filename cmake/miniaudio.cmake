# miniaudio: salida de audio multiplataforma (WASAPI en Windows), en un solo archivo.
# Solo se compila la API de dispositivos; el resto (decodificación, motor…) se desactiva.
include(FetchContent)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d # 0.11.25
    SOURCE_SUBDIR no-cmake # no usar su CMakeLists.txt: se compila aquí abajo
)
FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
target_compile_definitions(miniaudio PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH
    MA_NO_ENGINE
)
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
