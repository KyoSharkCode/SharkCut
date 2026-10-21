#Voz : transcripción del audio a palabras con su momento(whisper.cpp).Sin Qt.
add_library(sharkcut_speech STATIC transcriber.h transcriber.cpp)

    target_include_directories(sharkcut_speech PUBLIC ${PROJECT_SOURCE_DIR} / src)
        target_link_libraries(
            sharkcut_speech PUBLIC sharkcut_core sharkcut_media PRIVATE whisper sharkcut_warnings)
