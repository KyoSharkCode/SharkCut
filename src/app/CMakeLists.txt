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
        qml/Boton.qml
        qml/Chip.qml
        qml/FilaDato.qml
        qml/Pestana.qml
        qml/Deslizador.qml
        qml/Selector.qml
        qml/BarraTiempo.qml
        qml/LineaTiempo.qml
    SOURCES
        appinfo.h
        appinfo.cpp
        player.h
        player.cpp
        videoviewer.h
        videoviewer.cpp
        compositionsource.h
        playbackclock.h
        playbackclock.cpp
        transitionlab.h
        transitionlab.cpp
        editor.h
        editor.cpp
)

# El ejecutable va a bin/ para no chocar con la carpeta del módulo QML "SharkCut".
set_target_properties(SharkCut PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

target_compile_definitions(SharkCut PRIVATE SHARKCUT_VERSION="${PROJECT_VERSION}")
target_link_libraries(SharkCut PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::GuiPrivate
    sharkcut_render
    sharkcut_core
    sharkcut_media
    sharkcut_audio
    sharkcut_warnings
)
sharkcut_copy_runtime_dlls(SharkCut)

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

# FFmpeg (DLL y licencia) junto al ejecutable instalado.
if(WIN32)
    install(IMPORTED_RUNTIME_ARTIFACTS
        FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES ${SHARKCUT_FFMPEG_LICENSE} DESTINATION licenses RENAME FFmpeg.txt)
endif()
