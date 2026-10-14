qt_add_executable(SharkCut WIN32
    main.cpp
)

# Theme.qml es un singleton (pragma Singleton); debe declararse antes del módulo.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(SharkCut
    URI SharkCut
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
    SOURCES
        appinfo.h
        appinfo.cpp
)

# El ejecutable va a bin/ para no chocar con la carpeta del módulo QML "SharkCut".
set_target_properties(SharkCut PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

target_compile_definitions(SharkCut PRIVATE SHARKCUT_VERSION="${PROJECT_VERSION}")
target_link_libraries(SharkCut PRIVATE Qt6::Quick sharkcut_core sharkcut_warnings)

# Instalación: `cmake --install` deja una carpeta lista para ejecutar
# (en Windows copia las DLL de Qt con windeployqt).
install(TARGETS SharkCut
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
qt_generate_deploy_qml_app_script(
    TARGET SharkCut
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
)
install(SCRIPT ${deploy_script})
