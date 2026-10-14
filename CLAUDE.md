# SharkCut

Editor de video nativo de escritorio para Windows, de uso personal (KyoSumiVT).
Pensado para VODs de Twitch largos (40+ min) y para shorts/clips.
El plan completo está en docs/PLAN.md: léelo antes de proponer cambios grandes.

## Stack
- C++20 + Qt 6.10.3, interfaz en Qt Quick / QML.
- Render: QRhi (backend Direct3D 11 en Windows) + shaders GLSL 440 compilados
  con qsb. Vista previa y exportación usan el MISMO compositor. Las texturas que
  se renderizan y luego se muestrean se invierten en Y si rhi->isYUpInFramebuffer().
  Los shaders deben valer para GLSL ES 100 (bucles con límite constante).
- Medios: FFmpeg enlazado como biblioteca (libavformat/libavcodec; Windows: build
  de Gyan 9.0.2 descargada por cmake/FFmpeg.cmake; Linux: pkg-config), con
  NVDEC (D3D11VA) para decodificar y NVENC para codificar. Los fotogramas se
  quedan en la GPU siempre que sea posible.
- Audio: miniaudio 0.11.25 (WASAPI) + FFmpeg/swresample; el audio es el reloj maestro.
- Compilación: CMake (presets) + Ninja. Tests: Catch2 v3 + ctest.
- Archivo de proyecto: `.sharkp` (JSON con campo `version`).
- Tiempos internos en enteros (fotogramas/ticks) con aritmética racional,
  nunca segundos en coma flotante.

## Arquitectura
- src/core y src/media no dependen de Qt; deben poder probarse sin ventanas.
  src/media: análisis (probeMedia), VideoDecoder, DecodeWorker (hilo + cola), color.
  src/audio: AudioOutput (miniaudio + reloj maestro), AudioRingBuffer, AudioWorker.
  src/core también: easing, catálogo de transiciones (transition.h) y la secuencia
  A → transición → B (sequence.h).
  src/core también: proyecto y línea de tiempo (project.h), operaciones de edición
  (edit.h), deshacer/rehacer (history.h: cada edición se aplica a una copia del
  proyecto) y el archivo .sharkp (project_file.h, con nlohmann/json).
  src/media también: AudioSource, SequenceAudioSource (mezcla A/B con fundido).
  src/render (Qt Gui, sin Quick): Compositor QRhi (capas → transición → lienzo →
  presentación) y sus shaders; lo usan el visor y la exportación (mismo motor).
  src/app: Player (origen, marcas I/O), TransitionLab (secuencia de prueba),
  PlaybackClock (reloj audio/temporizador), VideoViewer (QQuickRhiItem + Compositor).
  src/media también: Mp4Writer (H.264 por hardware con respaldo x264 + AAC) y
  TimelineAudioSource (audio de la línea de tiempo).
  src/app también: Editor (proyecto + historial, herramientas y reproducción de la línea
  de tiempo); interfaz en qml/LineaTiempo.qml.
  src/render también: SequenceExporter (exportación en un hilo con su propio QRhi).
  tools/filmstrip: sharkcut_filmstrip, hoja de contactos de las transiciones sin interfaz
  (útil para revisar cambios en transition.frag); con --export exporta como la app.
  En Linux sin pantalla, ejecutar con xvfb-run.
- Formato de audio interno: float estéreo intercalado a 48 kHz; la muestra 0 es el
  primer fotograma del video. El reloj de reproducción lo marca el audio.
- El callback de audio es tiempo real: sin esperas, sin reservas de memoria, sin
  bloqueos (solo try_lock).
- Pruebas sin tarjeta de sonido: SHARKCUT_AUDIO_DEVICE=null (salida muda que avanza
  a velocidad real); los tests usan AudioOutput::Device::Null.
- El código debe compilar con FFmpeg 6.1 (Ubuntu) y 9.x (Windows): solo API moderna
  (nada de key_frame, avcodec_close ni av_err2str).
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
- Comentarios y textos de la interfaz en español. Excepción: nombres de
  TEST_CASE solo en ASCII (sin acentos ni ñ), porque ctest en Windows no los encuentra.
- Estilo visual del editor: tema "Marea Profunda" (docs/PLAN.md, sección 15),
  definido en src/app/qml/Theme.qml. No usar colores sueltos fuera del tema.

## Comandos
- cmake --preset windows-debug        (en Linux: linux-debug; requiere QT_ROOT_DIR)
- cmake --build --preset windows-debug
- ctest --preset windows-debug
- Solo núcleo y tests, sin Qt: cmake -S . -B build/core -G Ninja -DSHARKCUT_BUILD_APP=OFF
