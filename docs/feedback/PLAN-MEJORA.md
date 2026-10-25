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

### A4 · Atajos de teclado que se descubran · FB-003 · S–M
- **Problema:** se pidió Alt+C para cortar en el cabezal; ya existe Ctrl+K, pero nadie lo
  encontró.
- **Hacer:** lista de atajos visible (ventana o panel) y atajos en las ayudas de los
  botones; añadir Alt+C como segundo atajo de «Cortar en el cabezal».
- **Por decidir:** el mapa de atajos (PLAN.md lo tiene como decisión pendiente: copiar el
  de Premiere o uno propio) y si los atajos serán configurables (eso iría a la prioridad
  media).

### A5 · Alt + rueda para desplazar en vertical · FB-005 · S
- **Problema:** se pidió como función nueva, pero el código ya lo hace.
- **Hacer:** reproducirlo en Windows con el ratón de quien lo probó (zona de las
  cabeceras de pista, encima de los clips, ratones que mandan el giro en horizontal) y
  corregir donde no funcione; mencionarlo en la lista de atajos (A4).

### A6 · Ondas de audio más rápidas · FB-002 · M
- **Hacer:** primero medir con un VOD largo (tools/bench) cuánto tarda y dónde se va el
  tiempo (decodificar, calcular, guardar en caché); después optimizar lo que pese
  (p. ej. dibujar una onda aproximada mientras se calcula la completa, o repartir el
  cálculo en varios hilos).
- **Hecho cuando:** hay cifras de antes y después con el mismo VOD.

### A7 · Escuchar los sonidos antes de usarlos · FB-006 · S
- **Hacer:** en la pestaña Sonidos, un clic en el icono reproduce el sonido; otro clic lo
  detiene.

### A8 · Explicar el punto de anclaje · FB-010 · S
- **Hacer:** texto corto de ayuda en la sección (qué hace y para qué sirve: pegar algo a
  un objeto que se mueve), con pasos; valorar un nombre más claro.

### A9 · Aviso de descarga del modelo solo si hace falta · FB-017 · S
- **Hacer:** que el selector de modelo de DialogoSubtitulos diga «descargado» si el
  archivo ya está en la carpeta de modelos, y «descarga de ~X MB» solo si no.

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

### M6 · Preset de exportación para YouTube · FB-022 · S
- **Hacer:** preset «YouTube» en Exportar (1440p desde un lienzo 1080p, calidad Alta)
  con una línea que explique por qué subir en 1440p mejora el resultado.

### M7 · Ver qué aceleración por hardware se usa · FB-021 · S–M
- **Hacer:** apartado de información (GPU detectada, decodificador y codificador en uso,
  si hay respaldo por software) y, quizá, forzar el modo por software para diagnosticar.

### M8 · Logo más reconocible · FB-001 · M
- **Hacer:** rediseñar el icono para que se lea como aleta de tiburón (curva trasera
  marcada, línea de agua) y regenerarlo con tools/icono; actualizar el instalador y la
  página de descarga (web/).

### M9 · Botones de zoom arriba a la derecha · FB-019 · S
- **Hacer:** mover acercar/alejar de la línea de tiempo a su esquina superior derecha.

### M10 · Doble clic en Medios para importar · FB-024 · S
- **Hacer:** doble clic en la zona vacía del panel Medios abre el diálogo de importar (el
  doble clic sobre una carpeta o secuencia sigue abriéndola).

### M11 · Caché en una carpeta común y limpieza · FB-027 · M
- **Problema:** cada proyecto deja una carpeta `<nombre>.sharkp.cache` a su lado, y la
  carpeta de proyectos se llena de carpetas que no se sabe para qué son.
- **Hacer:** guardar la caché (ondas y proxies) en una carpeta común, como la caché de
  medios de Premiere (por defecto en los datos de la app, cambiable en ajustes), con una
  subcarpeta por proyecto; botón para ver cuánto ocupa y limpiarla. Seguir leyendo las
  carpetas `.sharkp.cache` que ya existen.
- **Cuidado:** la caché de un proyecto tiene que seguir encontrándose si el proyecto se
  mueve o se renombra, y no mezclarse si dos proyectos se llaman igual.

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

---

## Resumen

| Prioridad | Puntos | Entradas |
|-----------|--------|----------|
| Alta | A1–A13 | FB-002, 003, 004, 005, 006, 010, 012, 017, 020, 023, 025, 026, 028 |
| Media | M1–M11 | FB-001, 007, 008, 009, 013, 014, 015, 019, 020, 021, 022, 024, 027 |
| Baja | B1–B3 | FB-011, 016, 018 |
