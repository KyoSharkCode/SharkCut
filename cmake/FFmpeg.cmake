# Localiza FFmpeg y define los objetivos importados:
#   FFmpeg::avformat  FFmpeg::avcodec  FFmpeg::avutil  FFmpeg::swscale  FFmpeg::swresample
#
# Windows: descarga automáticamente la build compartida de Gyan (incluye NVENC,
#          NVDEC y D3D11VA) a .deps/, con versión y hash fijados. Se puede usar una copia
#          local con -DSHARKCUT_FFMPEG_DIR=<carpeta con bin/, include/ y lib/>.
# Linux:   usa las bibliotecas del sistema mediante pkg-config.

set(SHARKCUT_FFMPEG_LIBS avformat avcodec avutil swscale swresample)

if(WIN32)
    set(SHARKCUT_FFMPEG_VERSION "9.0.2")
    set(SHARKCUT_FFMPEG_DIR "" CACHE PATH "Carpeta de FFmpeg precompilado (vacío = descargar)")
    set(SHARKCUT_DEPS_DIR "${PROJECT_SOURCE_DIR}/.deps" CACHE PATH "Carpeta de dependencias descargadas")

    if(NOT SHARKCUT_FFMPEG_DIR)
        # Se descarga una sola vez a .deps/ (compartido por Debug y Release, fuera de git).
        set(name "ffmpeg-${SHARKCUT_FFMPEG_VERSION}-full_build-shared")
        set(archive "${SHARKCUT_DEPS_DIR}/${name}.7z")
        set(SHARKCUT_FFMPEG_DIR "${SHARKCUT_DEPS_DIR}/${name}")
        if(NOT EXISTS "${SHARKCUT_FFMPEG_DIR}/include/libavcodec/avcodec.h")
            if(NOT EXISTS "${archive}")
                message(STATUS "Descargando FFmpeg ${SHARKCUT_FFMPEG_VERSION} (~60 MB)…")
                file(DOWNLOAD
                    "https://github.com/GyanD/codexffmpeg/releases/download/${SHARKCUT_FFMPEG_VERSION}/${name}.7z"
                    "${archive}.part"
                    EXPECTED_HASH SHA256=4d2060a8b34a940aa47d785142055bb92a63053781e55f2ace4546edd519a8f5
                    STATUS status
                )
                list(GET status 0 code)
                if(NOT code EQUAL 0)
                    file(REMOVE "${archive}.part")
                    message(FATAL_ERROR "No se pudo descargar FFmpeg: ${status}")
                endif()
                file(RENAME "${archive}.part" "${archive}")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${SHARKCUT_DEPS_DIR}")
        endif()
    endif()

    if(NOT EXISTS "${SHARKCUT_FFMPEG_DIR}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR "No se encontró FFmpeg en '${SHARKCUT_FFMPEG_DIR}'")
    endif()
    message(STATUS "FFmpeg: ${SHARKCUT_FFMPEG_DIR}")

    foreach(lib IN LISTS SHARKCUT_FFMPEG_LIBS)
        file(GLOB dll "${SHARKCUT_FFMPEG_DIR}/bin/${lib}-*.dll")
        if(NOT dll)
            message(FATAL_ERROR "Falta ${lib}-*.dll en ${SHARKCUT_FFMPEG_DIR}/bin")
        endif()
        add_library(FFmpeg::${lib} SHARED IMPORTED GLOBAL)
        set_target_properties(FFmpeg::${lib} PROPERTIES
            IMPORTED_LOCATION "${dll}"
            IMPORTED_IMPLIB "${SHARKCUT_FFMPEG_DIR}/lib/${lib}.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${SHARKCUT_FFMPEG_DIR}/include"
        )
    endforeach()

    set(SHARKCUT_FFMPEG_LICENSE "${SHARKCUT_FFMPEG_DIR}/LICENSE")
else()
    find_package(PkgConfig REQUIRED)
    foreach(lib IN LISTS SHARKCUT_FFMPEG_LIBS)
        pkg_check_modules(SHARKCUT_PC_${lib} REQUIRED IMPORTED_TARGET GLOBAL lib${lib})
        add_library(FFmpeg::${lib} ALIAS PkgConfig::SHARKCUT_PC_${lib})
    endforeach()
    message(STATUS "FFmpeg (sistema): libavcodec ${SHARKCUT_PC_avcodec_VERSION}")
endif()

# Copia las DLL de FFmpeg junto al ejecutable tras compilarlo (solo Windows).
# Solo las de FFmpeg: las de Qt se cargan desde la instalación de Qt (PATH), porque
# Qt busca sus plugins junto a su propia DLL y una copia suelta no los encontraría.
function(sharkcut_copy_runtime_dlls target)
    if(WIN32)
        foreach(lib IN LISTS SHARKCUT_FFMPEG_LIBS)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:FFmpeg::${lib}> $<TARGET_FILE_DIR:${target}>
            )
        endforeach()
    endif()
endfunction()
