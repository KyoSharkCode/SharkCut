# Herramienta de desarrollo: hoja de contactos de las transiciones (sin interfaz).
qt_add_executable(sharkcut_filmstrip main.cpp)
target_link_libraries(sharkcut_filmstrip PRIVATE
    sharkcut_render sharkcut_media sharkcut_core Qt6::Gui Qt6::GuiPrivate sharkcut_warnings)
sharkcut_copy_runtime_dlls(sharkcut_filmstrip)
set_target_properties(sharkcut_filmstrip PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
