# Núcleo de SharkCut: C++ puro, sin dependencias de Qt ni de la interfaz.
add_library(sharkcut_core STATIC
    time.h
    time.cpp
    easing.h
    easing.cpp
    transition.h
    transition.cpp
    sfx.h
    sfx.cpp
    sequence.h
    sequence.cpp
    project.h
    project.cpp
    edit.h
    edit.cpp
    history.h
    document.h
    document.cpp
    project_file.h
    project_file.cpp
    export_settings.h
    export_settings.cpp
)

target_include_directories(sharkcut_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sharkcut_core PRIVATE sharkcut_warnings nlohmann_json::nlohmann_json)
