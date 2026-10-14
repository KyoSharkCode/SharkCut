include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG fa43b77429ba76c462b1898d6cd2f2d7a9416b14 # v3.7.1
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(Catch2)

# Los nombres de TEST_CASE deben ir sin acentos ni ñ: en Windows, ctest pasa el
# nombre a Catch2 con otra codificación y el test no se encontraría.
add_executable(sharkcut_tests
    test_time.cpp
    test_media.cpp
    test_color.cpp
    test_audio.cpp
    test_transition.cpp
    test_export.cpp
    test_clip.h
    test_clip.cpp
)
target_link_libraries(sharkcut_tests PRIVATE
    sharkcut_core
    sharkcut_media
    sharkcut_audio
    sharkcut_warnings
    Catch2::Catch2WithMain
    # El generador de clips de prueba usa FFmpeg directamente.
    FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil
)
sharkcut_copy_runtime_dlls(sharkcut_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
# PRE_TEST: la lista de tests se obtiene al ejecutar ctest, cuando las DLL de FFmpeg
# ya están junto al ejecutable.
catch_discover_tests(sharkcut_tests DISCOVERY_MODE PRE_TEST)
