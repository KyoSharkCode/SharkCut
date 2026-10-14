# SharkCut — Plan base del proyecto

> Documento fundacional del proyecto. Las reglas esenciales están resumidas en
> `CLAUDE.md` (raíz del repositorio) para que cualquier sesión de desarrollo
> arranque con este contexto.
>
> Nombre del proyecto: **SharkCut** (nombre de trabajo original: KyoEditor).
> Revisión 2: se cambia de Electron (web) a una **aplicación nativa en C++**.

---

## 1. Visión

SharkCut es un **editor de video nativo de escritorio para Windows**, de uso
personal, pensado para el flujo de trabajo de KyoSumiVT:

- **VODs de Twitch** de 40 minutos o más (edición larga, muchos cortes).
- Clips, shorts, TikToks y anuncios.
- Contenido de stream con la marca de Kyo.

El objetivo es **sustituir por completo el uso real de Premiere Pro +
Premiere Composer**, con una arquitectura de editor profesional (como
DaVinci Resolve o Premiere), no un editor web empaquetado:

- Línea de tiempo multipista fluida incluso con proyectos de varias horas.
- Decodificación, composición y codificación en la GPU, sin copias
  innecesarias de píxeles.
- Transiciones dinámicas propias (Zoom, Pan, Rotate, Shake & Distort…)
  arrastrables entre clips, con efectos de sonido sincronizados.
- Textos, títulos y overlays con la marca Marea Nocturna.
- Exportación rápida por hardware a MP4 en 16:9, 9:16 y 1:1.

**Horizonte:** proyecto a largo plazo. La suscripción de Premiere vence en
aproximadamente un año; es una fecha de referencia, no un límite. Se prioriza
una base sólida sobre la rapidez.

---

## 2. Decisiones tomadas

| Tema | Decisión | Motivo |
|---|---|---|
| Plataforma | Windows 10/11 (64 bits) | Es el sistema que se usa |
| Tipo de app | **Nativa**, local, sin depender de internet | Rendimiento de editor profesional, VODs largos |
| Lenguaje | **C++20** | Lenguaje de Premiere, DaVinci, Kdenlive y Shotcut; acceso directo a FFmpeg, GPU y audio |
| Interfaz | **Qt 6.10.3** con **Qt Quick / QML** (versión fijada en todo el proyecto) | Interfaz acelerada por GPU y totalmente personalizable (Marea Nocturna); Qt es lo que usa DaVinci |
| GPU / render | **QRhi** (capa gráfica de Qt) con backend **Direct3D 11** en Windows | Un solo código de render; en Windows habla D3D11 directamente; permite compilar y probar en otros sistemas con Vulkan/OpenGL |
| Shaders | GLSL estilo Vulkan (`#version 440`), compilados con `qsb` a HLSL/SPIR-V | Se escriben una vez y Qt los traduce a cada backend |
| Decodificación | **FFmpeg 9 (libavformat/libavcodec)** enlazado como biblioteca (build precompilada de Gyan), con **NVDEC vía D3D11VA** | Los fotogramas se decodifican directamente en texturas de la GPU |
| Codificación | **FFmpeg + NVENC** (`h264_nvenc` / `hevc_nvenc`) recibiendo texturas D3D11; fallback a `libx264` | Exportación sin pasar los píxeles por la CPU |
| Audio | **miniaudio** (WASAPI) + mezclador propio | Biblioteca ligera, baja latencia; el audio marca el reloj de reproducción |
| GPU de referencia | NVIDIA **RTX 3060 12 GB** | PC de trabajo de Kyo; NVENC/NVDEC por defecto |
| Proyecto por defecto | **1080p a 60 fps**, audio estéreo 48 kHz | Formato habitual de grabación |
| Edición | **No destructiva** | Los archivos originales nunca se modifican |
| Archivo de proyecto | **`.sharkp`** (JSON con campo `version`) | Legible, versionable, migrable |
| Compilación | **CMake** (presets) + Ninja; dependencias de C++ con FetchContent y, desde la llegada de FFmpeg, vcpkg o binarios precompilados | Estándar en C++ moderno; reproducible en el PC y en CI |
| Empaquetado | `windeployqt` + instalador (Inno Setup o NSIS) | Instalación sencilla en Windows |
| Tests | **Catch2 v3** + `ctest` | Pruebas del modelo, tiempos y pipeline sin interfaz |
| CI | GitHub Actions en `windows-latest` y Ubuntu | Compila, ejecuta tests y publica el ejecutable de Windows |
| Estilo visual | Tema **"Marea Profunda"**: versión sobria de Marea Nocturna (sección 15) | El editor se usa durante horas; la marca va en acentos |

### Principio clave: un solo motor de render

La vista previa y la exportación **usan exactamente el mismo motor**. Para
exportar, el motor dibuja cada fotograma en una textura fuera de pantalla y
se la entrega a NVENC. Lo que se ve en el visor es idéntico a lo que se
exporta.

### Principio clave: la GPU no suelta los fotogramas

```
Archivo MP4 ─▶ NVDEC ─▶ textura D3D11 (NV12) ─▶ shaders (composición,
                                                 transiciones, textos)
                                                        │
                                   ┌────────────────────┴───────────────┐
                                   ▼                                    ▼
                           Visor (pantalla)                  NVENC ─▶ MP4 final
```

En el camino normal los píxeles nunca bajan a la memoria RAM. Es lo que
permite editar VODs largos con fluidez y exportar mucho más rápido que el
tiempo real.

---

## 3. Alcance

### Dentro del alcance
- Línea de tiempo multipista (video, audio, texto/overlay).
- Cortar, recortar, mover, dividir, eliminar, rizar (ripple) y cortes rápidos
  con atajos estilo Premiere.
- Proyectos largos: VODs de 40 min a varias horas sin perder fluidez.
- Biblioteca de transiciones propias (sección 6).
- Biblioteca de efectos de sonido con licencia libre (sección 7).
- Textos y títulos con la marca Marea Nocturna.
- Overlays e imágenes (PNG con transparencia).
- Presets de exportación: 1920×1080 (16:9), 1080×1920 (9:16), 1080×1080 (1:1).
- Deshacer/rehacer, autoguardado y atajos de teclado.
- Proxies, miniaturas y formas de onda generadas en segundo plano.

### Fuera del alcance (al menos al principio)
- Gradación de color profesional (más allá de LUTs básicas), multicámara,
  edición colaborativa.
- Plugins de terceros (OFX, VST).
- macOS y Linux como plataformas de uso (el código se mantiene portable, ver
  sección 4.4, pero solo se publica para Windows).
- Distribución pública (no hace falta firmar el ejecutable).

---

## 4. Arquitectura

### 4.1 Capas

```
┌──────────────────────────── SharkCut.exe ────────────────────────────┐
│                                                                      │
│  Interfaz (Qt Quick / QML)                                           │
│   ├─ Biblioteca de medios, inspector, presets, exportación           │
│   ├─ Línea de tiempo (componente C++ propio, dibujado en GPU)        │
│   └─ Visor (QQuickRhiItem que muestra la salida del motor)           │
│                                                                      │
│  Núcleo (C++ puro, sin dependencias de interfaz)                     │
│   ├─ Modelo del proyecto: pistas, clips, transiciones, textos        │
│   ├─ Comandos + historial (deshacer/rehacer)                         │
│   ├─ Tiempo racional (fotogramas / ticks enteros)                    │
│   └─ Lectura/escritura .sharkp + migraciones de versión              │
│                                                                      │
│  Motor de medios                                                     │
│   ├─ Demux + decodificación (FFmpeg + NVDEC/D3D11VA)                 │
│   ├─ Caché de fotogramas decodificados (en GPU)                      │
│   ├─ Compositor (QRhi + shaders): capas, transiciones, textos        │
│   ├─ Motor de audio (miniaudio + mezclador, reloj maestro)           │
│   └─ Exportador (compositor fuera de pantalla + NVENC + mux)         │
│                                                                      │
│  Servicios en segundo plano (hilos de trabajo)                       │
│   └─ Análisis de medios, miniaturas, ondas, proxies, autoguardado    │
└──────────────────────────────────────────────────────────────────────┘
```

Regla de dependencias: **la interfaz depende del núcleo y del motor; el
núcleo no depende de nada de la interfaz**. Así el núcleo se prueba con tests
sin abrir ventanas.

### 4.2 Hilos
- **Hilo de interfaz:** QML y eventos de usuario. Nunca decodifica ni espera
  a disco.
- **Hilo de render:** el de Qt Quick; ejecuta el compositor.
- **Hilos de decodificación:** uno por clip activo, adelantándose unos
  fotogramas a la reproducción.
- **Hilo de audio:** callback de miniaudio; mezcla y marca el reloj.
- **Cola de trabajos:** miniaturas, ondas, proxies y exportación.

### 4.3 Reproducción
- El **audio es el reloj maestro**; el video muestra el fotograma que
  corresponde al instante de audio actual (y descarta o repite si va por
  detrás o por delante).
- La búsqueda es **precisa por fotograma**: FFmpeg salta al fotograma clave
  anterior y decodifica hacia delante hasta el fotograma exacto.
- Una caché de texturas decodificadas evita redecodificar al arrastrar el
  cabezal por la misma zona.

### 4.4 Portabilidad para desarrollo
El código específico de Windows (D3D11VA, interop D3D11 ↔ NVENC) queda
aislado detrás de interfaces. Existe un camino alternativo (decodificación
por software + subida de textura) que funciona con cualquier backend de
QRhi. Esto permite:
- Compilar y ejecutar los tests en Linux (sesiones de desarrollo en la nube y
  CI rápido).
- Tener un plan B si un clip no se puede decodificar por hardware.

---

## 5. Estructura de carpetas propuesta

```
SharkCut/
├─ CLAUDE.md                  # Instrucciones para el asistente
├─ README.md
├─ CMakeLists.txt
├─ CMakePresets.json          # Configuraciones (debug/release, windows/linux)
├─ vcpkg.json                 # Dependencias (ffmpeg, miniaudio, catch2, json…)
├─ docs/
│  └─ PLAN.md                 # Este documento
├─ src/
│  ├─ app/                    # main.cpp, arranque, registro de tipos QML
│  ├─ core/                   # Modelo, comandos, historial, tiempo, .sharkp
│  ├─ media/                  # Demux, decodificación, caché, análisis, proxies
│  │  └─ platform/win/        # D3D11VA, interop NVENC (solo Windows)
│  ├─ render/                 # Compositor QRhi, transiciones, textos
│  │  └─ shaders/             # *.vert / *.frag (GLSL 440 → qsb)
│  ├─ audio/                  # miniaudio, mezclador, reloj
│  ├─ export/                 # Exportador, presets, selección de codificador
│  └─ ui/
│     ├─ qml/                 # Pantallas y componentes QML
│     ├─ timeline/            # Componente de línea de tiempo en C++
│     └─ theme/               # Marea Nocturna: colores, tipografías, iconos
├─ presets/
│  ├─ transitions/            # Un .json por transición (+ shader en render/)
│  └─ sfx/                    # Audios con licencia libre + LICENCIAS.md
├─ tests/                     # Catch2: core, tiempo, comandos, export…
├─ tools/                     # Scripts de apoyo (empaquetado, utilidades)
└─ installer/                 # Script de Inno Setup / NSIS
```

---

## 6. Sistema de transiciones

### Objetivo
Recrear desde cero el tipo de transiciones que se usan en Premiere Composer.
**No se copia código ni recursos de Composer**: se reproducen las técnicas,
que son genéricas (transformaciones animadas, desenfoques y deformaciones).
El resultado será muy parecido, no idéntico píxel a píxel, y totalmente
personalizable.

### Prioridad 1 — las que se usan hoy en Composer

| Transición | Técnica |
|---|---|
| Fade to Black | Opacidad del clip A a negro y de negro al clip B, con curva configurable |
| Pan (Left) | El clip A sale hacia la izquierda y el B entra desde la derecha, con desenfoque de movimiento horizontal |
| Rotate | Rotación del clip A con desenfoque radial mientras entra el clip B girando hasta 0° |
| Shake & Distort | Desplazamiento y rotación con ruido que decae + deformación (ondas / lente) + aberración cromática RGB |
| Zoom In | Escala del clip A de 100% a ~300% con curva rápida + desenfoque radial; el clip B entra desde ~30% a 100% |
| Zoom Out | Inverso del Zoom In |
| Zoom & Rotate Out | Zoom Out combinado con rotación y desenfoque radial |

### Prioridad 2 — ampliaciones posteriores

| Transición | Técnica |
|---|---|
| Pan (otras direcciones) | Mismo shader que Pan (Left) con dirección como parámetro |
| Glitch | Bloques desplazados, separación RGB y ruido digital |
| Spin, Whip, Slide | Variantes de rotación y desplazamiento con desenfoque |

### Formato de un preset (ejemplo)

```json
{
  "id": "zoom-in",
  "name": "Zoom In",
  "category": "zoom",
  "defaultDuration": { "frames": 30 },
  "shader": "zoom.frag",
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
- Las transiciones con desenfoque usan varias pasadas a texturas intermedias
  (un desenfoque de calidad no cabe en una sola pasada).
- Las curvas de suavizado (easing) viven en `src/core/` y se comparten con
  futuras animaciones y keyframes.
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
- **Conexión futura:** cuando ambos proyectos estén estables, valorar que
  SharkCut importe las animaciones de Kyo Estudio (p. ej. renderizadas a
  video con transparencia, o un formato de animación compartido). No se
  diseña nada de esto antes de tiempo.

---

## 9. Medios de entrada: grabaciones de OBS y VODs

- Las grabaciones de OBS son **MP4 fragmentado**, 1080p60, una pista de
  audio. FFmpeg las lee directamente: **no se duplica ni se convierte nada**.
- Al importar se analiza el archivo (códec, resolución, fps, duración,
  fotogramas clave, pistas). Si se detectan **fotogramas variables (VFR)**, se
  ofrece generar una copia a fotogramas constantes en la caché.
- Contenedores menos habituales (MKV, MOV…) se leen igual con FFmpeg; si
  alguno diera problemas de búsqueda, se hace un remux sin recodificar en la
  caché.
- **VODs largos (40 min – varias horas):**
  - Nunca se carga un archivo entero en memoria; se decodifica por tramos.
  - Miniaturas y formas de onda se generan en segundo plano, por partes, y se
    guardan en la caché para no recalcularlas.
  - **Proxies opcionales** (p. ej. 540p, solo fotogramas clave) para clips 4K
    o códecs pesados; la exportación usa siempre el original.
- Carpeta de caché por proyecto (`<proyecto>.sharkp.cache/`) para proxies,
  miniaturas, ondas y remux; se puede borrar sin perder nada.
- El modelo de clip guarda las pistas de audio como lista (hoy hay una sola,
  pero no se cierra la puerta a varias).

---

## 10. Modelo de tiempo y proyecto

- Los tiempos se guardan como **enteros** (fotogramas del proyecto o ticks de
  una base de tiempo fija), nunca como segundos en coma flotante, para evitar
  desfases entre video y audio.
- Conversión entre la base de tiempo de cada archivo (la de FFmpeg) y la del
  proyecto con aritmética racional.
- Cada clip guarda `sourceIn`, `sourceOut` (en el archivo original) y
  `timelineStart` (en la línea de tiempo). Recortar y dividir solo cambia esos
  números: edición no destructiva por diseño.
- **Toda modificación pasa por un comando** (patrón Command) desde el primer
  día, para que deshacer/rehacer salga casi gratis después.
- El `.sharkp` incluye un campo `version` y migraciones para abrir proyectos
  antiguos cuando cambie el formato.
- Autoguardado periódico a un archivo aparte (`.sharkp.autosave`) para no
  pisar el proyecto principal.

---

## 11. Fases y criterios de "hecho"

### Fase 0 — Cimientos
- Proyecto CMake + vcpkg + Qt, que compila en Windows (y en Linux para CI).
- GitHub Actions: compilación, tests y ejecutable descargable.
- Ventana Qt Quick con un visor QRhi.
- Abrir un MP4 con FFmpeg, decodificar por hardware (NVDEC) y mostrarlo.
- Reproducir el audio sincronizado; play, pausa y búsqueda precisa.
**Hecho cuando:** un clip real de OBS 1080p60 se reproduce fluido y sin
desincronía en SharkCut, y se puede saltar a cualquier fotograma.

### Fase 1 — Transiciones
- Compositor con dos clips y las siete transiciones de prioridad 1.
- Panel de prueba con controles de intensidad, duración y easing.
- Exportación de prueba de esa mezcla con NVENC.
**Hecho cuando:** la calidad visual convence frente a Premiere Composer y el
resultado exportado es idéntico a la vista previa.

### Fase 2 — Editor básico
- Biblioteca de medios (arrastrar y soltar archivos).
- Línea de tiempo: 1 pista de video + 1 de audio; cortar, recortar, mover,
  dividir, eliminar. Comandos + deshacer/rehacer desde el inicio.
- Guardar y abrir `.sharkp`.
- Exportar a MP4 1080p60 con NVENC.
- Interfaz con el tema Marea Nocturna.
**Hecho cuando:** se puede montar y exportar un short sencillo sin Premiere.

### Fase 3 — Lo propio
- Transiciones integradas en la línea de tiempo (arrastrar entre clips).
- Biblioteca de efectos de sonido y emparejamiento transición ↔ sonido.
- Textos, títulos y overlays con la marca.
- Presets de exportación 16:9, 9:16 y 1:1 (con reencuadre).
**Hecho cuando:** se puede producir un short típico de principio a fin.

### Fase 4 — VODs y uso diario
- Varias pistas de video y audio.
- Rendimiento con proyectos largos: caché de fotogramas, miniaturas y ondas
  en segundo plano, proxies, línea de tiempo virtualizada.
- Edición rápida tipo Premiere: ripple delete, atajos (C, V, Q, W, J/K/L…),
  marcadores, zoom de línea de tiempo.
- Autoguardado y recuperación tras cierre inesperado.
**Hecho cuando:** se edita un VOD de 40+ minutos de principio a fin con
fluidez y no se echa de menos Premiere.

### Fase 5 — Lo pro
- Keyframes en posición, escala, rotación y opacidad.
- Más transiciones y efectos (glitch, spin, LUTs básicas, máscaras simples).
- Subtítulos automáticos en local con **whisper.cpp** (C++, usa la GPU).
- Herramientas para VODs: detección de silencios y marcadores automáticos.

---

## 12. Riesgos y cómo mitigarlos

| Riesgo | Mitigación |
|---|---|
| Curva de dificultad de C++ y del pipeline de GPU | Fases pequeñas con criterios claros; tests del núcleo; código de plataforma aislado |
| Interop D3D11 ↔ FFmpeg ↔ QRhi (compartir el mismo dispositivo GPU) | Resolverlo en la fase 0, antes de construir encima; camino por software como plan B |
| Diferencias entre vista previa y exportación | Un único compositor para ambas |
| Rendimiento con VODs largos | Decodificar por tramos, caché de GPU, trabajos en segundo plano, proxies |
| Sincronía audio/video | Audio como reloj maestro; tiempos enteros y racionales |
| Video con fotogramas variables (OBS) | Detectarlo al importar y ofrecer copia a fotogramas constantes |
| Pruebas de GPU en la nube (sin Windows ni NVIDIA) | Tests del núcleo en Linux; pruebas de GPU en el PC de Kyo y en CI de Windows |
| Licencias: Qt (LGPLv3) y FFmpeg (LGPL/GPL) | Enlace dinámico; uso personal; documentar en `LICENSES/` |
| Licencias de sonidos | Registro obligatorio en `LICENCIAS.md` |
| Alcance que crece sin control | Respetar las fases; nada de la fase 5 antes de cerrar la 3 |

---

## 13. Entorno de desarrollo

### En el PC de Kyo (Windows)
- Visual Studio 2022 (o Build Tools) con la carga "Desarrollo para el
  escritorio con C++".
- CMake ≥ 3.25 y Ninja.
- Qt 6.10.3 para MSVC 64 bits, con Qt Shader Tools.
- Controladores NVIDIA actualizados.
- Guía paso a paso: [`docs/SETUP-WINDOWS.md`](SETUP-WINDOWS.md).

### En sesiones en la nube (Linux)
- Compilan y prueban el núcleo y el camino por software.
- Lo específico de Windows y de NVIDIA se valida en el PC de Kyo o en el CI
  de Windows.

### Comandos
```
cmake --preset windows-debug          # o linux-debug / *-release
cmake --build --preset windows-debug
ctest --preset windows-debug
```

---

## 14. Cómo arrancar

1. ~~Crear el repositorio en GitHub~~ → `KyoSharkCode/SharkCut`.
2. ~~Añadir este plan y `CLAUDE.md`.~~
3. Preparar el entorno de desarrollo en el PC (sección 13).
4. Fase 0: cimientos.
   - [x] Etapa 1: proyecto CMake, CI, núcleo de tiempo con tests, ventana Qt Quick
     con diagnóstico (API gráfica, fps).
     Validada en el PC de Kyo (Windows 11 25H2, RTX 3060): Direct3D 11, render fluido.
   - [x] Etapa 2: FFmpeg + decodificación por hardware (D3D11VA/NVDEC) y visor de video
     QRhi con conversión YUV→RGB en shader; reproducción de video, búsqueda precisa
     por fotograma, paso a paso. Probada en Linux (camino por software); pendiente de
     validar en el PC de Kyo.
   - [ ] Etapa 2b: sin copias: la textura D3D11 de NVDEC pasa directamente al visor
     (hoy se copia a memoria y se vuelve a subir; suficiente para 1080p60).
   - [ ] Etapa 3: audio sincronizado (el audio pasa a ser el reloj maestro).
5. Fase 1: transiciones.

### Decisiones tomadas
- [x] Nombre: **SharkCut**.
- [x] Arquitectura: **nativa en C++20 + Qt 6** (revisión 2; antes Electron).
- [x] Transiciones prioritarias: Fade to Black, Pan (Left), Rotate,
      Shake & Distort, Zoom In, Zoom Out, Zoom & Rotate Out.
- [x] Tarjeta gráfica: NVIDIA RTX 3060 12 GB → NVDEC/NVENC por defecto.
- [x] Proyecto por defecto: 1080p60.
- [x] Grabación de OBS: MP4 fragmentado, una sola pista de audio.
- [x] Extensión del proyecto: `.sharkp`.
- [x] Kyo Estudio: SharkCut se hace desde cero; conexión eventual más adelante.
- [x] Lenguaje: C++ (Rust descartado por la madurez de sus bibliotecas de interfaz).
- [x] Estilo visual del editor: tema "Marea Profunda" (sección 15).
- [x] FFmpeg en Windows: binarios precompilados de Gyan (build "full shared", con NVENC,
      NVDEC y D3D11VA), versión 9.0.2 con hash fijado, descargados por CMake. En Linux,
      las bibliotecas del sistema. Cambiable sin tocar el código (`cmake/FFmpeg.cmake`).

### Decisiones pendientes
- [ ] Atajos de teclado: copiar el mapa de Premiere tal cual o uno propio.
- [ ] Instalador: Inno Setup o NSIS (decidir al llegar a la fase 2).


---

## 15. Estilo visual del editor: "Marea Profunda"

Marea Nocturna es una identidad de stream: saturada, con pegatinas, rebotes y
destellos. Un editor se mira durante horas y necesita lo contrario: calma visual
y neutralidad de color. Por eso la interfaz usa una variante sobria.

### Principios
- **Base neutra:** grises carbón con un matiz azul muy leve, como DaVinci o
  Premiere. Nada de fondos saturados.
- **El visor manda:** su entorno es casi negro y neutro (`#0B0C10`) para no
  alterar la percepción del color del video.
- **La marca en acentos:** Bioluz suavizado para foco y selección; Bioluz puro
  solo en el cabezal de reproducción.
- **Texto sin blanco puro** y contraste suficiente (WCAG AA) para leer cómodo.
- **Animaciones discretas y rápidas** (100–200 ms, sin rebotes) en la interfaz.
  Los rebotes y destellos de la marca quedan para la pantalla de inicio y
  momentos puntuales (p. ej. exportación terminada).
- **Clips de colores apagados** en la línea de tiempo, para que no compitan con
  el video.

### Paleta
| Rol | Color |
|---|---|
| Fondo | `#14161C` |
| Panel | `#1B1E26` |
| Panel elevado / hover | `#232733` |
| Bordes | `#2C3140` |
| Entorno del visor | `#0B0C10` |
| Texto principal | `#E6E8EE` |
| Texto secundario | `#9AA3B5` |
| Texto desactivado | `#5F6778` |
| Acento (foco, selección) | `#4FC3D1` (Bioluz suavizado) |
| Cabezal de reproducción | `#6FE3F0` (Bioluz) |
| Acento secundario | `#3A7BD5` (Aleta) |
| Clip de video / audio / transición / texto | `#3D5A8A` / `#2F6F6A` / `#7D74A8` / `#9C8340` |
| OK / aviso / error | `#4FBF8F` / `#E0B440` / `#D0465F` |

La paleta completa de Marea Nocturna sigue disponible en el tema
(`Theme.marca`) para la pantalla de inicio, el logo y los textos y títulos de
marca que se insertan en los videos.

### Tipografía
- Interfaz: fuente del sistema (Segoe UI en Windows) por legibilidad.
- Más adelante se puede incluir una fuente propia para la interfaz y las
  fuentes de marca (Fredoka, Chakra Petch…) para los títulos de los videos.

Implementación: `src/app/qml/Theme.qml`.
