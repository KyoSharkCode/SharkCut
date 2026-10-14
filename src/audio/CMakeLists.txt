# Audio: salida (miniaudio), búfer en anillo y reloj maestro. Sin Qt.
add_library(sharkcut_audio STATIC
    ring_buffer.h
    audio_output.h
    audio_output.cpp
    audio_worker.h
    audio_worker.cpp
)

target_include_directories(sharkcut_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sharkcut_audio
    PUBLIC sharkcut_media
    PRIVATE miniaudio sharkcut_warnings
)
