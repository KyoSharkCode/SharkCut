# Motor de medios: análisis, decodificación y cola de fotogramas.
# C++ puro + FFmpeg, sin Qt, para poder probarlo sin ventanas.
add_library(sharkcut_media STATIC
    ffmpeg_util.h
    ffmpeg_util.cpp
    media_error.h
    media_info.h
    media_info_internal.h
    media_info.cpp
    video_frame.h
    color.h
    color.cpp
    video_decoder.h
    video_decoder.cpp
    audio_source.h
    audio_decoder.h
    audio_decoder.cpp
    sequence_audio.h
    sequence_audio.cpp
    timeline_audio.h
    timeline_audio.cpp
    waveform.h
    waveform.cpp
    mp4_writer.h
    mp4_writer.cpp
    decode_worker.h
    decode_worker.cpp
    proxy.h
    proxy.cpp
)

find_package(Threads REQUIRED)

target_include_directories(sharkcut_media PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sharkcut_media
    PUBLIC sharkcut_core Threads::Threads
    PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
            sharkcut_warnings
)
