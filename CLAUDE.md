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
- Archivo de proyecto: `.sharkp` (JSON con campo `version`; hoy la 6, que abre la 1, la 2
  y la 3 como un proyecto de una secuencia; los marcadores son un campo opcional de la 4,
  la pantalla verde del clip, `transform.chromaKey`, llegó con la 5, las pistas de
  subtítulos, `captionTracks` de cada secuencia, con la 6, los keyframes de cada clip,
  `animation`, con la 7 y los efectos de imagen, `transform.effects`, con la 8).
  Autoguardado: copias de recuperación en la carpeta de datos de la app (recuperacion/).
- Tiempos internos en enteros (fotogramas/ticks) con aritmética racional,
  nunca segundos en coma flotante.

## Arquitectura
- src/core y src/media no dependen de Qt; deben poder probarse sin ventanas.
  src/media: análisis (probeMedia), VideoDecoder, DecodeWorker (hilo + cola), color.
  Los fotogramas son NV12 más un plano de alfa opcional (videos con transparencia, como
  .mov ProRes 4444 o Animación); el compositor lo usa en layer.frag.
  src/audio: AudioOutput (miniaudio + reloj maestro), AudioRingBuffer, AudioWorker.
  src/core también: easing, catálogo de transiciones (transition.h), la secuencia
  A → transición → B del laboratorio (sequence.h) y el catálogo de efectos de sonido
  (sfx.h, archivos en presets/sfx/ generados por tools/sfx/generar_sfx.py).
  src/core también: proyecto y línea de tiempo (project.h: pistas en número variable,
  primero las de video de V1 hacia arriba y después las de audio; transformación y
  volumen por clip; transiciones en los cortes de las pistas de video, que usan el
  material sobrante del medio sin mover los clips; marcadores), operaciones de edición
  (edit.h, que mantienen coherentes enlaces y transiciones; también las de rizo: Q/W,
  eliminar con rizo, cerrar hueco), deshacer/rehacer (history.h: cada
  edición se aplica a una copia del proyecto) y el archivo .sharkp (project_file.h, con
  nlohmann/json).
  src/core también: document.h, el proyecto completo (medios, carpetas y varias
  secuencias, cada una con su lienzo, fps y pistas). Las operaciones de edit.h trabajan
  sobre un `Project`, que es la vista de una secuencia (`Document::view`/`store`); el
  historial de cada proyecto abierto es un `DocumentHistory`.
  src/media también: AudioSource, SequenceAudioSource (mezcla A/B con fundido).
  src/render (Qt Gui, sin Quick): Compositor QRhi (capas de abajo arriba, cada una con
  posición, escala, opacidad y recorte, o dos clips con transición → lienzo →
  presentación) y sus shaders; lo usan el visor y la exportación (mismo motor).
  src/app: PlaybackClock (reloj audio/temporizador), VideoViewer (QQuickRhiItem +
  Compositor).
  src/core también: export_settings.h (tramo y fps de la exportación, resoluciones y
  tasas recomendadas) y parseTimecode en time.h.
  AV1 (grabaciones de OBS): por la GPU con el decodificador "av1" de FFmpeg (NVDEC en las
  RTX 30+), comprobado con el primer fotograma; si no, dav1d por software.
  Keyframes: ClipAnimation en Clip (project.h), por propiedad de ClipTransform
  (AnimProperty: colocación, rotación, opacidad, recorte y los valores de los efectos),
  con su curva (easing.h, kKeyframeEasings). Cada keyframe va en la
  posición del medio (Clip::mediaFrameAt), así sigue al contenido al recortar o dividir.
  transformAt(clip, frame) da la transformación animada (la usan el visor, Propiedades y
  la exportación); setClipTransformAt (edit.h) convierte un cambio en un keyframe si la
  propiedad está animada. Editor: editor_keyframes.cpp; interfaz: ⏱ ◀ ◆ ▶ junto a cada
  valor en PanelPropiedades y ◆ sobre los clips en LineaTiempo.
  Efectos de imagen: ClipEffects dentro de ClipTransform (blanco y negro, desenfoque,
  esquinas redondeadas, sombra/resplandor; medidas en px del lienzo, que la exportación
  escala). Todo en layer.frag: desenfoque con 32 muestras (disco de Vogel), esquinas con
  una distancia a un rectángulo redondeado, y la sombra es la misma capa dibujada antes en
  "modo sombra" (geometría ampliada, forma del clip con su alfa y su croma, difuminada).
  Se aplican desde la pestaña Efectos (Editor::applyEffect) y se ajustan en Propiedades.
  Ajustes guardados (presets de clips): src/core/clip_preset.h (presetFromClip,
  applyPreset; partes que lleva y animación en µs desde el inicio o el final del clip;
  JSON en project_file.cpp). Se guardan en QSettings "efectos/ajustes" (todos los
  proyectos). Editor: editor_presets.cpp; interfaz: «Guardar como ajuste…» en
  PanelPropiedades (qml/DialogoAjusteClip.qml) y lista en la pestaña Efectos de
  PanelMedios (se arrastran como un efecto, "ajuste:<nombre>" en applyEffect).
  Pantalla verde: ChromaKey dentro de ClipTransform (project.h), aplicada en layer.frag;
  src/media/chroma_key.h detecta el color de fondo en los bordes de un fotograma.
  Subtítulos: pistas propias (CaptionTrack en project.h, encima de todo) con estilo de
  pista y propio; src/core/captions.h divide palabras en subtítulos (caracteres, líneas,
  duración mínima…) y lee/escribe .srt; las operaciones están en edit.h (las de rizo
  también los mueven). src/render/caption_renderer.h los dibuja (QPainter) y el
  compositor los pone encima como superposiciones (overlay.frag). src/speech (sin Qt):
  transcripción local con whisper.cpp (cmake/whisper.cmake, siempre optimizado y con base
  AVX2 fija: nada de GGML_NATIVE ni AVX-512, que con MSVC rompía en procesadores sin él;
  GPU con -DSHARKCUT_WHISPER_CUDA=ON o _VULKAN=ON); el modelo se descarga una vez a
  <datos de la app>/modelos (ModelDownload, con su SHA-256). En el editor:
  editor_captions.cpp (pistas, selección, estilo, ajustes guardados en QSettings, .srt) y
  editor_transcription.cpp; interfaz: pestaña Subtítulos de PanelMedios, pistas S1… en
  LineaTiempo (TimelineClipsModel con captions: true), sección en PanelPropiedades y
  qml/DialogoSubtitulos.qml. Test [speech]: necesita SHARKCUT_WHISPER_MODEL y
  SHARKCUT_WHISPER_SAMPLE (si no, se salta).
  src/media también: proxy.h (proxies 540p para la vista previa, en la caché del
  proyecto; DecodeWorker guarda una caché de fotogramas para ir hacia atrás).
  src/media también: Mp4Writer (H.264 o H.265 por hardware con respaldo x264/x265 + AAC),
  TimelineAudioSource (mezcla de las pistas de audio) y waveform.h (onda del audio y su
  caché en disco).
  src/app también: Editor (proyectos abiertos con su historial, pestañas de secuencias,
  herramientas, reproducción de la secuencia activa, exportación y recientes; los
  ajustes de la app van en QSettings). Interfaz: pestañas superiores Proyectos
  (qml/PaginaProyectos.qml), Editor (qml/PanelMedios.qml con qml/PanelBiblioteca.qml ·
  qml/PanelPropiedades.qml · Programa, y qml/LineaTiempo.qml) y Exportar
  (qml/PaginaExportar.qml); diálogos qml/Dialogo*.qml. Menús: qml/MenuTema.qml. Miniaturas: ThumbnailProvider ("image://miniaturas/…", en hilos).
  Fotogramas de los clips en la línea de tiempo: FrameThumbnailProvider
  ("image://fotogramas/<ruta>@<µs>", thumbnails.h).
  Ondas: WaveformCache y WaveformView (waveforms.h), caché en <proyecto>.sharkp.cache/.
  Proxies: ProxyManager (proxies.h). Línea de tiempo virtualizada: TimelineClipsModel
  (timelinemodel.h): con miles de clips, nada de recorrer todos desde QML.
  tools/bench: sharkcut_bench, mide decodificación, saltos, marcha atrás, proxies y
  edición de una línea de tiempo grande (sin Qt).
  Valores numéricos editables: qml/CampoNumero.qml (arrastrar o escribir). Listas
  desplegables: qml/Selector.qml (con desplazamiento); fuentes: qml/SelectorFuente.qml
  (búsqueda y favoritas, guardadas en QSettings "fuentes/favoritas").
  Iconos: qml/Icono.qml (trazos SVG de qml/Trazos.qml dibujados con QtQuick.Shapes);
  añadir los nuevos en Trazos.qml.
  AudioOutput::takePeakLevels alimenta el medidor de la mezcla.
  src/render también: SequenceExporter (exportación en un hilo con su propio QRhi).
  tools/filmstrip: sharkcut_filmstrip, hoja de contactos de las transiciones sin interfaz
  (útil para revisar cambios en transition.frag); con --export exporta como la app y
  con --export-project exporta la secuencia activa de un .sharkp (con --escala, --fps,
  --desde/--hasta, --h265, --sin-audio, --mbps).
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
- Registro de cada sesión (src/app/logging.h): stderr y los mensajes de Qt van a
  <datos de la app>/registros/sharkcut-<fecha>.log (se guardan las últimas 8); en Windows,
  un cierre por fallo apunta el código y deja ultimo-cierre.dmp. Botón «Abrir la carpeta
  de registros» en Proyectos. SHARKCUT_LOG=consola: todo por la consola, sin archivo.

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
