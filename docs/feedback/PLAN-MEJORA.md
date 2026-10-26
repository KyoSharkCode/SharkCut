# Plan de mejora tras la primera ronda de pruebas (0.1.0)

Organiza el feedback de [FEEDBACK.md](FEEDBACK.md) (FB-001 a FB-022, recogido el
2026-10-22 de Kyo y Miyon) en tres prioridades. Cada punto indica las entradas que cubre,
qué hay que hacer y un esfuerzo estimado:

- **S:** horas, un cambio pequeño y localizado.
- **M:** uno o dos días.
- **L:** varios días o toca varias partes de la app.

Criterio de prioridad:

- **Alta:** errores, cosas que hoy confunden o parecen rotas, y mejoras rápidas que se
  notan en el uso diario. Objetivo: la próxima actualización (0.1.1).
- **Media:** funciones nuevas que piden varias personas o que encajan en el flujo de
  edición actual. Objetivo: 0.2.0.
- **Baja:** cambios grandes de estructura o comodidades que pueden esperar. Más adelante,
  según cómo vaya el resto.

Antes de empezar cada punto, comprobar que encaja en las fases de [PLAN.md](../PLAN.md)
(sección 11). Los puntos que piden una decisión lo dicen en «Por decidir».

---

## Prioridad alta (0.1.1)

### A1 · Imágenes PNG y JPG en la línea de tiempo · FB-012 · L
- **Problema:** el PNG sale con duración 0:00 y no llega a la línea de tiempo; el JPG llega
  pero no se ve en el visor y no se puede alargar.
- **Hacer:** soporte de imágenes fijas como medio propio: duración por defecto ajustable,
  se puede alargar como cualquier clip, PNG con transparencia (ya previsto en el alcance
  de PLAN.md: «Overlays e imágenes»). Añadir imágenes al filtro del diálogo de importar.
- **Hecho cuando:** un PNG con alfa y un JPG se importan, se ven en el visor y en la
  exportación, y se pueden alargar o acortar.
- **Hecho:** medio «imagen fija» (`MediaItem::still`, archivo .sharkp versión 10). FFmpeg
  las abre como `*_pipe`/`image2`, sin duración y sin poder saltar dentro: por eso el PNG
  salía con 0:00 y el JPG no se veía. VideoDecoder las lee una vez y las entrega como
  fotograma 0; sus clips se alargan sin límite hacia los dos lados (`in` puede ser
  negativo). Duración al importarlas: 5 s, cambiable en Propiedades («Usar para las
  imágenes nuevas»), donde también está la duración del clip. No deciden el lienzo ni los
  fps de la secuencia y no tienen proxy. Filtro «Imágenes» en el diálogo de importar.

### A2 · La transición y su sonido se mueven juntos · FB-004 · M
- **Problema:** al mover una transición, su efecto de sonido se queda donde estaba.
- **Hacer:** enlazar el clip de la transición con su sonido (como el video y el audio de un
  medio), para moverlos, recortarlos y borrarlos juntos; que se puedan desenlazar.
- **Hecho cuando:** arrastrar la transición mueve también su sonido, con deshacer/rehacer.
- **Hecho:** usa el enlace de los clips (`Clip::link`), que ahora también une una
  transición-clip con un clip de audio sin compartir tramo (`linkEffectSound`). Se mueven,
  se copian y se quitan juntos; al recortar cada uno conserva su duración. «Desvincular la
  transición y su sonido» en el menú del clip y en Propiedades. Las transiciones de
  proyectos anteriores no se enlazan solas.

### A3 · Botón «Proxies» que se entienda · FB-020 (primera parte) · S
- **Problema:** parece que a veces no funciona: está desactivado sin guardar el proyecto y,
  activado, no cambia nada si no hay proxies creados.
- **Hacer:** al activarlo con medios sin proxy, ofrecer crearlos (o crearlos solo);
  explicar en la ayuda del botón por qué está desactivado; indicar cuántos medios
  tienen proxy.
- **Hecho cuando:** activar «Proxies» siempre tiene un efecto visible o explica por qué no.
- **Hecho:** sin proyecto guardado el botón no se desactiva (así su ayuda explica por qué) y
  un clic abre «Guardar». Mientras estén activados se crean solos los que falten (al
  activarlos, al abrir un proyecto y al importar videos; cancelar lo detiene hasta volver a
  activarlos). El botón y la etiqueta del visor dicen cuántos videos tienen proxy
  («Proxies 2/3»).

### A4 · Atajos visibles al pasar el ratón · FB-003 · S
- **Problema:** se pidió Alt+C para cortar en el cabezal; ya existe Ctrl+K, pero nadie lo
  encontró porque los atajos no se ven en ninguna parte.
- **Decidido con Kyo (2026-10-26):** el mapa de atajos se queda como está (ya sigue el
  estilo de los editores habituales: Ctrl+K, Q/W, J/K/L, V/C…) y no se añade Alt+C. Los
  atajos configurables quedan fuera.
- **Hacer:** mostrar el atajo en la ayuda que sale al pasar el ratón por cada botón o
  herramienta que lo tenga (p. ej. «Cuchilla (C)», «Cortar en el cabezal (Ctrl+K)»). En
  los textos no se nombran otros productos.
- **Hecho cuando:** todos los botones con atajo lo muestran en su ayuda.
- **Hecho:** repasados todos los botones y menús con atajo. Ya lo decían las herramientas
  (V, C, S), Dividir (Ctrl+K), Quitar (Supr), Deshacer/Rehacer, Inicio/Fin, Reproducir
  (Espacio), Importar (Ctrl+I), Nuevo/Abrir/Guardar y Exportar rápido (Ctrl+E). Se añadió
  en las pestañas Proyectos/Editor/Exportar (1, 2, 3 o Ctrl+M, con ayuda nueva en
  `Pestana.qml`), los botones de fotograma anterior/siguiente (Mayús+←/→ y ↑/↓ entre
  cortes), Reproducir (J/K/L), los botones de zoom (-, +, Ctrl + rueda, \\, Alt + rueda),
  la ayuda de los marcadores (M, Mayús+M, Ctrl+Mayús+M) y el menú del clip («Dividir en el
  cabezal (Ctrl+K)», «Quitar, dejando el hueco (Supr)»). No se añadió Alt+C y ningún texto
  nombra otros productos.

### A5 · Alt + rueda para desplazar en vertical · FB-005 · S
- **Problema:** se pidió como función nueva, pero el código ya lo hace.
- **Hacer:** reproducirlo en Windows con el ratón de quien lo probó (zona de las
  cabeceras de pista, encima de los clips, ratones que mandan el giro en horizontal) y
  corregir donde no funcione; mencionarlo en la lista de atajos (A4).
- **Hecho:** reproducido también en Linux: con Alt, Qt cambia el eje del giro (la rueda
  normal llega como horizontal) y el `WheelHandler` de la línea de tiempo solo atendía la
  vertical, así que Alt + rueda no llegaba en ninguna zona (solo funcionaba Alt + la rueda
  horizontal). Ahora hay un manejador por orientación con la misma función (`rueda`), y el
  desplazamiento horizontal de los touchpads también llega. Comprobado con Xvfb encima de los
  clips, en las cabeceras de pista y en la regla, hacia arriba y hacia abajo, con la rueda
  normal y con la horizontal. Alt + rueda se menciona en la ayuda de los botones de zoom.

### A6 · Ondas de audio más rápidas · FB-002 · M
- **Hacer:** primero medir con un VOD largo (tools/bench) cuánto tarda y dónde se va el
  tiempo (decodificar, calcular, guardar en caché); después optimizar lo que pese
  (p. ej. dibujar una onda aproximada mientras se calcula la completa, o repartir el
  cálculo en varios hilos).
- **Hecho cuando:** hay cifras de antes y después con el mismo VOD.
- **Hecho:** medido con `sharkcut_bench <video> --solo-onda` (compilación Release, Linux, Xeon de 4 núcleos a 2,1 GHz) con un video de prueba de 1 h generado con FFmpeg (1280×720 a 30 fps en H.264, 1,3 GB, audio AAC estéreo a 48 kHz), con el archivo en la caché del sistema. **Antes: 2,6 s** (mediana de 6; entre 2,5 y 3,4 s). Dónde se iba el tiempo: decodificar el AAC ≈ 1,7 s (65 %, en un solo hilo), leer el contenedor ≈ 0,3 s (con el audio intercalado fotograma a fotograma hay que leer el archivo entero) y el resto en nuestro código (una reserva y puesta a cero de memoria por trama en AudioDecoder y el bucle de picos muestra a muestra); la conversión a 48 kHz no pesa. Cambios: computeWaveform reparte el audio en tramos (uno por minuto, como mucho 4 y no más que núcleos) que se decodifican a la vez en hilos y se juntan en orden, cada uno empezando 0,1 s antes para que el decodificador se asiente (la onda sale idéntica bloque a bloque: comprobado con el VOD de 1 h y en el test); AudioDecoder reutiliza su memoria de conversión, y los picos se calculan por bloques. **Después: 0,63 s** (mediana de 6; entre 0,60 y 0,69 s), unas 4 veces menos; en un solo tramo, 2,4 s. Con un disco duro mecánico lo que manda es leer el archivo, y eso no cambia.

### A7 · Escuchar los sonidos antes de usarlos · FB-006 · S
- **Hacer:** en la pestaña Sonidos, un clic en el icono reproduce el sonido; otro clic lo
  detiene.
- **Hecho:** cada sonido de la pestaña Sonidos tiene un botón ▶ (■ mientras suena): un
  clic lo escucha y otro lo para; al acabar vuelve a ▶. `SoundPreview` (src/audio, sin Qt)
  tiene su propia salida de audio, aparte de la secuencia, y decodifica el efecto entero con
  FFmpeg (miniaudio sigue compilado solo con la API de dispositivos). El editor lo carga en
  un hilo aparte (`Editor::toggleSoundPreview`). Test con la salida muda; comprobado con
  Xvfb.

### A8 · Explicar el punto de anclaje · FB-010 · S
- **Hacer:** texto corto de ayuda en la sección (qué hace y para qué sirve: pegar algo a
  un objeto que se mueve), con pasos; valorar un nombre más claro.
- **Hecho:** debajo de «Punto de anclaje», una frase dice qué hace y para qué sirve (pegar el
  clip a algo que se mueve en otro video: un texto, un emoji o un desenfoque que sigan a
  alguien). El botón ⓘ despliega los pasos (pista por encima, cabezal donde se vea, «Seguir
  en» y «Marcar lo que se sigue», recuadro en el visor) y qué se obtiene (keyframes de
  posición retocables). Se mantiene el nombre: la frase ya lo explica y el título largo no
  cabía en el panel.

### A9 · Aviso de descarga del modelo solo si hace falta · FB-017 · S
- **Hacer:** que el selector de modelo de DialogoSubtitulos diga «descargado» si el
  archivo ya está en la carpeta de modelos, y «descarga de ~X MB» solo si no.
- **Hecho:** `isSpeechModelDownloaded` (src/speech, sin Qt: el archivo existe y mide lo que
  debe) y `Editor::checkSpeechModels`, que lo mira en otro hilo al abrir el diálogo y tras
  descargar un modelo (`speechModels`). El selector dice «descargado» o «descarga de ~X MB»
  según el caso. Comprobado con Xvfb con un modelo rápido presente y el alto sin
  descargar.

### A10 · Cortar sin tirón mientras se reproduce · FB-023 · M
- **Problema:** al cortar con la reproducción en marcha se nota un pequeño tirón.
- **Causa probable:** al dividir cambian los clips, y `Editor::onProjectChanged` lo trata
  como un cambio de lo que hay que leer: pausa, reinicia todos los hilos de video
  (`resetVideoSlots`), reinicia el audio (`restartAudio`), vuelve a buscar y reanuda.
- **Hacer:** reconocer las ediciones que no cambian lo que se ve ni lo que se oye (dividir
  un clip deja el mismo medio en los mismos fotogramas) y no reiniciar nada; en el resto,
  reiniciar solo los decodificadores y el audio de lo que cambió.
- **Hecho cuando:** cortar con Ctrl+K o la cuchilla durante la reproducción no detiene ni
  hace saltar la imagen ni el sonido.
- **Hecho:** `sameVideo`/`sameAudio` (editor.cpp) comparaban los clips por id, y dividir crea
  uno nuevo: cada corte pausaba, reiniciaba todos los hilos de video y el audio y volvía
  a buscar. Ahora comparan las pistas «como se leen» (`continuousClips` en project.h: las
  dos mitades de un corte cuentan como el clip entero), así que dividir no reinicia nada.
  La mitad nueva hereda el fotograma que se ve y el hilo que la decodificaba
  (`Editor::continueSlot`, `DecodeWorker::continuesAfter`), sin buscar. Comprobado con Xvfb
  (un aviso temporal en el registro): 4 cortes con Ctrl+K durante la reproducción, en 3
  rondas, sin ningún reinicio ni búsqueda. Otras ediciones que sí cambian lo que se lee
  (mover o recortar) siguen reiniciando lo necesario.

### A11 · Seleccionar clips muy cortos con la línea de tiempo alejada · FB-025 · M
- **Problema:** con la línea de tiempo muy alejada, los cortes pequeños no se pueden
  seleccionar.
- **Causa probable:** por rendimiento, `TimelineClipsModel` junta los clips de menos de
  16 px (`kMinClipPixels`) en un «bloque», y los bloques no se pueden seleccionar ni
  arrastrar.
- **Hacer:** que un clic en un bloque haga algo útil (seleccionar el clip que hay bajo el
  ratón, o todos los del bloque, o acercar el zoom a esa zona) y que se vea que es un
  grupo de clips.
- **Hecho cuando:** con la línea de tiempo alejada, un clic sobre clips cortos siempre
  selecciona algo o acerca el zoom, nunca se ignora.
- **Hecho:** los bloques (`TimelineClipsModel`) ya responden. Un clic selecciona el clip que
  hay debajo o, si cae en un hueco del bloque, el más cercano a 16 px
  (`Project::nearestClip`, `Editor::clipNear`). Mayús/Ctrl+clic lo suma a la selección, el
  clic derecho abre su menú y con la cuchilla se corta. Un doble clic acerca el zoom alrededor
  del ratón hasta que ese clip mida unos 40 px. Los clips seleccionados se marcan sobre el
  bloque, que dice cuántos clips tiene («×12», o el texto completo si cabe) y lo explica al
  pasar el ratón. Los bloques de subtítulos funcionan igual (`Editor::captionNear`).
  Comprobado con Xvfb con 150 clips de 4 fotogramas en 60 s.

### A12 · No se puede arrastrar un medio recién importado · FB-026 · M
- **Problema:** tras importar con la ventana de importar, el medio nuevo no se deja
  arrastrar a la línea de tiempo; la app parece quedarse atascada.
- **Causa probable:** con «Proxies» activado, la creación automática del proxy del medio
  nuevo avisa del progreso cada 30 fotogramas, y cada aviso reconstruye toda la lista de
  Medios (`ProxyManager::changed` → `Editor::libraryChanged` → `mediaItems`): el panel se
  vuelve inutilizable (el desplazamiento vuelve arriba, el arrastre se corta) hasta que
  el proxy termina.
- **Hacer:** que el progreso de un proxy actualice solo su etiqueta («PROXY 40 %») sin
  reconstruir la lista (señal o modelo aparte), y limitar los avisos (p. ej. como mucho
  unas pocas veces por segundo). Confirmar antes en Windows: importar un video con
  «Proxies» activado e intentar desplazarse y arrastrar mientras se crea.
- **Hecho cuando:** un medio recién importado se arrastra a la línea de tiempo a la
  primera, también mientras se crea su proxy, y la lista de Medios no salta mientras se
  crean proxies.
- **Hecho:** confirmado con la 0.1.1: con 15 proxies creándose solos, la lista de Medios se
  reconstruyó 79 veces en unos 2 s. El estado de los proxies va ahora aparte
  (`Editor::proxyStates`, solo cambia la etiqueta de cada medio), el progreso se anuncia como
  mucho 4 veces por segundo y `libraryChanged` (medios, carpetas y secuencias) solo llega si
  cambió lo que se muestra. Además, cuando la lista sí se reconstruye (p. ej. cambia la
  duración de una secuencia al editar), conserva su desplazamiento. Comprobado con Xvfb:
  importar un video de 1 min con «Proxies» activado, desplazarse y arrastrarlo a la línea de
  tiempo mientras se crea su proxy.

### A13 · La línea de tiempo sigue al cabezal al saltar entre cortes · FB-028 · S
- **Problema:** al saltar entre cortes con las flechas, si el corte siguiente queda fuera
  de la vista, la línea de tiempo no se mueve y el cabezal desaparece.
- **Causa:** la vista solo sigue al cabezal durante la reproducción
  (`onCurrentFrameChanged` en `LineaTiempo.qml` sale si no se está reproduciendo).
- **Hacer:** que también lo siga cuando el cabezal se mueve con el teclado (↑/↓ entre
  cortes, ←/→ por fotogramas, Inicio/Fin, marcadores) y quede fuera de la vista; sin
  mover la vista si el cabezal ya se ve, ni cuando se mueve haciendo clic o arrastrando
  en la regla.
- **Hecho cuando:** con el zoom puesto, recorrer los cortes con ↑/↓ mantiene siempre el
  cabezal a la vista.
- **Hecho:** `LineaTiempo.mostrarCabezal()` centra la vista en el cabezal si quedó fuera; lo
  llaman los atajos (←/→, Mayús+←/→, ↑/↓, Inicio/Fin, Mayús+M y Ctrl+Mayús+M) y los
  botones de transporte del visor (`ventana.navegar` en Main.qml). Si el cabezal ya se
  ve, la vista no se mueve; los clics y arrastres en la regla no la llaman. Comprobado con
  Xvfb: con el zoom puesto, ↓ repetido mantiene siempre el cabezal a la vista.

### A14 · El cabezal puede ir hasta el final exacto de la secuencia · FB-030 · S–M
- **Problema:** el cabezal no llega al final de los clips: se queda en el último fotograma
  (p. ej. 00:01:52:14 con una duración de 00:01:52:15), un fotograma antes del borde.
- **Causa:** `Editor::seek` limita el cabezal a `frameCount() - 1`, igual que Fin, el botón
  de ir al final y el final de la reproducción.
- **Hacer:** permitir el cabezal en `frameCount()` (justo después del último fotograma),
  como en los editores habituales: el visor muestra el lienzo vacío (negro) y Fin, el botón
  de ir al final y el imán con el final de los clips lo llevan ahí. Revisar lo que usa el
  cabezal: pegar (Ctrl+V), poner sonidos en el cabezal, dividir (en el final no divide
  nada), Q/W, marcadores, reproducir desde el final (vuelve al principio o no hace nada) y
  la exportación por tramo.
- **Hecho cuando:** el cabezal se puede dejar justo en el borde final de los clips y pegar
  ahí deja el clip a continuación, sin pisar el último fotograma.
- **Hecho:** `Editor::seek` deja el cabezal en `frameCount()` (justo después del último
  fotograma), y ahí el visor muestra el lienzo vacío. Fin y el botón de ir al final lo
  llevan ahí, y el imán y ↓ con el final de los clips también. La reproducción se para ahí y
  vuelve a empezar desde el principio si se reproduce desde el final; J/L llegan hasta el
  final, y el control de posición de Exportar y «hasta el cabezal» también. Lo que usa el
  cabezal en el final: pegar lo deja a continuación, dividir y W no hacen nada, y sonidos,
  marcadores y transiciones se ponen ahí. Test del núcleo; comprobado con Xvfb (Fin y
  Ctrl+V en el final).

---

## Prioridad media (0.2.0)

### M1 · Transiciones de entrada y salida, marca de centro e imán · FB-007, FB-008 · L
- **Pedido:** mantener las transiciones como clips en su pista y, además, poder ponerlas
  al principio o al final de un clip para que se adapten (fundido a negro de entrada:
  empieza en negro y aparece el clip; de salida: se ve el clip y pasa a negro). Que la
  transición muestre el centro exacto del corte, que ese centro tenga imán con los cortes
  y que sus dos mitades se puedan separar (una mitad como cierre, la otra como apertura).
- **Hacer:** diseñar juntos los dos pedidos: las mitades separables y las transiciones de
  entrada/salida son la misma idea. Marca de centro e imán al mover una transición.
- **Por decidir:** con Kyo, la forma final (transición partida en dos mitades frente a
  tres variantes: entrada, salida y corte).

### M2 · Editar una secuencia ya creada y guardar presets · FB-015 · M
- **Hacer:** cambiar lienzo (relación de aspecto, tamaño libre) y fps de una secuencia
  existente; guardar la configuración como preset propio (QSettings, como los ajustes
  de clips).
- **Cuidado:** cambiar fps mueve los tiempos de los clips: definir cómo se reajustan.

### M3 · Tinte de color y selector de color completo · FB-013, FB-014 · M
- **Hacer:** efecto de color (como blanco y negro, pero teñido con un color elegido);
  selector de color con toda la gama al pulsar cualquier muestra de color (efectos,
  pantalla verde, subtítulos).
- **Nota:** gradación profesional queda fuera del alcance (PLAN.md); un tinte simple sí
  encaja.

### M4 · Pistas de audio de un mismo archivo (MKV/OBS) · FB-009 · L
- **Hacer:** detectar las pistas de audio de un medio y mostrarlas en Propiedades
  (sección Video · Audio, con cada pista por separado); poder elegir cuáles usar, ajustar
  cada una y llevarlas a pistas separadas de la línea de tiempo.
- **Por decidir:** PLAN.md asume grabaciones de OBS con una sola pista de audio; esto
  cambia esa decisión.

### M5 · Resolución de reproducción del visor · FB-020 (segunda parte) · M
- **Hacer:** selector Completa · 1/2 · 1/4 en el visor (decodifica y dibuja más pequeño,
  sin crear archivos), independiente de los proxies.

### M6 · Presets de exportación para YouTube y TikTok · FB-022, FB-033 · S
- **Hacer:** presets en Exportar con una línea que explique cada uno:
  - «YouTube»: 1440p desde un lienzo 1080p, calidad Alta (subir en 1440p mejora el
    resultado tras la recompresión).
  - «TikTok»: vertical 1080×1920, H.264, calidad Alta, a los fps de la secuencia (30 o 60).
    Con una secuencia horizontal, avisar de que no es vertical (no recortar por su cuenta).
- **Hecho:** «Preparar para» en Exportar, con los botones YouTube y TikTok. Aplican sus
  ajustes (`exportPreset` en export_settings.h, con tests; `Editor::exportPreset` añade los
  textos) y una línea explica cada uno mientras no se cambien a mano:
  - YouTube: la escala ofrecida más pequeña que llega a 1440 líneas (desde 1080p, el 133 %:
    2560×1440), H.264 y calidad Alta, con la explicación de la recompresión. Con un lienzo de
    1440p o más, a su tamaño.
  - TikTok: el tamaño con 1080 de ancho (1080×1920 desde un lienzo vertical), H.264, calidad
    Alta, a los fps de la secuencia si son 30 o 60 (si no, el más cercano de los dos). Con
    una secuencia horizontal avisa (en color de aviso) y la exporta a su tamaño, sin recortar.
  Además se arregló `Selector.qml`: al cambiar su lista volvía al primer elemento sin mirar
  `indice` (Exportar mostraba «200 %» al abrir aunque se fuera a exportar al 100 %).

### M7 · Ver qué aceleración por hardware se usa · FB-021 · S–M
- **Hacer:** apartado de información (GPU detectada, decodificador y codificador en uso,
  si hay respaldo por software) y, quizá, forzar el modo por software para diagnosticar.
- **Hecho:** botón «Hardware y codificadores» en Proyectos (qml/DialogoSistema.qml): la GPU con la que dibuja el visor (QRhi driverInfo), la decodificación (por la GPU en uso, la que se podría usar, o el respaldo por software) y el codificador que usaría la exportación en H.264 y H.265 (probeVideoEncoder, en segundo plano), con lo que va por software en amarillo. Al terminar una exportación por software, el mensaje lo dice.

### M8 · Logo más reconocible · FB-001 · M
- **Hacer:** rediseñar el icono para que se lea como aleta de tiburón (curva trasera
  marcada, línea de agua) y regenerarlo con tools/icono; actualizar el instalador y la
  página de descarga (web/).

### M9 · Botones de zoom arriba a la derecha · FB-019 · S
- **Hacer:** mover acercar/alejar de la línea de tiempo a su esquina superior derecha.
- **Hecho:** alejar, acercar y «Ver todo» pasan de abajo de la columna de herramientas a la
  esquina superior derecha de la línea de tiempo, en la fila de las pestañas de secuencias.
  Sus ayudas dicen los atajos (-, + o Ctrl + rueda, \\) y cómo se mueve la rueda (Alt +
  rueda: entre pistas).

### M10 · Doble clic en Medios para importar · FB-024 · S
- **Hacer:** doble clic en la zona vacía del panel Medios abre el diálogo de importar (el
  doble clic sobre una carpeta o secuencia sigue abriéndola).
- **Hecho:** un `TapHandler` en la lista y en la cuadrícula de Medios abre «Importar» con un
  doble clic en un hueco; sobre una carpeta, secuencia o medio sigue haciendo lo suyo (sus
  zonas se quedan el clic). Los textos de ayuda del panel lo mencionan. Comprobado con Xvfb.

### M11 · Caché en una carpeta común y limpieza · FB-027 · M
- **Problema:** cada proyecto deja una carpeta `<nombre>.sharkp.cache` a su lado, y la
  carpeta de proyectos se llena de carpetas que no se sabe para qué son.
- **Hacer:** guardar la caché (ondas y proxies) en una carpeta común de caché de medios
  (por defecto en los datos de la app, cambiable en ajustes), con una
  subcarpeta por proyecto; botón para ver cuánto ocupa y limpiarla. Seguir leyendo las
  carpetas `.sharkp.cache` que ya existen.
- **Cuidado:** la caché de un proyecto tiene que seguir encontrándose si el proyecto se
  mueve o se renombra, y no mezclarse si dos proyectos se llaman igual.

### M12 · Poner una transición en el cabezal con un clic · FB-031 · S
- **Hacer:** en la pestaña Transiciones, un clic en el botón de la flecha de cada
  transición la pone en la línea de tiempo centrada en el cabezal, con su sonido, como el
  botón + de los sonidos (`addSoundAtPlayhead`). Arrastrarla sigue funcionando igual.
- **Decidido con Kyo (2026-10-26):** se mantienen las dos formas. Con un clic, la
  transición se centra exactamente en el cabezal (su punto medio, el del cambio, queda en
  el cabezal), haya un corte ahí o no. Arrastrándola, sigue como hoy: se centra en el corte
  más cercano al punto donde se suelta.
- **Hecho:** el cuadrado de la flecha de cada transición es ahora un botón: un clic llama a
  `Editor::addEffectClipAtPlayhead`, que la pone desde V1 hacia arriba (en la primera pista
  libre) con su punto medio en el cabezal (`effectStartCenteredAt`) y su sonido enlazado
  con el golpe ahí. La colocación y el sonido se comparten con el arrastre
  (`placeEffectWithSound`), que sigue centrándose en el corte más cercano. Test del núcleo;
  comprobado con Xvfb con el cabezal a mitad de un clip.

### M13 · Cabezal con forma de aleta de tiburón · FB-032 · S
- **Hacer:** cambiar el tirador del cabezal (el cuadrito de la regla) por una aleta de
  tiburón, con el color del cabezal del tema (`Theme.cabezal`) y el mismo tamaño para
  agarrarlo. Icono nuevo en `Trazos.qml`.
- **Nota:** conviene hacerlo con el logo nuevo (M8) para que la aleta sea la misma.

### M14 · Propiedades con una pestaña por efecto · FB-036 · M
- **Hacer:** organizar Propiedades en pestañas: la de base (posición, escala, rotación,
  opacidad, recorte) y una pestaña por cada efecto aplicado al clip (p. ej. al aplicar la
  pantalla verde aparece la pestaña «Croma»). Al quitar el efecto, su pestaña desaparece.
- **Nota:** es la base de M15 y M16 (las máscaras serán un efecto con su pestaña).

### M15 · Línea de tiempo de keyframes en Propiedades · FB-035 · L
- **Hacer:** dar más ancho al panel Propiedades y usar el espacio extra para una pequeña
  línea de tiempo del clip seleccionado, al lado de cada valor, que muestre sus keyframes
  (◆) y permita moverlos arrastrando, con el cabezal sincronizado.
- **Decidido con Kyo (2026-10-26):** el panel ocupa desde donde empieza hoy hasta donde está
  ahora el botón «Proxies» del visor (aprox. el doble de ancho; el visor se estrecha). Su
  borde se puede arrastrar para ensancharlo o estrecharlo, y el ancho se recuerda
  (adelanta una parte de B1, paneles reorganizables).
- **Depende de:** M14 (cada pestaña tendría su propia franja de keyframes).

### M17 · Altura de las pistas ajustable · FB-037 · M
- **Hacer:** que cada pista de la línea de tiempo se pueda hacer más alta o más baja:
  arrastrando el borde inferior de su cabecera, y con la rueda y un modificador con el
  ratón sobre la cabecera (Ctrl + rueda ya es el zoom horizontal; usar otro, p. ej.
  Mayús + rueda, o dejar Ctrl + rueda solo sobre las cabeceras). Atajo para hacer
  todas las pistas más altas o más bajas. La altura se guarda en el proyecto.
- **Nota:** hoy todas las pistas miden 50 px fijos (`alturaPista` en `LineaTiempo.qml`);
  con más altura se ven mejor las ondas y las miniaturas.

### M16 · Máscaras con formas simples · FB-034 · M–L
- **Hacer:** nuevo efecto «Máscara» con formas clásicas (rectángulo, elipse/círculo y
  quizá estrella y corazón): posición, tamaño, rotación, borde suave (difuminado),
  invertir y opacidad, animables con keyframes. Al aplicarlo se crea su pestaña
  «Máscaras» en Propiedades (M14) y la forma se puede ajustar en el visor.
- **Fuera por ahora:** dibujar formas a mano (pluma) y que la máscara siga sola a un
  objeto (se podría apoyar después en el punto de anclaje).
- **Nota:** el plan original ya proponía «máscaras simples» como efecto. Se dibuja en
  layer.frag, como las esquinas redondeadas (distancia a una forma).

### M18 · Velocidad de los clips (acelerar y ralentizar) · FB-038 · L
- **Hacer:** velocidad por clip (p. ej. 25 %, 50 %, 200 %, 400 % o un valor libre) en
  Propiedades, que cambia la duración del clip en la línea de tiempo. El audio se
  acelera o ralentiza con él, con opción de conservar el tono (que la voz no suene aguda
  o grave). Poner el clip en marcha atrás puede venir después.
- **Cuidado:** hoy un clip muestra el medio fotograma a fotograma (`in`, `length`,
  `Clip::mediaFrameAt`). Cambiar la velocidad toca la posición en el medio, los
  keyframes, los recortes y las divisiones, las transiciones, la mezcla de audio, las ondas,
  la reproducción, la exportación y el archivo .sharkp (nueva versión). Ralentizar repite
  fotogramas (sin interpolar por ahora).
- **Por decidir:** si además hay una herramienta para estirar el clip arrastrando su borde
  y que cambie la velocidad, y si la velocidad puede variar dentro del clip (rampas).

---

## Prioridad baja (más adelante)

### B1 · Paneles reorganizables y diseños guardados · FB-016 · L
- **Hacer:** paneles que se puedan cambiar de tamaño y de sitio; barra de menús clásica
  (Archivo, Editar, Ventana…) con «Diseño predeterminado» y «Guardar diseño».
- **Nota:** es un cambio grande de la interfaz; conviene hacerlo de una vez cuando la
  lista de paneles esté estable.

### B2 · Carpetas vinculadas entre proyectos · FB-011 · M
- **Hacer:** botón + en Medios/Transiciones/Efectos para vincular carpetas de uso
  frecuente, guardadas para todos los proyectos; avisar si una carpeta ya no está.

### B3 · Descargas opcionales en el instalador · FB-018 · M
- **Hacer:** casilla en el instalador para descargar el modelo de transcripción (y
  futuras descargas), con la misma comprobación SHA-256.
- **Nota:** PLAN.md decidió no meter los modelos en el instalador; una descarga opcional
  durante la instalación es compatible con eso.

### B4 · Apartado de tutorial · FB-029 · L
- **Hacer:** un apartado de tutorial dentro de la app para quien empieza: lo básico
  (importar, cortar, transiciones, exportar), las funciones menos evidentes (punto de
  anclaje, proxies, subtítulos) y la lista de atajos.
- **Nota:** recoge lo que piden A8 (punto de anclaje confuso) y A4 (atajos que no se
  ven) a mayor escala; se diseña con Kyo cuando toque.

---

## Resumen

| Prioridad | Puntos | Entradas |
|-----------|--------|----------|
| Alta | A1–A14 | FB-002, 003, 004, 005, 006, 010, 012, 017, 020, 023, 025, 026, 028, 030 |
| Media | M1–M18 | FB-001, 007, 008, 009, 013, 014, 015, 019, 020, 021, 022, 024, 027, 031, 032, 033, 034, 035, 036, 037, 038 |
| Baja | B1–B4 | FB-011, 016, 018, 029 |
