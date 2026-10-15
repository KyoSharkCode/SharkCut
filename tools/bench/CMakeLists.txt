# Herramienta de desarrollo: mide decodificación, búsquedas, ondas y edición de una
# línea de tiempo grande (sin Qt), para comparar antes y después de optimizar.
add_executable(sharkcut_bench main.cpp)
target_link_libraries(sharkcut_bench PRIVATE sharkcut_media sharkcut_core sharkcut_warnings)
sharkcut_copy_runtime_dlls(sharkcut_bench)
set_target_properties(sharkcut_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
