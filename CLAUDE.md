# SharkCut

Editor de video nativo de escritorio para Windows, gratuito y libre (GPL 3.0), para
creadores de contenido; lo desarrolla KyoSumiVT. Pensado para VODs largos de stream
(40+ min) y para shorts/clips. Los textos públicos lo presentan como una app para la
comunidad, sin nombrar productos de terceros.
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
- Archivo de proyecto: `.sharkp` (JSON con campo `version`; hoy la 12, que abre la 1, la 2
  y la 3 como un proyecto de una secuencia; los marcadores son un campo opcional de la 4,
  la pantalla verde del clip, `transform.chromaKey`, llegó con la 5, las pistas de
  subtítulos, `captionTracks` de cada secuencia, con la 6, los keyframes de cada clip,
  `animation`, con la 7, los efectos de imagen, `transform.effects`, con la 8 y las
  transiciones como clips, `effect` del clip, con la 9, las imágenes fijas, `still` del
  medio, con la 10, los colores sólidos y el tinte con la 11, y los textos libres, `text` del
  clip, y la pista de audio del archivo de cada clip, `audioStream`, con la 12).
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
  (sfx.h, con su grupo: Clic, Pop, Swoosh, Whoosh, Impactos; archivos en presets/sfx/
  generados por tools/sfx/generar_sfx.py). Pestaña Sonidos de PanelMedios: se arrastran a
  una pista de audio (Editor::addSound) o se ponen con su golpe en el cabezal
  (addSoundAtPlayhead). Los que una edición añade a la biblioteca van a la carpeta
  «Recursos de SharkCut» (Folder::resources, la marca la reconoce, no el nombre;
  resourceFolder en document.h; Editor::edit los mueve ahí).
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
  Efectos de imagen: ClipEffects dentro de ClipTransform (blanco y negro, tinte, desenfoque,
  esquinas redondeadas, sombra/resplandor; medidas en px del lienzo, que la exportación
  escala). Todo en layer.frag: desenfoque con 32 muestras (disco de Vogel), esquinas con
  una distancia a un rectángulo redondeado, y la sombra es la misma capa dibujada antes en
  "modo sombra" (geometría ampliada, forma del clip con su alfa y su croma, difuminada).
  Se aplican desde la pestaña Efectos (Editor::applyEffect; a todos los clips de video
  seleccionados si el clip es uno de ellos) y se ajustan en Propiedades. Selección por
  cuadro en LineaTiempo (marcoSeleccion → Editor::selectArea).
  Transiciones como clips: Clip::effect (TransitionParams, sin
  medio, media = 0) en una pista de video; afecta a todo lo de debajo en su tramo. El
  compositor (CompositorLayer::adjust) copia el lienzo dibujado hasta ahí y le aplica la
  transición con A = B = esa copia. Su efecto avanza con `in` (dura durationFrames, como
  un medio): al cortarla o recortarla cada trozo sigue donde iba. placeEffect/setClipEffect
  (edit.h); Editor::addEffectClip (se centra en el corte más cercano, va a una pista libre y
  lleva su sonido con placeSoundHit) y setEffectClip. Archivo .sharkp versión 9
  (clip.effect). Las transiciones antiguas en los cortes (Track::transitions) se siguen
  abriendo y dibujando.
  Su sonido va enlazado con ella (Clip::link, linkEffectSound): los enlaces unen el video
  y el audio de un medio (mismo tramo) o una transición-clip y un clip de audio (cada uno
  con su tramo: se mueven, copian y quitan juntos, pero se recortan por separado).
  Imágenes fijas (PNG, JPG…): MediaItem::still (VideoStreamInfo::still al analizarlas:
  demuxers image2/*_pipe). VideoDecoder las lee al abrirlas y las da como fotograma 0;
  mediaFrameIndex es siempre 0 y sus clips se alargan sin límite (`in` puede ser < 0).
  Duración al importarlas: Editor::stillSeconds (QSettings "medios/duracionImagen").
  No adoptan lienzo ni fps y no tienen proxy.
  Copiar y pegar clips: copyClips/pasteClips (edit.h, Clipboard con sus medios) y
  Editor::copySelection/cutSelection/pasteAtPlayhead (Ctrl+C/X/V).
  Un clic fuera de un campo de texto le quita el teclado (FocusRelease en main.cpp): si
  no, los atajos de una tecla dejaban de funcionar.
  Ajustes guardados (presets de clips): src/core/clip_preset.h (presetFromClip,
  applyPreset; partes que lleva y animación en µs desde el inicio o el final del clip;
  JSON en project_file.cpp). Se guardan en QSettings "efectos/ajustes" (todos los
  proyectos). Editor: editor_presets.cpp; interfaz: «Guardar como ajuste…» en
  PanelPropiedades (qml/DialogoAjusteClip.qml) y lista en la pestaña Efectos de
  PanelMedios (se arrastran como un efecto, "ajuste:<nombre>" en applyEffect).
  Punto de anclaje (seguimiento de movimiento): src/media/motion_tracker.h (MotionTracker
  y trackVideo: correlación cruzada normalizada de la luminancia, de grueso a fino) y
  src/core/motion_anchor.h (mediaPointOnCanvas/canvasPointOnMedia, como el compositor;
  anchorClip convierte el recorrido en keyframes de posición, simplificados). Editor:
  editor_anchor.cpp (en segundo plano, con el proxy si lo hay); interfaz: «Punto de
  anclaje» en PanelPropiedades y MarcoAnclaje en Main.qml (recuadro en el visor).
  Colores sólidos: medios sin archivo con ruta "sharkcut-color:RRGGBB:ANCHOxALTO"
  (SolidColor, solidColorPath/solidColorOf en project.h); VideoDecoder da su fotograma como
  una imagen fija. addSolidColor/setSolidColor (document.h); en la interfaz, clic derecho en
  un hueco de Medios y «Cambiar el color…» en su menú.
  Colores elegibles: qml/SelectorColor.qml (toda la gama), se abre desde cada muestra.
  Textos libres: Clip::text (TextClip: contenido y CaptionStyle, con `align`) en una pista
  de video, sin medio. src/render/text_frame.h los dibuja con el motor de los subtítulos en
  un fotograma del tamaño del lienzo con transparencia (TextFrameCache), que el compositor
  trata como un video (visor: Editor::composition; exportación: SequenceExporter); no se
  decodifican (Clip::hasMedia). placeText/setClipText (edit.h); Editor: addText,
  setTextStyle… en editor_captions.cpp; interfaz: pestaña Texto de PanelMedios y la sección
  del subtítulo en PanelPropiedades (también edita textos).
  Varias pistas de audio en un archivo (MKV de OBS): Clip::audioStream (−1 = la principal)
  elige cuál suena; AudioDecoder, computeWaveform y TimelineAudioSource la abren. Editor:
  audioStreamsOf (análisis en segundo plano), setClipAudioStream y separateAudioStreams
  (edit.h: cada pista a su propia pista de audio).
  Girar 90°: Editor::rotateClip; fitToCanvas (motion_anchor.h) ajusta con el giro.
  Los dos shaders de capa (layer.vert y layer.frag) declaran el mismo bloque de uniformes:
  un campo nuevo va en los dos (si no, OpenGL no los enlaza y el visor sale negro).
  Pantalla verde: ChromaKey dentro de ClipTransform (project.h), aplicada en layer.frag;
  src/media/chroma_key.h detecta el color de fondo en los bordes de un fotograma.
  Subtítulos: pistas propias (CaptionTrack en project.h, encima de todo) con estilo de
  pista y propio; src/core/captions.h divide palabras en subtítulos (caracteres, líneas,
  duración mínima…) y lee/escribe .srt; las operaciones están en edit.h (las de rizo
  también los mueven; la cuchilla y Ctrl+K los dividen con splitCaptionsAt, y el borde
  entre dos pegados mueve los dos con trimCaptionEdge). src/render/caption_renderer.h los dibuja (QPainter) y el
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
  src/media también: proxy.h (proxies 540p para la vista previa, en la caché de medios;
  DecodeWorker guarda una caché de fotogramas para ir hacia atrás). Con
  «Proxies» activado, ProxyManager::setAutoCreate crea solos los que falten.
  Caché de medios común (ondas/ y proxies/, por archivo: ruta, tamaño y fecha del medio):
  Editor::cacheFolder (QSettings "cache/carpeta"; por defecto la caché del usuario,
  medios/), DialogoCache.qml en Proyectos; las carpetas antiguas <proyecto>.sharkp.cache se
  siguen leyendo.
  Resolución de la vista previa (Completa · 1/2 · 1/4, Editor::previewDivisor): VideoDecoder
  y DecodeWorker con `downscale`, el visor dibuja a CompositionSource::renderSize y las
  medidas en píxeles del lienzo se escalan con scaledTransform (project.h), como al exportar.
  Lienzo y fps de una secuencia ya creada: setSequenceSettings (edit.h, pasa todos los tiempos
  a los fps nuevos con los mismos segundos) y Editor::editSequence; ajustes de secuencia
  guardados en QSettings "secuencias/ajustes"; diálogo: DialogoNuevoProyecto.qml con `editar`.
  src/media también: Mp4Writer (H.264 o H.265 por hardware con respaldo x264/x265 + AAC;
  probeVideoEncoder dice cuál usaría), TimelineAudioSource (mezcla de las pistas de audio)
  y waveform.h (onda del audio, repartida en tramos que se decodifican en paralelo, y su
  caché en disco; sharkcut_bench --solo-onda la mide).
  src/app también: Editor (proyectos abiertos con su historial, pestañas de secuencias,
  herramientas, reproducción de la secuencia activa, exportación y recientes; los
  ajustes de la app van en QSettings). Interfaz: pestañas superiores Proyectos
  (qml/PaginaProyectos.qml), Editor (qml/PanelMedios.qml con qml/PanelBiblioteca.qml ·
  qml/PanelPropiedades.qml · Programa, y qml/LineaTiempo.qml) y Exportar
  (qml/PaginaExportar.qml); diálogos qml/Dialogo*.qml. Menús: qml/MenuTema.qml. Miniaturas: ThumbnailProvider ("image://miniaturas/…", en hilos).
  Fotogramas de los clips en la línea de tiempo: FrameThumbnailProvider
  ("image://fotogramas/<ruta>@<µs>", thumbnails.h; 2 hilos de baja prioridad): solo el
  primer fotograma de cada clip, a su inicio (con muchos cortes y VODs de una hora, una
  tira de miniaturas atascaba el zoom).
  Ondas: WaveformCache y WaveformView (waveforms.h), en la caché de medios; prefetch las
  prepara de antemano (al importar o abrir), de una en una y detrás de las que pide un clip.
  Proxies: ProxyManager (proxies.h). Línea de tiempo virtualizada: TimelineClipsModel
  (timelinemodel.h): con miles de clips, nada de recorrer todos desde QML; `pinned` mantiene
  cargado lo que se arrastra. Los arrastres de LineaTiempo pasan por arrastrarCon, que
  desplaza la vista cerca de los bordes y repite el movimiento.
  tools/bench: sharkcut_bench, mide decodificación, saltos, marcha atrás, proxies y
  edición de una línea de tiempo grande (sin Qt).
  Valores numéricos editables: qml/CampoNumero.qml (arrastrar o escribir). Listas
  desplegables: qml/Selector.qml (con desplazamiento); fuentes: qml/SelectorFuente.qml
  (búsqueda y favoritas, guardadas en QSettings "fuentes/favoritas").
  Iconos: qml/Icono.qml (trazos SVG de qml/Trazos.qml dibujados con QtQuick.Shapes);
  añadir los nuevos en Trazos.qml.
  AudioOutput::takePeakLevels alimenta el medidor de la mezcla.
  Rastro de la reproducción: PlaybackTrace (playbacktrace.h) apunta en el registro los
  clips que no tienen fotograma o se congelan al reproducir (con DecodeWorker::status) y
  el audio sin muestras (AudioOutput::takeMissingFrames); resumen al pausar. Red de
  seguridad: Editor::rescueStalledSlots recoloca el hilo de un clip visible sin fotograma
  cuando su hilo está parado (DecodeWorker::Status::waiting). TimelineAudioSource abre los
  archivos 2 s antes de que suenen.
  Hardware y codificadores (botón en Proyectos, qml/DialogoSistema.qml): la GPU del visor
  (CompositionSource::setRenderDevice), la decodificación y los codificadores
  (editor_hardware.cpp, probeHwDecodeDevice y probeVideoEncoder en segundo plano).
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
  Mensajes con qUtf8Printable (no qPrintable: en Windows rompía las tildes); FFmpeg solo
  escribe sus errores (quietMediaLog en media_info.h).
- Entrega: versión en CMakeLists (project VERSION) y src/core/version.h (comparar versiones).
  Updater (src/app/updater.h, qml/DialogoActualizacion.qml): al abrir consulta
  releases/latest de GitHub (SHARKCUT_UPDATE_FEED la cambia para pruebas), descarga el
  *-Setup.exe con ModelDownload (SHA-256 del `digest`) y lo ejecuta en silencio al cerrar.
  Instalador: installer/SharkCut.iss (Inno Setup, sin administrador). Icono:
  src/app/icons (tools/icono/generar_icono.py). Licencias de terceros: licenses/.
  Publicar: subir la etiqueta vX.Y.Z (igual a la versión de CMake) con
  docs/novedades/X.Y.Z.md; CI crea la versión con el instalador y el .zip (y el código de
  FFmpeg y x264 que exige la GPL: FFMPEG_SOURCE_* y X264_COMMIT en build.yml, a la par que
  cmake/FFmpeg.cmake). Firma con SignPath preparada en build.yml, desactivada mientras no
  haya variables (docs/FIRMA.md). Página de descarga: web/ (GitHub Pages,
  .github/workflows/pages.yml), con privacidad.html (= PRIVACIDAD.md), firma.html y
  roadmap.html (= docs/ROADMAP.md, la hoja de ruta, con textos para el público).
  Textos legales: MARCA.md (nombre, logo e icono fuera de la GPL), CONTRIBUTING.md (no se
  aceptan cambios de código), PRIVACIDAD.md; © 2026 KyoSumiVT. «Acerca de»:
  qml/DialogoAcercaDe.qml (botón en Proyectos). Enlace de Ko-fi: vacío por ahora
  (enlaceKofi en DialogoAcercaDe.qml y ENLACE_KOFI en web/index.html; vacío = oculto).

## Reglas
- Edición no destructiva: nunca modificar los archivos de medios originales.
  Proxies, miniaturas, ondas y remux van a la caché de medios (nunca junto a los medios).
- No copiar código ni recursos de otros productos de pago. En código, comentarios y
  documentos no se nombran otros programas de edición: se describe el comportamiento.
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
