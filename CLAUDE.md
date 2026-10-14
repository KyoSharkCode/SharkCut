# SharkCut

Editor de video nativo de escritorio para Windows, de uso personal (KyoSumiVT).
Pensado para VODs de Twitch largos (40+ min) y para shorts/clips.
El plan completo está en docs/PLAN.md: léelo antes de proponer cambios grandes.

## Stack
- C++20 + Qt 6 (LTS ≥ 6.8), interfaz en Qt Quick / QML.
- Render: QRhi (backend Direct3D 11 en Windows) + shaders GLSL 440 compilados
  con qsb. Vista previa y exportación usan el MISMO compositor.
- Medios: FFmpeg enlazado como biblioteca (libavformat/libavcodec), con
  NVDEC (D3D11VA) para decodificar y NVENC para codificar. Los fotogramas se
  quedan en la GPU siempre que sea posible.
- Audio: miniaudio (WASAPI) + mezclador propio; el audio es el reloj maestro.
- Compilación: CMake + vcpkg. Tests: Catch2 + ctest.
- Archivo de proyecto: `.sharkp` (JSON con campo `version`).
- Tiempos internos en enteros (fotogramas/ticks) con aritmética racional,
  nunca segundos en coma flotante.

## Arquitectura
- src/core no depende de Qt Quick ni de la interfaz; debe poder probarse sin
  ventanas.
- Código específico de Windows/NVIDIA solo en src/media/platform/win/, detrás
  de interfaces, con un camino por software que compila en Linux.
- Toda modificación del proyecto pasa por un comando (deshacer/rehacer).
- Nada de decodificar ni acceder a disco en el hilo de interfaz.

## Reglas
- Edición no destructiva: nunca modificar los archivos de medios originales.
  Proxies, miniaturas, ondas y remux van a la carpeta de caché del proyecto.
- No copiar código ni recursos de Premiere Composer ni de otros productos de pago.
- Todo sonido añadido debe registrarse en presets/sfx/LICENCIAS.md.
- Respetar las fases del plan; no adelantar funciones de fases posteriores.
- Comentarios y textos de la interfaz en español.
- Estilo visual de la app: marca Marea Nocturna de KyoSumiVT.

## Comandos (actualizar cuando existan)
- cmake --preset windows-release
- cmake --build --preset windows-release
- ctest --preset windows-release
