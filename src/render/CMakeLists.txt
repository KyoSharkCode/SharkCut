# Motor de composición en la GPU (QRhi): el mismo para vista previa y exportación.
# Usa Qt Gui (QRhi), pero no Qt Quick ni la interfaz.
add_library(sharkcut_render STATIC
    compositor.h
    compositor.cpp
    caption_renderer.h
    caption_renderer.cpp
    text_frame.h
    text_frame.cpp
    sequence_exporter.h
    sequence_exporter.cpp
)

# Shaders GLSL 440 compilados con qsb para cada API gráfica (D3D11, Vulkan, OpenGL…).
qt_add_shaders(sharkcut_render "sharkcut_render_shaders"
    PREFIX "/sharkcut"
    FILES
        shaders/layer.vert
        shaders/layer.frag
        shaders/quad.vert
        shaders/transition.frag
        shaders/present.frag
        shaders/overlay.frag
)

target_include_directories(sharkcut_render PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sharkcut_render
    PUBLIC sharkcut_core sharkcut_media Qt6::Gui
    PRIVATE Qt6::GuiPrivate sharkcut_warnings
)
