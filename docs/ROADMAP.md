# Hoja de ruta de SharkCut

Qué trae cada versión, qué viene después y qué queda pendiente. Sale del feedback de la fase
de pruebas ([FEEDBACK.md](feedback/FEEDBACK.md)), organizado en
[PLAN-MEJORA.md](feedback/PLAN-MEJORA.md), donde está el detalle de cada punto (su código,
p. ej. M19, lleva a él).

Las próximas versiones salen de dos o tres puntos cada una; después de la 0.1.9 viene la
0.2.0. Lo que aún no tiene versión («Más adelante») puede cambiar de orden con el feedback
nuevo. La página de descarga lo muestra en `web/roadmap.html`: al cambiar este archivo,
actualizar también esa página.

Última actualización: 2026-10-27.

---

## Próximas versiones

### 0.1.5 · Medios más ordenados y subtítulos más fáciles de ajustar
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M19 | Los sonidos de SharkCut que se usan en un proyecto van a su propia carpeta de Medios («Recursos de SharkCut»), en vez de mezclarse con tus medios. | Pequeño |
| M20 | Medios se abre con la vista de miniaturas (la lista queda como opción) y recuerda la vista que elijas. | Pequeño |
| A15 | Cortar subtítulos con la cuchilla y alargarlos o acortarlos desde su borde, también cuando van pegados unos a otros. | Medio |

### 0.1.6 · Color
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M3 | Efecto de tinte de color y un selector de color completo en todas las muestras de color (efectos, pantalla verde, subtítulos). | Medio |
| M22 | Crear un color sólido desde Medios (clic derecho en un hueco), para fondos y rellenos. | Medio |

### 0.1.7 · Texto y audio de las grabaciones
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M21 | Editor de texto: textos libres en la línea de tiempo que se colocan en cualquier parte de la pantalla, con su estilo, keyframes y efectos. Pestaña nueva «Texto» en el panel. | Grande |
| M4 | Pistas de audio por separado de las grabaciones que guardan varias (por ejemplo, micrófono y juego en un MKV). | Grande |

### 0.1.8 · Secuencias y rendimiento
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M2 | Cambiar el lienzo y los fps de una secuencia ya creada y guardar presets propios. | Medio |
| M5 | Resolución de reproducción del visor (completa, 1/2, 1/4) para que vaya más fluido. | Medio |
| M11 | Caché en una carpeta común, con opción para limpiarla. | Medio |

### 0.1.9 · Línea de tiempo
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M1 | Transiciones de entrada y de salida, con marca de centro e imán al moverlas. | Grande |
| M17 | Altura de las pistas ajustable. | Medio |

### 0.2.0 · Nueva aleta y atajos a tu gusto
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M8 | Logo más reconocible como aleta de tiburón. | Medio |
| M13 | Cabezal con forma de aleta de tiburón (la misma del logo). | Pequeño |
| M18 | Atajos de teclado personalizables. | Medio |

### 0.2.1 · Propiedades a fondo
| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| M14 | Propiedades con una pestaña por cada efecto del clip (va primero: las máscaras usan su pestaña). | Medio |
| M15 | Línea de tiempo de keyframes dentro de Propiedades. | Grande |
| M16 | Máscaras con formas simples (rectángulo, elipse…). | Medio–grande |

---

## Más adelante

Cambios grandes o comodidades que pueden esperar.

| Punto | Qué trae | Tamaño |
|-------|----------|--------|
| B1 | Paneles que se pueden mover y cambiar de tamaño, con diseños guardados. | Grande |
| B2 | Carpetas vinculadas que se comparten entre proyectos. | Medio |
| B3 | Descargas opcionales en el instalador (como el modelo de subtítulos). | Medio |
| B4 | Tutorial dentro de la app. | Grande |
| B5 | Velocidad de los clips: acelerar, ralentizar y rampas de velocidad. | Grande |
| B6 | Varios idiomas, elegibles en el instalador. | Grande |

---

## Versiones publicadas

### 0.1.4 · 2026-10-26
- El cabezal llega hasta el final exacto de la secuencia (A14).
- Un clic en la flecha de una transición la pone en el cabezal, con su sonido (M12).
- «Preparar para» YouTube o TikTok en Exportar (M6).
- Apartado «Hardware y codificadores»: GPU, decodificación y codificador en uso (M7).
- Ondas de audio unas 4 veces más rápidas (A6).

### 0.1.3 · 2026-10-26
- Escuchar los efectos de sonido antes de usarlos (A7).
- Doble clic en un hueco de Medios para importar (M10).
- Botones de zoom arriba a la derecha (M9).
- Atajos de teclado a la vista en las ayudas de los botones (A4).
- Punto de anclaje explicado (A8) y aviso de descarga del modelo de subtítulos solo si
  hace falta (A9).
- Alt + rueda para desplazarse entre pistas (A5).

### 0.1.2 · 2026-10-25
- Arrastrar un medio recién importado (A12).
- Cortar durante la reproducción sin tirones (A10).
- Seleccionar clips muy cortos con la línea de tiempo alejada (A11).
- La línea de tiempo sigue al cabezal al saltar entre cortes (A13).
- Pantalla «Acerca de».

### 0.1.1 · 2026-10-22
- Imágenes PNG y JPG en la línea de tiempo (A1).
- Las transiciones y su sonido se mueven juntos (A2).
- Botón «Proxies» más claro (A3).

### 0.1.0 · 2026-10-21
- Primera versión de acceso anticipado.

---

## Cómo se mantiene

- Al decidir la versión de un punto, se apunta aquí, en su «Para:» de PLAN-MEJORA.md y en
  `web/roadmap.html` (con textos para el público, sin los códigos).
- Al terminarlo, se marca como hecho en PLAN-MEJORA.md y FEEDBACK.md; al publicar la
  versión, sus puntos pasan a «Versiones publicadas».
- El feedback nuevo entra primero en FEEDBACK.md y después aquí, cuando tiene prioridad.
