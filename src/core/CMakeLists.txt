# Núcleo de SharkCut: C++ puro, sin dependencias de Qt ni de la interfaz.
add_library(sharkcut_core STATIC
    time.h
    time.cpp
    easing.h
    easing.cpp
    transition.h
    transition.cpp
    sequence.h
    sequence.cpp
)

target_include_directories(sharkcut_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sharkcut_core PRIVATE sharkcut_warnings)
