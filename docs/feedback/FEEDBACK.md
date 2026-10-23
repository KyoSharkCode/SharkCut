# Registro de feedback

Formato y estados: ver [README.md](README.md).

| ID | Fecha | Área | Tipo | Resumen | Estado |
|----|-------|------|------|---------|--------|
| [FB-001](#fb-001) | 2026-10-22 | Marca/logo | Usabilidad | El logo parece un pincho de Geometry Dash, no una aleta | En el plan |
| [FB-002](#fb-002) | 2026-10-22 | Rendimiento | Propuesta | Ondas más rápidas | En el plan |
| [FB-003](#fb-003) | 2026-10-22 | Línea de tiempo | Propuesta | Atajo Alt+C para cortar en el cabezal | En el plan |
| [FB-004](#fb-004) | 2026-10-22 | Efectos | Error | Transición y su sonido no se mueven juntos | Hecho |
| [FB-005](#fb-005) | 2026-10-22 | Línea de tiempo | Propuesta | Alt + rueda para desplazar en vertical | En el plan |
| [FB-006](#fb-006) | 2026-10-22 | Medios | Propuesta | Vista previa de sonidos con un clic | En el plan |
| [FB-007](#fb-007) | 2026-10-22 | Efectos | Propuesta | Transiciones dentro del clip (entrada/salida) | En el plan |
| [FB-008](#fb-008) | 2026-10-22 | Efectos | Propuesta | Marca de centro, imán y mitades separables | En el plan |
| [FB-009](#fb-009) | 2026-10-22 | Propiedades | Propuesta | Pistas de audio múltiples (MKV) por separado | En el plan |
| [FB-010](#fb-010) | 2026-10-22 | Propiedades | Usabilidad | Punto de anclaje confuso | En el plan |
| [FB-011](#fb-011) | 2026-10-22 | Medios | Propuesta | Carpetas vinculadas entre proyectos | En el plan |
| [FB-012](#fb-012) | 2026-10-22 | Medios | Error | PNG/JPG: no se ven ni se puede alargar | Hecho |
| [FB-013](#fb-013) | 2026-10-22 | Efectos | Propuesta | Efecto de tinte de color | En el plan |
| [FB-014](#fb-014) | 2026-10-22 | Propiedades | Propuesta | Selector de color completo | En el plan |
| [FB-015](#fb-015) | 2026-10-22 | Proyectos | Propuesta | Editar secuencia y guardar presets | En el plan |
| [FB-016](#fb-016) | 2026-10-22 | Otro | Propuesta | Paneles reorganizables y diseños | En el plan |
| [FB-017](#fb-017) | 2026-10-22 | Subtítulos | Usabilidad | Ocultar aviso de descarga si ya existe | En el plan |
| [FB-018](#fb-018) | 2026-10-22 | Instalación | Propuesta | Descargas opcionales en el instalador | En el plan |
| [FB-019](#fb-019) | 2026-10-22 | Línea de tiempo | Usabilidad | Zoom arriba a la derecha | En el plan |
| [FB-020](#fb-020) | 2026-10-22 | Visor | Usabilidad | Botón Proxies que «a veces no sirve» y calidad de vista previa elegible | En el plan |
| [FB-021](#fb-021) | 2026-10-22 | Rendimiento | Propuesta | Ver y configurar la aceleración por hardware | En el plan |
| [FB-022](#fb-022) | 2026-10-22 | Exportación | Pregunta | ¿El códec de exportación aguanta la recompresión de YouTube? | En el plan |
| [FB-023](#fb-023) | 2026-10-23 | Línea de tiempo | Error | Tirón al cortar durante la reproducción | En el plan |
| [FB-024](#fb-024) | 2026-10-23 | Medios | Propuesta | Doble clic en Medios abre Importar | En el plan |
| [FB-025](#fb-025) | 2026-10-23 | Línea de tiempo | Error | Clips muy cortos no se seleccionan con la línea alejada | En el plan |
| [FB-026](#fb-026) | 2026-10-23 | Medios | Error | Un medio recién importado no se deja arrastrar | En el plan |

---

<a id="fb-001"></a>
### FB-001 · El logo no se reconoce como una aleta de tiburón
- **Fecha:** 2026-10-22
- **Quién:** amigo/a tester (por chat)
- **Versión:** 0.1.0 (logo actual)
- **Área:** Marca/logo
- **Tipo:** Usabilidad
- **Qué dijo:** «parece un pincho del Geometría Dinámica» (Geometry Dash).
- **Contexto:** vio el icono de la app; Kyo tuvo que aclararle que es la aleta dorsal de
  un tiburón.
- **Notas:** el icono actual (`src/app/icons/sharkcut.svg`, generado con
  `tools/icono/generar_icono.py`) es un trazo triangular casi simétrico sobre una línea
  horizontal. Sin la curva trasera marcada ni referencia al agua, se lee como un pico o
  un triángulo. Afecta también al icono del instalador y a la página de descarga (`web/`).
- **Capturas:** [FB-001-logo-pincho.png](capturas/FB-001-logo-pincho.png)
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-002"></a>
### FB-002 · Generar las ondas de audio más rápido
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Rendimiento
- **Tipo:** Propuesta
- **Qué dijo:** Si es posible, optimizar el tiempo que tardan en generarse los picos de audio (ondas).
- **Contexto:** Al importar medios y verlos en la línea de tiempo.
- **Notas:** Las ondas se calculan con `waveform.h` y se guardan en la caché del proyecto (`<proyecto>.sharkp.cache/`); la segunda vez deberían salir de la caché. Conviene medir con VODs largos para saber dónde se va el tiempo.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-003"></a>
### FB-003 · Atajo para cortar en el cabezal (ej. Alt+C)
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Línea de tiempo
- **Tipo:** Propuesta
- **Qué dijo:** Atajos de teclado; por ejemplo Alt+C para cortar justo donde está el cabezal de reproducción (Ctrl+C no sirve porque es copiar).
- **Contexto:** Edición en la línea de tiempo.
- **Notas:** Hoy ya existe Ctrl+K para cortar en el cabezal (`Main.qml`), pero no se descubrió: falta que los atajos se vean (menú, tooltips o una lista de atajos). Considerar atajos configurables.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-004"></a>
### FB-004 · La transición y su sonido se separan al moverlas
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Efectos
- **Tipo:** Error
- **Qué dijo:** Al poner una transición, se separa de su audio: si se intenta mover, se mueve solo uno, no los dos a la vez.
- **Contexto:** Transiciones como clips con su efecto de sonido (Editor::addEffectClip + placeSoundHit).
- **Notas:** El sonido se coloca junto a la transición pero no queda enlazado como el video/audio de un medio. Se espera que se comporten como un clip enlazado.
- **Estado:** Hecho ([PLAN-MEJORA.md](PLAN-MEJORA.md), A2)

<a id="fb-005"></a>
### FB-005 · Alt + rueda para desplazarse en vertical en la línea de tiempo
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Línea de tiempo
- **Tipo:** Propuesta
- **Qué dijo:** Que Alt + rueda del ratón desplace verticalmente la línea de tiempo.
- **Contexto:** Navegación entre pistas.
- **Notas:** En el código ya existe (WheelHandler de `LineaTiempo.qml`: Alt + rueda desplaza entre pistas). Revisar por qué no les funcionó (versión probada, zona donde está el ratón, ratón/sistema que manda el giro distinto) o si no lo conocían.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-006"></a>
### FB-006 · Escuchar la vista previa de un sonido con un clic
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Medios
- **Tipo:** Propuesta
- **Qué dijo:** En la pestaña Sonidos, al hacer clic en el icono de audio, reproducir una vista previa del sonido.
- **Contexto:** Elegir efectos de sonido en PanelMedios → Sonidos.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-007"></a>
### FB-007 · Transiciones dentro del clip, al principio o al final
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Efectos
- **Tipo:** Propuesta
- **Qué dijo:** Mantener el sistema actual de transiciones en su propia pista y, a la vez, permitir ponerlas dentro del clip, como era al principio. Si la transición va al principio o al final del clip, que se adapte: un fundido a negro al principio empieza en negro y aparece el clip; al final, se ve el clip y pasa a negro. (O separarlo en 3 transiciones: entrada, salida y corte.)
- **Contexto:** Transiciones como clips (versión actual) frente a las antiguas en los cortes.
- **Notas:** Las transiciones antiguas en los cortes (`Track::transitions`) aún se abren y dibujan, así que parte del motor existe.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-008"></a>
### FB-008 · Transiciones con marca de centro, imán y mitades separables
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Efectos
- **Tipo:** Propuesta
- **Qué dijo:** Que las transiciones tengan una marca en el centro exacto del corte, con imán para sincronizarse con el corte de la línea de tiempo. Además, que sea separable: el lado izquierdo se puede separar del derecho, para usar una mitad como cierre y la otra como apertura en los extremos de la línea de tiempo.
- **Contexto:** Colocar transiciones como clips sobre los cortes.
- **Notas:** Relacionado con FB-007 (las mitades separables equivalen a transiciones de entrada y de salida). Hoy addEffectClip ya centra la transición en el corte más cercano al soltarla, pero no hay marca visible ni imán al moverla después.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-009"></a>
### FB-009 · Ver y usar por separado las pistas de audio de un archivo (MKV)
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Propiedades
- **Tipo:** Propuesta
- **Qué dijo:** Algunos formatos (como MKV) guardan varias pistas de audio en un archivo. Sería ideal que en Propiedades salieran por separado: agrandar ese panel con secciones Propiedades · Video · Audio, y si el audio tiene varias pistas/canales, que aparezcan y se puedan manejar una a una. También poder exportarlas o sacarlas a una pista separada de la línea de tiempo.
- **Contexto:** Grabaciones de OBS con varias pistas de audio (micro, juego, música…).
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-010"></a>
### FB-010 · El punto de anclaje es confuso
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Propiedades
- **Tipo:** Usabilidad
- **Qué dijo:** El apartado de punto de anclaje es algo confuso para quien no lo conoce; habría que explicar mejor su uso.
- **Contexto:** Sección «Punto de anclaje» en PanelPropiedades (seguimiento de movimiento).
- **Notas:** Posibles vías: texto de ayuda, tooltip, mini tutorial o un nombre más claro (p. ej. «Seguir un objeto»).
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-011"></a>
### FB-011 · Carpetas vinculadas de medios frecuentes, entre proyectos
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Medios
- **Tipo:** Propuesta
- **Qué dijo:** En Medios, Transiciones, Efectos, etc., añadir un botón + para vincular carpetas con toda la media que se usa a menudo, y que se mantengan entre proyectos (mientras la carpeta no cambie de ruta).
- **Contexto:** Reutilizar recursos (overlays, sonidos, música…) en todos los proyectos.
- **Notas:** Ya hay ajustes que se guardan para todos los proyectos en QSettings (ajustes de clips, fuentes favoritas), un patrón parecido. Si la carpeta se mueve, habría que avisar.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-012"></a>
### FB-012 · Las imágenes PNG y JPG no funcionan bien
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Medios
- **Tipo:** Error
- **Qué dijo:** El PNG no aparece en la línea de tiempo y en el proyecto sale como 0:00. Los JPG sí aparecen en la línea de tiempo, pero no se ven en el visor (Programa) y no se puede alargar su duración.
- **Contexto:** Importar imágenes fijas.
- **Notas:** El diálogo de importar solo lista videos y audio; las imágenes fijas no tienen aún soporte propio (duración ajustable). En el plan aparece «Overlays e imágenes (PNG con transparencia)».
- **Estado:** Hecho ([PLAN-MEJORA.md](PLAN-MEJORA.md), A1)

<a id="fb-013"></a>
### FB-013 · Efecto de color personalizado
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Efectos
- **Tipo:** Propuesta
- **Qué dijo:** Un efecto de color como el de blanco y negro, pero con cualquier color personalizado (teñir la imagen).
- **Contexto:** Pestaña Efectos.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-014"></a>
### FB-014 · Selector de color completo al pulsar el cuadro de color
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Propiedades
- **Tipo:** Propuesta
- **Qué dijo:** En los efectos con colores, al hacer clic en el cuadro del color, que se despliegue un selector con toda la gama para elegir el que se quiera.
- **Contexto:** Ajustar colores de efectos (sombra, pantalla verde, subtítulos…).
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-015"></a>
### FB-015 · Editar la secuencia después de crearla y guardarla como preset
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Proyectos
- **Tipo:** Propuesta
- **Qué dijo:** Poder editar la secuencia una vez creada: cambiar la relación de aspecto, los fotogramas por segundo y ajustarla fuera de los presets por defecto, con opción para guardar esa configuración como preset propio.
- **Contexto:** Configuración de secuencias (lienzo y fps).
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-016"></a>
### FB-016 · Paneles reorganizables y diseños guardados
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Otro
- **Tipo:** Propuesta
- **Qué dijo:** Que los paneles (Medios, Propiedades, Programa, Línea de tiempo…) se puedan reorganizar. Añadir el clásico menú superior (Archivo, …) con «Diseño» predeterminado y «Guardar nuevo» para guardar la distribución que elija cada persona.
- **Contexto:** Interfaz general del editor.
- **Notas:** Hoy la interfaz usa pestañas superiores (Proyectos · Editor · Exportar) sin barra de menús.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-017"></a>
### FB-017 · No mostrar «descargar la primera vez» si el modelo ya está
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Subtítulos
- **Tipo:** Usabilidad
- **Qué dijo:** Si ya se descargó el modelo de transcripción (o algo parecido), que deje de aparecer el aviso de «descarga la primera vez».
- **Contexto:** Diálogo de subtítulos.
- **Notas:** El texto del selector de modelo en `DialogoSubtitulos.qml` es fijo («descarga de ~550 MB la primera vez») aunque el modelo ya esté en <datos de la app>/modelos.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-018"></a>
### FB-018 · Descargas opcionales desde el instalador
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Instalación
- **Tipo:** Propuesta
- **Qué dijo:** En el instalador, añadir la instalación/descarga opcional del transcriptor (modelo), para dejarlo listo desde el principio; y lo mismo con futuras funciones que necesiten descargas, si la persona lo pide.
- **Contexto:** Instalador (installer/SharkCut.iss, Inno Setup).
- **Notas:** Debería conservar la comprobación SHA-256 que ya hace ModelDownload.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-019"></a>
### FB-019 · Botones de zoom de la línea de tiempo arriba a la derecha
- **Fecha:** 2026-10-22
- **Quién:** Kyo y Miyon
- **Versión:** 0.1.0
- **Área:** Línea de tiempo
- **Tipo:** Usabilidad
- **Qué dijo:** Mover los botones de acercar y alejar de la línea de tiempo a la esquina superior derecha.
- **Contexto:** Barra de la línea de tiempo.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-020"></a>
### FB-020 · El botón Proxies «a veces no sirve»; elegir la calidad de la vista previa
- **Fecha:** 2026-10-22
- **Quién:** Miyon
- **Versión:** 0.1.0
- **Área:** Visor
- **Tipo:** Usabilidad (con una propuesta)
- **Qué dijo:** no se entiende por qué a veces el botón «Proxies» no funciona. Si los proxies
  son una vista previa de baja calidad en el visor, sería ideal elegir cuánta calidad se
  quiere: completa, 3/4, 1/2… como en Premiere.
- **Contexto:** botón «Proxies» de la barra del visor.
- **Notas:** según el código (`Main.qml`, `Editor::decodePath`), el botón:
  1. está desactivado mientras el proyecto no está guardado (los proxies van en su caché);
  2. al activarlo solo cambia los medios que ya tienen un proxy creado: los proxies no se
     crean solos, hay que hacerlo con clic derecho → «Crear proxy» en Medios. Sin proxies
     creados, activarlo no cambia nada a la vista, y eso parece que «no sirve».
  Los proxies son archivos aparte a 540p (`kProxyHeight`), no una resolución de
  reproducción. Lo que pide (completa, 1/2, 1/4…) es otra cosa: bajar la resolución del
  visor en vivo, sin crear archivos. Pueden convivir las dos.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md)): la primera parte (botón «Proxies», A3) está hecha; la calidad de la vista previa elegible es M5.

<a id="fb-021"></a>
### FB-021 · Ver y configurar la aceleración por hardware
- **Fecha:** 2026-10-22
- **Quién:** Miyon
- **Versión:** 0.1.0
- **Área:** Rendimiento
- **Tipo:** Propuesta
- **Qué dijo:** preguntó por un apartado de configuración para comprobar cómo se está
  acelerando la app («aceleración por hardware»).
- **Contexto:** pregunta general sobre ajustes/rendimiento.
- **Notas:** la app ya usa la GPU sola, sin ajustes: decodifica con D3D11VA/NVDEC si puede
  (si no, por software), dibuja con Direct3D 11 y exporta con NVENC, AMF o Quick Sync, o con
  x264/x265 por software. Hoy no se ve en ninguna parte qué se está usando (solo el códec
  al terminar una exportación y el registro de la sesión). Idea: un apartado de
  Configuración/Información con la GPU detectada, el decodificador y el codificador en uso,
  y quizá poder forzar el camino por software si da problemas.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-022"></a>
### FB-022 · ¿El códec de exportación aguanta la recompresión de YouTube?
- **Fecha:** 2026-10-22
- **Quién:** Miyon
- **Versión:** 0.1.0
- **Área:** Exportación
- **Tipo:** Pregunta
- **Qué dijo:** YouTube comprime los videos; ¿el códec con el que exporta SharkCut evita esa
  compresión o la aguanta bien?
- **Contexto:** exportar para subir a YouTube.
- **Notas:** ningún códec evita la recompresión: YouTube vuelve a codificar todo lo que se
  sube. Lo que ayuda es subir con mucha calidad para que pierda menos. SharkCut exporta MP4
  H.264 o H.265 + AAC 192 kbps; la calidad «Alta» va a 16 Mbps en 1080p60 (YouTube
  recomienda ~12) y «Máxima» a 40 Mbps. Idea para el futuro: un preset «YouTube» en
  Exportar con una explicación corta, y quizá la opción de exportar a 1440p (YouTube
  usa entonces un códec mejor, VP9/AV1, y se ve mejor incluso a 1080p).
- **Seguimiento (Kyo):** ¿1080p a 40 Mbps se ve mejor en YouTube? Solo un poco: YouTube
  recomprime a su propia tasa, y por encima de lo recomendado apenas se nota (el archivo
  pesa más y tarda más en subir). Lo que más cambia el resultado es la resolución subida:
  en 1080p se suele quedar en H.264 (VP9/AV1 suele llegar solo si el video tiene muchas
  visitas); desde 1440p suele usar VP9/AV1 con más tasa. YouTube no lo documenta, es lo
  que se observa. En SharkCut ya se puede: Exportar → resolución 133 % (2560×1440 desde un
  lienzo 1080p), calidad «Alta» (~28 Mbps).
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md))

<a id="fb-023"></a>
### FB-023 · Tirón al cortar durante la reproducción
- **Fecha:** 2026-10-23
- **Quién:** Kyo
- **Versión:** 0.1.1
- **Área:** Línea de tiempo
- **Tipo:** Error
- **Qué dijo:** cuando se hace un corte y el video se está reproduciendo, se genera un
  pequeño «lagazo».
- **Contexto:** cortar con la reproducción en marcha.
- **Notas:** en el código, dividir cambia los clips y `Editor::onProjectChanged` pausa,
  reinicia los hilos de video y el audio, vuelve a buscar y reanuda; eso explica el tirón.
  Dividir no cambia lo que se ve ni se oye, así que se podría evitar el reinicio.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md), A10)

<a id="fb-024"></a>
### FB-024 · Doble clic en Medios abre Importar
- **Fecha:** 2026-10-23
- **Quién:** Kyo
- **Versión:** 0.1.1
- **Área:** Medios
- **Tipo:** Propuesta
- **Qué dijo:** al hacer doble clic en Medios, donde está toda la media, que se abra la
  ventana de importar sin tener que pulsar el botón Importar.
- **Contexto:** panel Medios (PanelBiblioteca).
- **Notas:** hoy el doble clic solo abre carpetas y secuencias; en la zona vacía no hace
  nada. Es lo que hace Premiere.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md), M10)

<a id="fb-025"></a>
### FB-025 · Clips muy cortos no se seleccionan con la línea de tiempo alejada
- **Fecha:** 2026-10-23
- **Quién:** Kyo
- **Versión:** 0.1.1
- **Área:** Línea de tiempo
- **Tipo:** Error
- **Qué dijo:** a veces, si los cortes son muy pequeños y la línea de tiempo está muy
  alejada, al intentar seleccionarlos no se consigue, no lo registra.
- **Contexto:** línea de tiempo con poco zoom y muchos cortes cortos.
- **Notas:** por rendimiento, los clips de menos de 16 px en pantalla se dibujan juntos como
  un «bloque» (`TimelineClipsModel`, `kMinClipPixels`), y un bloque no se puede
  seleccionar. Acercando el zoom vuelven a ser clips normales.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md), A11)

<a id="fb-026"></a>
### FB-026 · Un medio recién importado no se deja arrastrar a la línea de tiempo
- **Fecha:** 2026-10-23
- **Quién:** Kyo
- **Versión:** 0.1.1
- **Área:** Medios
- **Tipo:** Error
- **Qué dijo:** importó un archivo nuevo e intentó arrastrarlo a la línea de tiempo, pero no
  la dejó; es como si al cerrar la ventana de importar la app se hubiera quedado bugueada.
- **Contexto:** importar con la ventana de importar y arrastrar el medio enseguida.
- **Detalles (Kyo):** video de unos 3 minutos; se cerró el diálogo con el botón «Abrir».
  No funcionaba nada del panel Medios: al desplazarse, la lista volvía al principio. Se
  arregló guardando, cerrando y abriendo la app.
- **Notas:** causa probable (sin confirmar en Windows): desde la 0.1.1, con «Proxies»
  activado, el proxy de un medio nuevo se crea solo (`ProxyManager::setAutoCreate`).
  Mientras se crea, cada 30 fotogramas (`writeProxy`) se avisa del progreso
  (`ProxyManager::changed` → `Editor::libraryChanged`), y eso vuelve a construir toda la
  lista de Medios varias veces por segundo: el desplazamiento vuelve arriba y el
  arrastre se corta porque el elemento se destruye. Al reabrir la app el proxy ya
  estaba hecho, por eso todo iba bien. Falta confirmar que «Proxies» estaba activado.
- **Estado:** En el plan ([PLAN-MEJORA.md](PLAN-MEJORA.md), A12)
