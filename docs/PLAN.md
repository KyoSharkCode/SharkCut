# SharkCut — Plan base del proyecto

> Documento fundacional del proyecto. La sección **"Instrucciones para el
> asistente"** está copiada en `CLAUDE.md` (raíz del repositorio) para que
> cualquier sesión de desarrollo arranque con este contexto.
>
> Nombre del proyecto: **SharkCut** (nombre de trabajo original: KyoEditor).

---

## 1. Visión

SharkCut es un **editor de video de escritorio para Windows**, de uso personal,
pensado para el flujo de trabajo de KyoSumiVT: clips, shorts, anuncios y
contenido de stream.

No pretende clonar Adobe Premiere Pro. Su objetivo es **sustituir el uso real
que se hace hoy de Premiere + Premiere Composer**:

- Cortar y montar clips en una línea de tiempo.
- Aplicar transiciones dinámicas (Zoom In, Zoom Out, Shake, Distort, etc.)
  arrastrándolas entre dos clips.
- Añadir efectos de sonido sincronizados con esas transiciones.
- Integrar las animaciones, overlays y la marca de Kyo Estudio.
- Exportar rápido a MP4 en vertical (9:16) y horizontal (16:9).

**Plazo de referencia:** la suscripción de Premiere vence en aproximadamente
un año. La app debe cubrir el flujo diario antes de esa fecha.

---

## 2. Decisiones tomadas

| Tema | Decisión | Motivo |
|---|---|---|
| Plataforma | Windows (64 bits) | Es el sistema que se usa |
| Tipo de app | Escritorio, local, sin depender de internet | Rendimiento, acceso a archivos, clips largos |
| Framework | **Electron** | Interfaz en HTML/CSS/JS; reutiliza el código y estilo de Kyo Estudio |
| Lenguaje | JavaScript (módulos ES), con JSDoc donde ayude | Mismo lenguaje que Kyo Estudio, fácil reutilizar plantillas |
| Render (vista previa y exportación) | **WebGL2 + shaders GLSL** | Transiciones y efectos rápidos en GPU |
| Codificación final | **ffmpeg nativo** incluido en la app | Mucho más rápido que ffmpeg.wasm; usa la GPU |
| Codificación por hardware | **NVENC por defecto** (RTX 3060 12 GB); AMF / QuickSync detectados si existen; fallback a `libx264` | El PC de trabajo tiene NVIDIA; el resto queda por compatibilidad |
| Edición | **No destructiva** | Los archivos originales nunca se modifican |
| Archivo de proyecto | `.sharkp` (JSON) | Legible, versionable, fácil de depurar |
| Empaquetado | `electron-builder` → instalador NSIS `.exe` | Instalación sencilla en Windows |
| Alternativa futura | Tauri (Rust) | Solo si el rendimiento o el tamaño lo exigen |

### Principio clave: un solo motor de render

La vista previa y la exportación **usan exactamente el mismo código de render**
(WebGL). La exportación dibuja cada fotograma con ese motor y se lo pasa a
ffmpeg solo para codificarlo. Así lo que se ve en pantalla es idéntico a lo
que se exporta.

---

## 3. Alcance

### Dentro del alcance
- Línea de tiempo multipista (empezar con 2–3 pistas de video y 2 de audio).
- Cortar, recortar, mover, dividir y eliminar clips.
- Biblioteca de transiciones propias (sección 6).
- Biblioteca de efectos de sonido con licencia libre (sección 7).
- Textos y títulos con la marca Marea Nocturna.
- Overlays y animaciones propios (conexión con Kyo Estudio más adelante).
- Presets de exportación: 1080×1920 (9:16), 1920×1080 (16:9), 1080×1080 (1:1).
- Deshacer/rehacer, autoguardado y atajos de teclado.

### Fuera del alcance (al menos al principio)
- Gradación de color profesional, multicámara, edición colaborativa.
- Plugins de terceros (formatos OFX, VST, etc.).
- macOS y Linux.
- Distribución pública (no hace falta firmar el ejecutable; Windows
  SmartScreen avisará y basta con "Ejecutar de todos modos").

---

## 4. Arquitectura

```
┌───────────────────────── Electron ─────────────────────────┐
│                                                            │
│  Proceso principal (Node.js)                               │
│   ├─ Gestión de ventanas y menús                           │
│   ├─ Sistema de archivos (abrir/guardar .sharkp, medios)  │
│   ├─ ffprobe: leer metadatos de los clips                  │
│   ├─ ffmpeg: generar miniaturas, ondas de audio, proxies   │
│   └─ ffmpeg: codificar la exportación final                │
│                                                            │
│  Proceso de interfaz (renderer)                            │
│   ├─ UI: biblioteca de medios, línea de tiempo, inspector  │
│   ├─ Modelo del proyecto (estado + historial deshacer)     │
│   ├─ Motor de render WebGL2 (compositor + transiciones)    │
│   ├─ Decodificación de video (WebCodecs)                   │
│   └─ Motor de audio (Web Audio API)                        │
│                                                            │
│  Comunicación entre ambos: IPC con contextBridge           │
│  (contextIsolation activado, nodeIntegration desactivado)  │
└────────────────────────────────────────────────────────────┘
```

### Flujo de exportación
1. El motor de render dibuja el fotograma N en un canvas WebGL fuera de pantalla.
2. Se leen los píxeles (RGBA) y se envían por IPC al proceso principal.
3. El proceso principal los escribe en la entrada estándar de ffmpeg:
   ```
   ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 60 -i - \
          -i audio_mezclado.wav \
          -c:v h264_nvenc -preset p5 -b:v 12M \
          -c:a aac -b:a 192k salida.mp4
   ```
4. El audio se mezcla aparte (Web Audio `OfflineAudioContext` o filtros de
   ffmpeg) y se añade en el mismo paso.
5. Se detecta qué codificador por hardware existe (`ffmpeg -encoders`) y se
   usa el mejor disponible; si ninguno funciona, `libx264`.

### Vista previa
- **Fase 1:** `HTMLVideoElement` + `requestVideoFrameCallback` como textura
  WebGL (sencillo, suficiente para pocas pistas).
- **Fase 3–4:** WebCodecs (`VideoDecoder`) + demuxer (p. ej. `mp4box.js`)
  para búsqueda precisa por fotograma y mejor rendimiento.
- **Proxies:** para clips pesados (4K, códecs difíciles) se genera con ffmpeg
  una copia ligera que se usa en la vista previa; la exportación usa el original.

---

## 5. Estructura de carpetas propuesta

```
SharkCut/
├─ CLAUDE.md                 # Instrucciones para el asistente (sección 11)
├─ README.md
├─ docs/
│  └─ PLAN.md                # Este documento
├─ package.json
├─ electron-builder.yml
├─ src/
│  ├─ main/                  # Proceso principal
│  │  ├─ main.js
│  │  ├─ preload.js
│  │  ├─ ffmpeg.js           # Rutas, detección de codificadores, exportación
│  │  └─ media.js            # ffprobe, miniaturas, ondas, proxies
│  ├─ renderer/              # Interfaz
│  │  ├─ index.html
│  │  ├─ app.js
│  │  ├─ ui/                 # Línea de tiempo, biblioteca, inspector, visor
│  │  ├─ project/            # Modelo, historial, guardado .sharkp
│  │  ├─ render/             # Motor WebGL, compositor
│  │  │  └─ shaders/
│  │  └─ audio/              # Mezcla y reproducción
│  └─ shared/                # Utilidades comunes (easing, tiempo, etc.)
├─ presets/
│  ├─ transitions/           # Un .json + shader por transición
│  └─ sfx/                   # Audios con licencia libre + LICENCIAS.md
├─ vendor/
│  └─ ffmpeg/                # ffmpeg.exe y ffprobe.exe (ver licencia)
├─ prototype/                # Prueba de concepto (fase 0)
└─ tests/
```

---

## 6. Sistema de transiciones

### Objetivo
Recrear desde cero el tipo de transiciones que se usan en Premiere Composer.
**No se copia código ni recursos de Composer**: se reproducen las técnicas,
que son genéricas (transformaciones animadas, desenfoques y deformaciones).
El resultado será muy parecido, no idéntico píxel a píxel, y totalmente
personalizable.

### Transiciones iniciales

| Transición | Técnica |
|---|---|
Prioridad 1 — las que se usan hoy en Composer (fase 0):

| Transición | Técnica |
|---|---|
| Fade to Black | Opacidad del clip A a negro y de negro al clip B, con curva configurable |
| Pan (Left) | El clip A sale hacia la izquierda y el B entra desde la derecha, con desenfoque de movimiento horizontal |
| Rotate | Rotación del clip A con desenfoque radial mientras entra el clip B girando hasta 0° |
| Shake & Distort | Desplazamiento y rotación con ruido que decae + shader de deformación (ondas / lente) + aberración cromática RGB |
| Zoom In | Escala del clip A de 100% a ~300% con curva rápida + desenfoque radial; el clip B entra desde ~30% a 100% |
| Zoom Out | Inverso del Zoom In |
| Zoom & Rotate Out | Zoom Out combinado con rotación y desenfoque radial |

Prioridad 2 — ampliaciones posteriores (fase 4):

| Transición | Técnica |
|---|---|
| Pan (otras direcciones) | Mismo shader que Pan (Left) con dirección como parámetro |
| Glitch | Bloques desplazados, separación RGB y ruido digital |

### Formato de un preset (ejemplo)

```json
{
  "id": "zoom-in",
  "name": "Zoom In",
  "category": "zoom",
  "defaultDuration": 0.5,
  "shader": "zoom.glsl",
  "params": {
    "intensity": { "type": "number", "min": 1.5, "max": 5, "default": 3 },
    "blur":      { "type": "number", "min": 0,   "max": 1, "default": 0.6 },
    "easing":    { "type": "easing", "default": "easeInOutExpo" },
    "center":    { "type": "point",  "default": [0.5, 0.5] }
  },
  "sfx": "whoosh-zoom-01"
}
```

- Cada transición recibe los dos clips como texturas y un `progress` (0 → 1).
- Las curvas de suavizado (easing) viven en `src/shared/easing.js` y se
  comparten con las animaciones.
- El campo `sfx` (opcional) vincula un sonido que se coloca automáticamente
  en la pista de audio, sincronizado con la transición.

---

## 7. Efectos de sonido

### Regla de licencias (obligatoria)
- **Prohibido** extraer o reutilizar los sonidos de Premiere Composer u otros
  productos de pago: tienen licencia de sus autores.
- Solo se incluyen sonidos con licencia que permita su uso y redistribución.
- Cada sonido se registra en `presets/sfx/LICENCIAS.md` con: nombre del
  archivo, origen, autor y licencia.

### Fuentes válidas
1. **Freesound.org**, filtrando por licencia **CC0**.
2. **Pixabay Sound Effects** (licencia de Pixabay).
3. **Sonniss GDC Game Audio Bundle** (gratuito, libre de regalías).
4. **Sonidos sintetizados por código** (whoosh, impacto, pop, glitch) con
   ruido filtrado y envolventes; son 100% propios.
5. Grabaciones propias.

### Categorías iniciales
Whoosh, Impacto / Hit, Riser, Pop / Click, Glitch, Swoosh, Transición musical.

---

## 8. Integración con Kyo Estudio

- **SharkCut se construye desde cero.** No se copia código de Kyo Estudio
  (`KyoSharkCode/KyoWidgets`), aunque ese proyecto ya tenga un mini editor.
- Usar la guía de marca **Marea Nocturna** (colores, tipografías, logo) para
  la interfaz y los textos y títulos predefinidos.
- **Conexión futura (después de la fase 2):** cuando ambos proyectos estén
  estables, valorar que SharkCut pueda cargar las animaciones de Kyo Estudio
  como clips de animación, o extraer un paquete compartido. No se diseña nada
  de esto antes de tiempo.

---

## 9. Fases y criterios de "hecho"

### Fase 0 — Prueba de concepto (`prototype/`)
Página HTML independiente: se cargan dos clips y se previsualizan entre ellos
las siete transiciones de prioridad 1 (sección 6) con controles de intensidad
y duración.
**Hecho cuando:** la calidad visual convence frente a Premiere Composer.

### Fase 0.5 — Spike técnico del pipeline (`prototype/pipeline/`)
Prueba mínima, sin interfaz, para validar las tres piezas de mayor riesgo
antes de construir encima (detalles en la sección 13):
- Decodificar un clip real de OBS fotograma a fotograma con precisión.
- Exportar 10 s de 1080p60 con una transición aplicada, comparando las dos
  vías de exportación de la sección 13.1 y midiendo fotogramas por segundo.
- Abrir un archivo MKV de OBS con varias pistas de audio.
**Hecho cuando:** la exportación de prueba es al menos tan rápida como el
tiempo real (≥ 60 fps de render para 1080p60) y se ha elegido la vía de
exportación.

### Fase 1 — Lo básico
- Esqueleto Electron + empaquetado `.exe`.
- Importar clips (arrastrar y soltar), leer metadatos con ffprobe.
- Línea de tiempo: 1 pista de video + 1 de audio; cortar, recortar, mover.
- Visor con reproducción, pausa y búsqueda.
- Guardar y abrir `.sharkp`.
- Exportar a MP4 1080p con codificación por hardware (decodificación
  fotograma a fotograma con WebCodecs, ver 13.2).
**Hecho cuando:** se puede montar y exportar un short sencillo sin Premiere.

### Fase 2 — Lo propio
- Transiciones de la fase 0 integradas (arrastrar entre clips).
- Biblioteca de efectos de sonido y emparejamiento transición ↔ sonido.
- Textos con la marca y overlays propios.
- Presets de exportación 9:16, 16:9 y 1:1.
**Hecho cuando:** se puede producir un short típico de principio a fin.

### Fase 3 — Lo cómodo
- Varias pistas de video y audio.
- Deshacer/rehacer, autoguardado, atajos de teclado (estilo Premiere).
- Miniaturas y formas de onda en la línea de tiempo.
- Vista previa con WebCodecs.
**Hecho cuando:** el uso diario es fluido y no se echa de menos Premiere.

### Fase 4 — Lo pro
- Keyframes en posición, escala, rotación y opacidad.
- Más transiciones y efectos (glitch, spin, LUTs básicas).
- Subtítulos automáticos (p. ej. Whisper ejecutado en local).
- Proxies para clips pesados y VODs largos.

---

## 10. Riesgos y cómo mitigarlos

| Riesgo | Mitigación |
|---|---|
| Vista previa lenta con muchas pistas | Empezar con pocas pistas, usar proxies, WebCodecs en fase 3 |
| Diferencias entre vista previa y exportación | Un único motor de render para ambas |
| Consumo de memoria con clips largos | Decodificar por fragmentos; nunca cargar un video entero en memoria |
| Códecs raros (HEVC, ProRes, VFR de OBS) | Normalizar con ffmpeg al importar o generar proxy |
| Video con fotogramas variables (OBS) | Detectarlo con ffprobe y convertir a fotogramas constantes |
| Licencia de ffmpeg | Usar una build LGPL o GPL; para uso personal no hay problema; documentarlo en `vendor/ffmpeg/LICENSE` |
| Licencias de sonidos | Registro obligatorio en `LICENCIAS.md` |
| Alcance que crece sin control | Respetar las fases; nada de la fase 4 antes de cerrar la 2 |
| Ancho de banda al exportar (≈ 8 MB por fotograma RGBA en 1080p) | Ver sección 13.1: convertir a YUV en GPU o codificar con WebCodecs |
| Búsqueda imprecisa de `HTMLVideoElement` al exportar | La exportación decodifica con WebCodecs desde la fase 1 (sección 13.2) |
| Deriva de tiempos por usar segundos en coma flotante | Tiempos internos en enteros (sección 13.4) |

---

## 11. Instrucciones para el asistente (copiadas en `CLAUDE.md`)

```markdown
# SharkCut

Editor de video de escritorio para Windows, de uso personal (KyoSumiVT).
El plan completo está en docs/PLAN.md: léelo antes de proponer cambios grandes.

## Stack
- Electron + JavaScript (módulos ES). Sin frameworks de UI salvo que se acuerde.
- Render: WebGL2 + shaders GLSL. Vista previa y exportación usan el MISMO motor.
- Codificación: ffmpeg nativo en vendor/ffmpeg, invocado desde el proceso principal.
- Seguridad Electron: contextIsolation activado, nodeIntegration desactivado,
  toda la comunicación por preload.js + IPC.

## Reglas
- Edición no destructiva: nunca modificar los archivos de medios originales.
- No copiar código ni recursos de Premiere Composer ni de otros productos de pago.
- Todo sonido añadido debe registrarse en presets/sfx/LICENCIAS.md.
- Respetar las fases del plan; no adelantar funciones de fases posteriores.
- Comentarios y textos de la interfaz en español.
- Estilo visual de la app: marca Marea Nocturna de KyoSumiVT.

## Comandos (actualizar cuando existan)
- npm install
- npm run dev       # arranca la app en modo desarrollo
- npm run build     # genera el instalador .exe
- npm test
```

---

## 12. Cómo arrancar

1. ~~Crear el repositorio en GitHub~~ → `KyoSharkCode/SharkCut`.
2. ~~Añadir este archivo como `docs/PLAN.md` y la sección 11 como `CLAUDE.md`.~~
3. Abrir una sesión de desarrollo sobre el repositorio nuevo.
4. Primer encargo: **Fase 0, prueba de concepto de transiciones** en `prototype/`.
5. Fase 0.5: spike del pipeline (decodificar, exportar, MKV de OBS).
6. Tras validar las fases 0 y 0.5, crear el esqueleto Electron (fase 1).

### Decisiones pendientes
- [x] Nombre definitivo de la app: **SharkCut**.
- [x] Transiciones prioritarias: Fade to Black, Pan (Left), Rotate,
      Shake & Distort, Zoom In, Zoom Out, Zoom & Rotate Out.
- [x] Tarjeta gráfica: NVIDIA RTX 3060 12 GB → `h264_nvenc` por defecto.
- [x] Clips habituales: 1080p a 60 fps (proyecto por defecto 1080p60).
- [x] Grabación de OBS: MP4 fragmentado, una sola pista de audio.
- [x] Extensión del proyecto: `.sharkp`.
- [x] Vía de exportación: **A** (ffmpeg por stdin con conversión a YUV).
- [x] Kyo Estudio: SharkCut se hace desde cero; conexión eventual más adelante.

---

## 13. Revisión técnica y ajustes al plan

Puntos detectados al revisar el plan original. No cambian la visión ni el
stack; ajustan el orden de algunas piezas para reducir riesgo.

### 13.1 Exportación: el cuello de botella es mover píxeles
Un fotograma RGBA de 1920×1080 ocupa ≈ 8,3 MB. A 60 fps son ≈ 500 MB/s
pasando de la GPU a la CPU, por IPC y a la entrada de ffmpeg. Es viable pero
justo, y en 4K no lo es. Dos vías a comparar en la fase 0.5:

| Vía | Cómo funciona | Ventajas | Inconvenientes |
|---|---|---|---|
| **A. ffmpeg por stdin** (plan original) | Un shader convierte el fotograma a YUV 4:2:0 (≈ 3,1 MB, 2,7× menos) antes de leerlo con `readPixels` asíncrono (PBO). Se envía al proceso principal con `MessagePort` y buffers transferibles. | Control total del codificador (NVENC/AMF/QSV con todas sus opciones). | Copia GPU→CPU→IPC en cada fotograma. |
| **B. WebCodecs `VideoEncoder`** | Se crea un `VideoFrame` directamente desde el canvas WebGL y Chromium lo codifica por hardware (Media Foundation en Windows). ffmpeg solo une video y audio en el MP4 final (`-c copy`). | Sin copias de píxeles por IPC; muy rápido. | Menos control de calidad y bitrate; depende del soporte de Chromium. |

**Decisión: vía A.** ffmpeg por stdin, con conversión a YUV en la GPU y
`h264_nvenc` como codificador por defecto. La vía B queda documentada solo
como alternativa si la A no alcanzara la velocidad necesaria.

### 13.2 Decodificación precisa desde la fase 1
`HTMLVideoElement` sirve para la vista previa en tiempo real, pero al exportar
hay que obtener **exactamente** el fotograma N, y `currentTime` + `seek` no lo
garantiza (es lento e impreciso con GOPs largos). Ajuste:
- **Vista previa (fase 1):** `HTMLVideoElement` + `requestVideoFrameCallback`,
  como estaba previsto.
- **Exportación (fase 1):** WebCodecs `VideoDecoder` + demuxer (`mp4box.js`),
  avanzando en orden fotograma a fotograma. Al exportar se decodifica en
  secuencia, que es el caso fácil de WebCodecs.
- **Fase 3:** la vista previa también pasa a WebCodecs (búsqueda precisa al
  arrastrar por la línea de tiempo).

### 13.3 Importación de clips de OBS
- Las grabaciones de OBS son **MP4 fragmentado**, que `mp4box.js` lee
  directamente: **no se duplica ni se convierte el archivo**. Se usa tal cual.
- Solo si algún día se importa un MKV u otro contenedor no compatible, se hace
  un remux a MP4 con `ffmpeg -c copy` en la carpeta de caché (rápido y sin
  pérdida). El original nunca se toca.
- **VFR:** ffprobe detecta fotogramas variables (`r_frame_rate` ≠
  `avg_frame_rate`); en ese caso se genera una copia a fotogramas constantes
  en la caché.
- **Audio:** las grabaciones actuales tienen una sola pista. El modelo de clip
  guarda las pistas como lista para no cerrar la puerta a varias, pero la
  interfaz solo trabaja con una hasta que haga falta.
- Carpeta de caché por proyecto (`<proyecto>.sharkp.cache/`) para proxies,
  miniaturas y ondas; se puede borrar sin perder nada.

### 13.4 Modelo de tiempo
- Los tiempos del proyecto se guardan como **enteros** (microsegundos o
  fotogramas a los fps del proyecto), nunca como segundos en coma flotante, para
  evitar desfases acumulados entre video y audio.
- Cada clip guarda `sourceIn`, `sourceOut` (en el archivo original) y
  `timelineStart` (en la línea de tiempo). Recortar y dividir solo cambia esos
  números: edición no destructiva por diseño.
- El `.sharkp` incluye un campo `version` para poder migrar proyectos
  antiguos cuando cambie el formato.

### 13.5 Electron
- Servir los medios locales con un **protocolo propio** (`media://`) registrado
  con `protocol.handle`, compatible con peticiones por rangos para que el
  `<video>` pueda buscar. Evita activar `webSecurity: false` o abrir `file://`.
- Además de `contextIsolation` y sin `nodeIntegration`: `sandbox: true` en el
  renderer y una API de `preload.js` mínima y explícita (nada de exponer `fs`
  ni `child_process`).
- ffmpeg se ejecuta siempre desde el proceso principal con `spawn` y
  argumentos en lista (nunca concatenando una cadena de comando).

### 13.6 Calidad y automatización
- `npm test` con un ejecutor ligero (p. ej. Vitest) para lo que no depende de
  la GPU: modelo del proyecto, historial, easing, tiempos, construcción de
  argumentos de ffmpeg.
- GitHub Actions en `windows-latest` que ejecute los tests y genere el
  instalador `.exe` como artefacto descargable.
- Un pequeño conjunto de clips de prueba (cortos, propios) para comprobar la
  exportación de extremo a extremo.
