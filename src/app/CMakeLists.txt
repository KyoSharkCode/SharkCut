qt_add_executable(SharkCut WIN32
    main.cpp
)

# Theme.qml es un singleton (pragma Singleton); debe declararse antes del módulo.
set_source_files_properties(qml/Theme.qml qml/Trazos.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        qml/CampoNumero.qml
        qml/Selector.qml
        qml/SelectorFuente.qml
        qml/SelectorColor.qml
        qml/LineaTiempo.qml
        qml/Icono.qml
        qml/Trazos.qml
        qml/BotonIcono.qml
        qml/Medidor.qml
        qml/ArrastreMedio.qml
        qml/ArrastreTransicion.qml
        qml/PanelMedios.qml
        qml/DialogoNuevoProyecto.qml
        qml/DialogoNombre.qml
        qml/DialogoSubtitulos.qml
        qml/DialogoAjusteClip.qml
        qml/DialogoActualizacion.qml
        qml/DialogoAcercaDe.qml
        qml/DialogoSistema.qml
        qml/MenuTema.qml
        qml/BarraDesplazamiento.qml
        qml/PanelBiblioteca.qml
        qml/PanelPropiedades.qml
        qml/PaginaProyectos.qml
        qml/PaginaExportar.qml
    SOURCES
        appinfo.h
        appinfo.cpp
        videoviewer.h
        videoviewer.cpp
        compositionsource.h
        playbackclock.h
        playbackclock.cpp
        playbacktrace.h
        playbacktrace.cpp
        editor.h
        editor.cpp
        editor_captions.cpp
        editor_presets.cpp
        editor_hardware.cpp
        editor_anchor.cpp
        editor_keyframes.cpp
        editor_transcription.cpp
        modeldownload.h
        modeldownload.cpp
        updater.h
        updater.cpp
        logging.h
        logging.cpp
        thumbnails.h
        thumbnails.cpp
        waveforms.h
        timelinemodel.h
        proxies.h
        proxies.cpp
        timelinemodel.cpp
        waveforms.cpp
    RESOURCES
        icons/sharkcut.png
)

# Icono y versión del ejecutable en Windows (generados por tools/icono/generar_icono.py).
if(WIN32)
    configure_file(sharkcut.rc.in ${CMAKE_CURRENT_BINARY_DIR}/sharkcut.rc @ONLY)
    target_sources(SharkCut PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/sharkcut.rc)
endif()

# El ejecutable va a bin/ para no chocar con la carpeta del módulo QML "SharkCut".
set_target_properties(SharkCut PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

target_compile_definitions(SharkCut PRIVATE SHARKCUT_VERSION="${PROJECT_VERSION}")
target_link_libraries(SharkCut PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::GuiPrivate
    Qt6::Network
    sharkcut_render
    sharkcut_speech
    sharkcut_core
    sharkcut_media
    sharkcut_audio
    sharkcut_warnings
)
if(WIN32)
    target_link_libraries(SharkCut PRIVATE dbghelp) # volcado al cerrarse por un fallo
endif()
sharkcut_copy_runtime_dlls(SharkCut)

# Efectos de sonido de las transiciones: junto al ejecutable (presets/sfx/), donde los
# busca el editor.
add_custom_command(TARGET SharkCut POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/presets/sfx $<TARGET_FILE_DIR:SharkCut>/presets/sfx
    COMMENT "Copiando los efectos de sonido"
)

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
install(DIRECTORY ${PROJECT_SOURCE_DIR}/presets/sfx
    DESTINATION ${CMAKE_INSTALL_BINDIR}/presets
)

# FFmpeg (DLL y licencia) junto al ejecutable instalado.
if(WIN32)
    install(IMPORTED_RUNTIME_ARTIFACTS
        FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES ${SHARKCUT_FFMPEG_LICENSE} DESTINATION licenses RENAME FFmpeg.txt)
endif()
# Licencias de los componentes (ver licenses/LEEME.md).
install(DIRECTORY ${PROJECT_SOURCE_DIR}/licenses/ DESTINATION licenses)
