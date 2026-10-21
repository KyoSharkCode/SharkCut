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
| GPU de referencia | NVIDIA **RTX 5070 12 GB** (antes RTX 3060 12 GB) | PC de trabajo de Kyo; NVENC/NVDEC por defecto |
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

### Fase 0 — Cimientos ✅
- Proyecto CMake + Qt, que compila en Windows (y en Linux para CI).
- GitHub Actions: compilación, tests y ejecutable descargable.
- Ventana Qt Quick con un visor QRhi.
- Abrir un MP4 con FFmpeg, decodificar por hardware (NVDEC) y mostrarlo.
- Reproducir el audio sincronizado; play, pausa y búsqueda precisa.
**Hecho cuando:** un clip real de OBS 1080p60 se reproduce fluido y sin
desincronía en SharkCut, y se puede saltar a cualquier fotograma.

### Fase 1 — Transiciones ✅
- Compositor con dos clips y las siete transiciones de prioridad 1.
- Panel de prueba con controles de intensidad, duración y easing.
- Marcas de entrada y salida (I/O) para elegir los tramos A y B dentro de un video,
  sin tener que cortar archivos aparte (la línea de tiempo completa es la fase 2).
- Exportación de prueba de esa mezcla con NVENC.
**Hecho cuando:** la calidad visual convence frente a Premiere Composer y el
resultado exportado es idéntico a la vista previa.

### Fase 2 — Editor básico ✅
- Biblioteca de medios (arrastrar y soltar archivos).
- Línea de tiempo: 1 pista de video + 1 de audio; cortar, recortar, mover,
  dividir, eliminar. Comandos + deshacer/rehacer desde el inicio.
- Guardar y abrir `.sharkp`.
- Exportar a MP4 1080p60 con NVENC.
- Interfaz con el tema Marea Profunda.
**Hecho cuando:** se puede montar y exportar un short sencillo sin Premiere.

### Fase 3 — Interfaz definitiva y montaje real ✅
Reordenada tras la fase 2 con los comentarios de Kyo (sección 14).
- ✅ Rediseño de la interfaz: distribución de paneles con un lugar para cada cosa
  (también para lo que llegará después: efectos, transiciones, propiedades del
  clip) e iconos de herramientas. Primero una maqueta aprobada; después, la
  implementación con el tema Marea Profunda. (El logo definitivo, los cursores y el
  icono de los .sharkp se diseñan con el instalador, en la fase 4.)
- ✅ Varias pistas de video (uno encima de otro) y de audio (música, etc.), con la
  onda del audio dibujada en la línea de tiempo, y posición y escala de cada clip
  (facecam encima, reencuadrar un video 16:9 en un lienzo vertical).
- ✅ Transiciones integradas en la línea de tiempo (arrastrar entre clips y
  ajustarlas ahí mismo) y efectos de sonido emparejados con cada transición.
**Hecho cuando:** se monta un short con facecam o música encima y transiciones
sin salir de la pestaña Editor.

### Fase 4 — Varios proyectos, uso diario y efectos
Reordenada al cerrar la fase 3 con los comentarios de Kyo (sección 14): la app queda
neutral (sin plantillas de marca, para compartirla con su círculo), la estructura se
parece a su espacio de trabajo de Premiere y los efectos que usa en sus shorts se
adelantan desde la antigua fase 5.
- Varios proyectos abiertos a la vez y varias secuencias por proyecto, como en Premiere:
  cada proyecto abierto es una pestaña del panel de medios (con sus carpetas, medios y
  secuencias) y cada secuencia abierta, de cualquier proyecto, es una pestaña de la línea
  de tiempo. Cada secuencia tiene su propio lienzo y fps ("Duplicar como vertical" pasa
  a ser duplicar una secuencia en 9:16).
- Nueva distribución: pestañas superiores Proyectos (recientes, nuevo, abrir), Editor y
  Exportar (configuración completa: formato, resolución, fps, códec, calidad, tramo,
  audio); el botón de la esquina pasa a ser "Exportar rápido" (última configuración).
  Panel Propiedades separado: Medios · Transiciones · Efectos | Propiedades | Programa.
  Se retiran las pestañas Origen y Transición (eran herramientas de las primeras fases).
- Edición rápida tipo Premiere: Q/W, ripple delete, J/K/L, marcadores, crear una pista al
  soltar un medio en la zona vacía de la línea de tiempo.
- Autoguardado y recuperación tras cierre inesperado.
- Rendimiento con VODs largos: visor sin copias (textura D3D11 de NVDEC directa al
  compositor), caché de fotogramas, proxies, línea de tiempo virtualizada.
- Efectos para los shorts (se concretan con Kyo al llegar a esa etapa; candidatos de la
  antigua fase 5: keyframes de posición/escala/rotación/opacidad, croma, LUTs básicas,
  máscaras simples, más transiciones).
**Hecho cuando:** se edita un VOD de 40+ minutos de principio a fin con fluidez, se
saltan proyectos y secuencias sin cerrar nada y los shorts llevan sus efectos sin
echar de menos Premiere.

### Fase 5 — Entrega
- Instalador, icono de los archivos .sharkp y asociación con SharkCut.
- Carpeta de licencias (FFmpeg, Qt, whisper.cpp/ggml —MIT—) y código disponible, para
  compartir SharkCut gratis. Los modelos de whisper se descargan aparte (no van en el
  instalador). ggml ya se compila sin GGML_NATIVE (base AVX2): lo compilado sirve en
  otros PC.
- Mejora de los efectos de sonido (generador o sonidos CC0 registrados).
- 0.1.0 Early Access para Kyo y sus amigos (.zip o instalador, y si se quiere una web
  sencilla de descarga).
**Hecho cuando:** los amigos de Kyo instalan SharkCut y editan con él.

### Fase 6 — Lo pro
- Lo que quede de efectos y transiciones tras la fase 4 (glitch, spin…).
- ~~Subtítulos automáticos en local con whisper.cpp~~: adelantados a la fase 4, etapa 5.
- Herramientas para VODs: detección de silencios y marcadores automáticos.
- Herramienta de texto genérica (sin plantillas de marca), si hace falta.

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
| Alcance que crece sin control | Respetar las fases; nada de una fase posterior antes de cerrar la actual |

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
     por fotograma, paso a paso. Validada en el PC de Kyo con un clip real de OBS
     (H.264 1920×1080 a 60 fps, 3:35): decodificación por GPU (d3d11va), Direct3D 11.
   - [x] Etapa 3: audio sincronizado. FFmpeg decodifica y convierte a float estéreo
     48 kHz alineado con el primer fotograma (silencio o recorte si el audio empieza
     antes o después); miniaudio (WASAPI) reproduce desde un búfer en anillo sin
     bloqueos y su contador de muestras, descontando la latencia, es el reloj maestro.
     Sin audio o sin tarjeta de sonido se usa un temporizador. Validada en el PC de Kyo
     con dos grabaciones de OBS (la más larga, 22 min): audio sincronizado, silenciar
     y parada al final correctos.
   - **Fase 0 terminada.** La optimización "sin copias" (textura de NVDEC directa al
     visor) pasa a la fase 4, donde importa (4K, varias pistas, VODs).
5. Fase 1: transiciones.
   - [x] Etapa 1: laboratorio de transiciones. Núcleo (curvas de suavizado, catálogo
     de transiciones, secuencia A → transición → B), compositor QRhi de cuatro
     pasadas (capa A, capa B, transición, presentación) compartido por visor y
     exportación, las 7 transiciones en un shader con desenfoques radial, de giro y
     direccional y bordes en espejo, audio de la secuencia con fundido cruzado de
     potencia constante, pestañas Origen (marcas I/O) y Transición (parámetros en
     directo, "Ver transición"). Herramienta `sharkcut_filmstrip` para revisar las
     transiciones fuera de la interfaz. Validada en el PC de Kyo con grabaciones de
     OBS (21 min + 3:35) y con clips verticales cortos (13 s y 1 min): las 7
     transiciones se ven igual que en Premiere Composer y el audio no se corta.
   - [x] Etapa 2: exportar la secuencia de prueba a MP4. SequenceExporter compone cada
     fotograma fuera de pantalla con el mismo Compositor (instancia QRhi propia en un
     hilo: D3D11 en Windows), lo lee de la GPU y Mp4Writer lo codifica en H.264 (NVENC,
     AMF, QuickSync o x264, el primero que funcione; BT.709 rango limitado) con audio
     AAC de la mezcla A/B. Comprobado en Linux que el fotograma exportado coincide con
     la vista previa. Validada en el PC de Kyo: exporta con h264_nvenc (56 s) y el
     MP4 se ve y se oye igual que en SharkCut.
   - **Fase 1 terminada.** Lo único que falta frente a Composer son los efectos de
     sonido de cada transición, previstos en la fase 3 (sección 7).
6. Fase 2: editor básico.
   - [x] Etapa 1: núcleo de la línea de tiempo, sin interfaz. Proyecto (ajustes,
     biblioteca de medios, pistas V1 y A1), clips enlazados video + audio, operaciones
     de edición (colocar sobrescribiendo, dividir, recortar inicio/final, mover,
     quitar dejando hueco), historial de deshacer/rehacer por instantáneas del proyecto
     y archivo `.sharkp` (JSON con `version`, rutas absolutas y relativas, guardado
     atómico). Todo con tests.
   - [x] Etapa 2: la línea de tiempo en la interfaz. Pestaña Editor con biblioteca de
     medios (importar en segundo plano, arrastrar y soltar), línea de tiempo V1/A1 con
     regla, zoom e imán, herramientas Selección (mover, recortar bordes) y Cuchilla,
     dividir en el cabezal, quitar, deshacer/rehacer. Reproducción: dos hilos de video
     (el clip actual y el siguiente preparado en su primer fotograma) y audio de la línea
     de tiempo con fundidos de 2 ms en los cortes. Desde Origen se envía el tramo marcado
     al final o al cabezal. Pendiente de validar en el PC de Kyo.
   - [x] Etapa 3: proyectos y exportación. Nuevo, Abrir, Guardar y Guardar como (lectura y
     escritura en segundo plano, aviso de medios que faltan, pregunta antes de perder
     cambios) y exportación de la línea de tiempo a MP4 con el mismo exportador de la
     fase 1, ahora genérico (prueba de transición o línea de tiempo). Comprobado en Linux
     que cada fotograma exportado coincide con su origen (cortes y huecos incluidos).
   - **Fase 2 terminada.** Validada en el PC de Kyo con dos VODs largos (40 y 21 min):
     corta, mueve y reproduce sin tirones, audio perfecto, exportación con h264_nvenc.
   - Comentarios de Kyo tras probarla (lo que echa de menos frente a Premiere), con la
     fase donde encaja cada uno según el plan actual:
     - Rediseñar la interfaz (la de ahora es provisional, para probar) → fase 3.
     - Iconos de herramientas y cursores, logo de la app e icono de los `.sharkp`
       (se pueden diseñar en Figma) → fase 3; el icono de archivo, con el instalador (fase 4).
     - Ver la onda del audio en la línea de tiempo → fase 3.
     - Poner videos uno encima de otro (varias pistas de video) → fase 3.
     - Añadir pistas de audio (música, etc.) arrastrando un archivo a la línea de tiempo
       o desde Medios → fase 3.
     - Aplicar la transición directamente entre dos clips de la línea de tiempo, sin pasar
       por Origen y el laboratorio → fase 3.
     - Keyframes para hacer zoom y movimiento dentro de un clip → fase 5.
     - Croma (pantalla verde) → fase 5 (efectos).
     - Subtítulos automáticos → fase 5 (whisper.cpp).
   - Con esos comentarios se reordenaron las fases 3 y 4 (sección 11): la fase 3 empieza
     por el rediseño de la interfaz, sigue con varias pistas y ondas, y termina con las
     transiciones en la línea de tiempo.
7. Fase 3: interfaz definitiva y montaje real.
   - [x] Etapa 1: rediseño de la interfaz, según la maqueta aprobada por Kyo. Panel
     izquierdo con pestañas (Medios con lista o cuadrícula de miniaturas y buscador,
     Transiciones, Efectos, Propiedades), visor de Programa a la derecha con el lienzo,
     línea de tiempo alta (V2, V1, A1, A2; V2 y A2 llegan en la etapa 2) con las
     herramientas en vertical a su izquierda y el medidor de la mezcla (L/R en dB) a su
     derecha, iconos vectoriales propios, Exportar en la esquina inferior derecha con
     su ventana (nombre, ubicación, calidad) y "Nuevo proyecto" con el lienzo (16:9,
     9:16, 1:1) y los fps: el lienzo es del proyecto y se exporta tal cual se edita.
     Validada en el PC de Kyo (proyecto vertical 1080×1920 con clips de OBS): "se ve
     de maravilla". Las pestañas Transiciones, Efectos y Propiedades muestran de
     momento lo que llegará; la onda del audio en los clips llega en la etapa 2.
   - [x] Etapa 2: varias pistas y reencuadre.
     - Modelo: pistas de video y audio en número variable (V1, V2… encima; A1, A2…),
       añadir y quitar pistas, clips enlazados entre su pista de video y la de audio
       (con "Desvincular audio y video" para editarlos por separado, p. ej. quitar el
       audio de la facecam). Ocultar (video), silenciar (audio) y bloquear cada pista,
       también como pasos de deshacer. Archivo `.sharkp` versión 2 (pistas, enlaces,
       transformación y volumen), que abre los de la versión 1.
     - Compositor con N capas apiladas (V2 sobre V1…) y transformación por clip:
       posición, escala, opacidad y recorte (necesario para sacar la facecam de la propia
       grabación de OBS). Cada capa se dibuja directamente en el lienzo con alfa
       premultiplicado; las transiciones del laboratorio siguen igual. Mismo motor en
       vista previa y exportación; las pistas ocultas no se ven ni se exportan.
     - Propiedades del clip editables (números que se arrastran o se escriben, con vista
       previa en directo y un solo paso de deshacer al soltar; Ajustar, Rellenar y
       Restablecer) y arrastrando en el visor: clic sobre un clip lo selecciona (el de más
       arriba), arrastrar lo mueve con guías de bordes y centro (Mayús: sin ajuste) y las
       esquinas lo escalan.
     - Audio: mezcla de todas las pistas que no están silenciadas, volumen por clip (dB)
       en Propiedades. Mover la facecam o cambiar un volumen no reinicia los hilos de
       video (y lo primero tampoco el audio).
     - Onda del audio de cada clip, calculada en segundo plano (dos a la vez) y guardada
       en `<proyecto>.sharkp.cache/ondas/` (en memoria mientras el proyecto no tiene
       archivo); escala logarítmica para que se vea también la voz.
     - Línea de tiempo: arrastrar un medio a cualquier pista (la música a A2; un video
       soltado en V2 lleva su audio a A2), mover clips a otra pista del mismo tipo
       arrastrando en vertical (su enlazado del otro tipo se queda en su pista), menú con
       clic derecho en cabeceras (añadir y quitar pistas) y clips (desvincular, ajustar,
       rellenar, dividir, quitar), desplazamiento vertical con muchas pistas.
     - Comprobado en Linux: un short vertical 1080×1920 con gameplay rellenando el lienzo
       en V1, facecam recortada en V2 y música a −6 dB en A2 se ve en el visor igual que
       en el MP4 exportado, y suena la mezcla.
     - Primera prueba de Kyo (VODs reales): mover un clip a otra pista hacía saltar una
       aserción de MSVC (se buscaba destino también para pistas sin clips que mover);
       corregido con test, y en Linux Debug ahora se activa `_GLIBCXX_ASSERTIONS` para
       ver estos fallos igual que en Windows. La onda se dibuja de abajo arriba (raíz de
       la amplitud: −12 dB llega a la mitad) y muestra "Calculando la onda…" mientras se
       calcula.
     - Validada en el PC de Kyo con VODs reales de OBS (proyecto de 11 min con V1, V2, A1
       y A2, cortes y clips movidos entre pistas): se ve, se oye y se exporta bien, y la
       exportación sigue sin problemas aunque se abra otro proyecto mientras tanto.
     - Hecho cuando: un short vertical con el gameplay reencuadrado en V1, la facecam
       en V2 y música en A2 se ve, se oye y se exporta igual.
   - [x] Etapa 3: transiciones y sonido en la línea de tiempo.
     - Arrastrar una transición de la pestaña Transiciones al corte entre dos clips
       seguidos de una pista de video (se ilumina el corte más cercano). Cambio respecto a
       lo previsto ("los clips se solapan"): la transición se centra en el corte y usa el
       material que sobra del medio antes y después de cada clip, como en Premiere; si no
       hay, repite el primer o el último fotograma. Así ningún clip se mueve y el audio y
       las demás pistas siguen sincronizados.
     - Ajustarla ahí mismo: duración arrastrando sus bordes (antes y después del corte, por
       separado); tipo, duración, intensidad, desenfoque, curva y sonido en Propiedades
       (se abre al soltarla). Supr o clic derecho para quitarla. Se reproduce (dos hilos de
       decodificación por pista: saliente y entrante) y se exporta con el compositor. En
       una pista superior (la facecam) la transición no tapa lo de debajo: las capas de la
       transición son transparentes donde no hay clip.
     - Las transiciones siguen a las ediciones: si se parte el clip saliente pasan a su
       parte derecha; si los clips dejan de estar seguidos, desaparecen; su duración se
       limita a lo que cabe. Archivo `.sharkp` versión 3 (abre la 1 y la 2).
     - Biblioteca de efectos de sonido sintetizados por código (tools/sfx/generar_sfx.py),
       propios y CC0, registrados en presets/sfx/LICENCIAS.md: whoosh, whoosh que sube,
       whoosh que baja, swoosh, giro, impacto, glitch y riser. Cada transición lleva uno por
       defecto (Fade to Black, ninguno) que se coloca con su golpe en el corte, en la
       primera pista de audio libre y a −6 dB; es un clip normal (se puede mover, cambiar
       de volumen o quitar) y se cambia o se quita desde Propiedades.
     - Comprobado en Linux: arrastrar, ajustar, quitar y deshacer en la app; reproducir a
       través de la transición; exportar (V1 con Zoom In y V2 con un fundido que deja ver
       V1).
     - Validada en el PC de Kyo con VODs reales: las transiciones se añaden, se ajustan y
       se exportan en orden, con su efecto de sonido.
     - Comentario de Kyo: los efectos de sonido cumplen, pero suenan distintos a los que
       usa (sintetizados); hay cosas que mejorar → pendiente para una etapa posterior
       (retocar el generador o añadir sonidos CC0 de Freesound/Pixabay, registrados en
       presets/sfx/LICENCIAS.md).
   - **Fase 3 terminada.** Se monta un short con facecam, música y transiciones sin salir
     de la pestaña Editor.
   - Comentarios de Kyo al cerrar la fase 3, con la fase donde encaja cada uno:
     - Q y W como en Premiere: Q recorta desde el cabezal hacia atrás (quita del inicio
       del clip hasta el cabezal y cierra el hueco) y W desde el cabezal hacia delante →
       fase 4 (edición rápida).
     - Arrastrar un medio a la zona vacía de la línea de tiempo (encima de la pista de
       video más alta o debajo de la última de audio): que se vea que ahí se crea una
       pista nueva y que se cree al soltar → fase 4.
     - Mejorar los efectos de sonido → fase 4.
8. Fase 4: varios proyectos, uso diario y efectos. Etapas acordadas con Kyo:
   - Decisiones al planificarla: sin textos ni títulos con la marca (la app queda neutral
     para compartirla); se retiran Origen y Transición (Kyo no usaba Origen); varios
     proyectos abiertos y varias secuencias por proyecto, como en su espacio de trabajo
     de Premiere; efectos adelantados desde la antigua fase 5 y la entrega pasa a la 5.
   - [x] Etapa 1: proyectos y secuencias. Núcleo: `Document` (src/core/document.h) con
     medios, carpetas (un nivel) y secuencias (cada una con su lienzo, fps y pistas); las
     operaciones de edición siguen trabajando sobre un `Project`, que es la vista de una
     secuencia (`Document::view`/`store`), y el historial es por proyecto
     (`BasicHistory<Document>`). `.sharkp` versión 4 (guarda también las secuencias
     abiertas y la activa); los archivos 1–3 se abren como un proyecto de una secuencia.
     Interfaz: proyectos abiertos como pestañas en Medios (con × y aviso de cambios sin
     guardar; al cerrar la ventana se pregunta por cada uno), carpetas (crear, renombrar,
     quitar, entrar y soltar medios o secuencias encima), secuencias en la biblioteca
     (doble clic para abrir; menú: renombrar, duplicar, duplicar como vertical 9:16,
     quitar), pestañas de secuencias en la línea de tiempo (de cualquier proyecto; doble
     clic para renombrar, × o botón central para cerrar, «+» para una nueva). Quitar un
     medio lo quita de todas las secuencias. Pedidos de Kyo en la etapa: barra de
     desplazamiento vertical visible en la línea de tiempo y pistas de video en tono
     azulado y de audio en tono verdoso.
   - [x] Etapa 2: nueva distribución y exportación. Pestañas superiores Proyectos (nuevo,
     abrir, proyectos abiertos, recientes y datos del sistema; es la pestaña con la que
     arranca la app), Editor y Exportar. Editor: Medios · Transiciones · Efectos |
     Propiedades | Programa. Exportar: secuencia (la activa; elegir otra la abre), nombre
     y carpeta, tramo (completa o desde/hasta en código de tiempo, con «Cabezal»), códec
     H.264 o H.265, resolución (la de la secuencia o escalas con su forma: 200, 133, 67 y
     50 %), fps (los de la secuencia, 60, 50, 30, 25, 24), calidad (Ligera, Alta, Máxima
     con la tasa ajustada a la resolución y los fps, o a medida en Mbps) y audio (AAC
     128/192/320 kbps o sin audio); vista previa, resumen con el tamaño aproximado y
     «Abrir carpeta» al terminar. «Exportar rápido» (esquina inferior, Ctrl+Mayús+M)
     exporta la secuencia activa entera con los últimos ajustes, a la última carpeta y
     con el nombre de la secuencia. Nunca se reemplaza un archivo: «nombre (2).mp4». Se
     retiran Origen, Transición y su código (Player, TransitionLab); la prueba de
     transición sigue en tools/filmstrip.
   - [x] Etapa 3: edición rápida y trabajo seguro. Con rizo, como Premiere con bloqueo de
     sincronización (se desplaza todo lo posterior en las pistas no bloqueadas, y los
     marcadores): Q quita desde el borde anterior hasta el cabezal y W desde el cabezal
     hasta el borde siguiente; Mayús+Supr (o el menú del clip) elimina con rizo; clic
     derecho en un hueco: «Cerrar este hueco». Si otras pistas tienen clips en el tramo,
     el hueco no se cierra y se avisa. J/K/L: atrás, pausa, adelante; otra pulsación dobla
     la velocidad (hasta 8×), sin sonido salvo a 1×; hacia atrás se muestra fotograma a
     fotograma según llegan. Marcadores por secuencia (campo opcional de la versión 4 del
     .sharkp): M pone uno, Mayús+M / Ctrl+Mayús+M van al siguiente / anterior; en la
     regla se arrastran, doble clic para el nombre, clic derecho para quitarlos. Zona
     vacía encima de las pistas de video y debajo de las de audio: soltar un medio ahí
     crea pistas nuevas (una de video y una de audio, las que necesite). Autoguardado:
     cada 2 minutos, una copia de cada proyecto con cambios en la carpeta de datos de la
     app (recuperacion/); se borra al guardar o cerrar el proyecto. Si la app no se cerró
     bien, Proyectos ofrece recuperar o descartar cada copia (al recuperar, el proyecto
     se abre con sus cambios sin guardar y se guarda en su archivo original). Atajos que
     cambian: M ya no silencia (es marcador; el botón de sonido sigue en el visor) y
     «Exportar rápido» pasa a Ctrl+E.
   - [x] Etapa 4: rendimiento con VODs largos. Medido con tools/bench (sharkcut_bench) y
     un VOD de prueba de 10 min 1080p60 (MP4 fragmentado, clave cada 2 s, como OBS) y
     un proyecto de 3 h troceado en ~8.700 clips:
     - Línea de tiempo virtualizada (TimelineClipsModel): solo existen los clips
       visibles (más media pantalla a cada lado) y cada edición actualiza solo las filas
       que cambian; alejada, los clips de menos de 16 px se agrupan en bloques («N clips
       · acércate para editarlos»). Dividir pasó de 14 s a ~20 ms; el peor salto de zoom,
       de 1,9 s a ~0,35 s (medido en una pantalla virtual con dibujado por software).
       Imán, corte más cercano y grupos de arrastre se calculan en C++.
     - Decodificación: saltos cortos hacia delante sin volver al fotograma clave
       (+10 fotogramas: de ~47 a ~7 ms) y caché de ~64 MB por hilo que, al ir hacia
       atrás, guarda los fotogramas anteriores al destino (J: de ~14 a ~177 fps).
     - Proxies (540p, clave cada 15 fotogramas, mismos índices de fotograma que el
       original) en la caché del proyecto, creados en segundo plano desde Medios («Crear
       proxy», «Crear proxies de todos los videos»); botón «Proxies» en Programa. Solo
       para la vista previa: la exportación usa siempre los originales. Con proxy, un
       salto al azar baja de ~47 a ~6 ms.
     - Arreglo pedido por Kyo al probarla: los videos con transparencia (.mov ProRes
       4444, Animación/qtrle, PNG…) conservan su alfa en la vista previa y al exportar
       (plano de alfa en VideoFrame y en el compositor). No tienen proxy (H.264 no
       guarda transparencia). Los .webm VP9 con alfa necesitarían el decodificador
       libvpx: quedan pendientes.
     - Arreglos tras el primer video editado por Kyo: flechas arriba/abajo saltan al
       corte anterior/siguiente de cualquier pista; clic o arrastre en la zona de pistas
       mueve el cabezal (con imán a cortes y marcadores) y una línea tenue con su tiempo
       sigue al ratón; los clips de video muestran fotogramas en miniatura
       (FrameThumbnailProvider, del proxy si existe); el zoom con la rueda ya no deja la
       vista atascada pasado el final.
     - Pendiente: visor sin copias (textura D3D11 de NVDEC directa al compositor). Es
       código solo de Windows que no se puede compilar ni probar en la nube: se hará en
       una sesión de prueba con Kyo en su PC.
   - [ ] Etapa 5: efectos para los shorts. Kyo usa en Premiere estos ajustes guardados
     (presets con nombre, que se aplican arrastrándolos a un clip), que sirven de lista:
     - Movimiento (animaciones de posición/escala con keyframes): Acercamiento Motion
       Kyo3D, Alejamiento Motion Kyo3D, Acercamiento Reacción, Chat Motion, Disk
       Description Motion, Motion Prefarm.
     - Posición fija: KyoChibi Position, Logo Clip Position.
     - Blanco y negro, desenfoque (Blur), esquinas redondeadas (Edges, Chat Edges),
       recorte (Crop, ya existe; Crop Description Disk como preset), croma (GreenScreen)
       y sombra paralela / resplandor azul detrás de un clip.
     - [x] Adelantado a petición de Kyo: pantalla verde (croma) por clip. Es un efecto de
       la pestaña Efectos: se arrastra sobre un clip de video (o «Aplicar» al seleccionado),
       busca solo el color del fondo y aparece en Propiedades, donde se ajusta o se quita:
       color (escrito, «Verde», «Azul» o «Detectar el color del fondo», que lo busca en
       los bordes del fotograma que se ve), similitud, suavidad y quitar reflejo. Compara
       el tono relativo al brillo, así quita también el fondo mal iluminado. Archivo
       .sharkp versión 5. Y lectura de videos AV1 (OBS): NVDEC si la tarjeta lo admite,
       si no dav1d por software.
     - [x] Adelantado a petición de Kyo (lo necesitaba para los shorts): subtítulos.
       «Crear subtítulos a partir de la transcripción» (pestaña Subtítulos), como en
       Premiere: transcribe en local con whisper.cpp (modelo grande «turbo» o pequeño,
       descargado una sola vez; sin internet después) y divide en subtítulos con una
       línea, dos o una sola palabra, caracteres por línea, duración mínima, separación y
       quitar puntuación; se puede volver a dividir sin transcribir otra vez (cada
       subtítulo guarda el momento de sus palabras). Pistas de subtítulos S1, S2… encima
       de las de video: mover, recortar, editar el texto y el estilo (fuente, tamaño,
       negrita, cursiva, mayúsculas, color, contorno, sombra, caja de fondo, posición y
       ancho) de uno, de varios o de toda la pista; ajustes de estilo de fábrica y
       guardados con nombre; importar y exportar .srt. Sirven también como texto suelto
       («Añadir un subtítulo o texto en el cabezal»). Archivo .sharkp versión 6.
     - [x] Fallo arreglado (17–20 oct 2026, volcado ultimo-cierre.dmp del PC de Kyo): la
       app se cerraba al transcribir con 0xC000001D (instrucción no válida), en una
       instrucción AVX-512 de ggml; el procesador de Kyo (AMD Zen 3) no tiene AVX-512. Con
       MSVC y GGML_NATIVE=ON, la detección de ggml (FindSIMD.cmake) daba AVX-512 por bueno.
       Ahora cmake/whisper.cmake fija una base AVX2 (SSE4.2, AVX, AVX2, BMI2, FMA, F16C;
       sin AVX-512 ni GGML_NATIVE), que además sirve para repartirlo en la fase 5, y
       transcribe() comprueba AVX2 antes de empezar (mensaje claro en un PC sin él).
       Alternativa futura, más rápida en cada PC: GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS.
     - [x] 5.1 Keyframes (orden acordado con Kyo: keyframes → efectos → ajustes guardados →
       anclaje → .webm con transparencia → mover subtítulos en el visor). Posición, escala,
       opacidad y recorte se animan con ⏱ en Propiedades; con la propiedad animada, cada
       cambio (en Propiedades o arrastrando en el visor) crea o actualiza el keyframe del
       cabezal (también la rotación, añadida tras la prueba de Kyo: en Propiedades y con un
       tirador en el visor, Mayús de 15° en 15°); ◀ ◆ ▶ para ir al anterior, poner o quitar
       uno, ir al siguiente; curva por
       keyframe (lineal, suave, entrada suave, salida suave, explosiva, con rebote, cúbica
       y mantener). Los keyframes se ven (◆) sobre el clip en la línea de tiempo: clic para
       ir, arrastrar para moverlos. Siguen al contenido al recortar o dividir el clip.
       Archivo .sharkp versión 7.
     - [x] 5.2 Efectos: blanco y negro (intensidad), desenfoque (cantidad), esquinas
       redondeadas (radio) y sombra o resplandor (color, opacidad, difuminado, tamaño y
       distancia), que sigue la forma del clip: su transparencia y su pantalla verde (el
       resplandor azul detrás del modelo VTuber). Se arrastran desde Efectos a un clip (o
       «Aplicar» al seleccionado), aparecen en Propiedades y se quitan con 🗑️. En el mismo
       shader de la capa: vista previa y exportación iguales. Archivo .sharkp versión 8.
       Los valores de los efectos también se animan con keyframes (⏱ en cada uno), como
       la posición o la escala.
     - [x] 5.3 Ajustes guardados (presets de clips): «Guardar como ajuste…» en
       Propiedades guarda el aspecto del clip de video con nombre, eligiendo qué lleva
       (posición, escala y rotación; opacidad; recorte; efectos; pantalla verde; animación).
       Sin animación se guardan los valores que se ven en el cabezal; con ella, los
       keyframes cuentan desde el inicio del clip (entradas) o desde el final (salidas),
       en microsegundos: valen para clips de cualquier duración y fps. Aparecen en la
       pestaña Efectos, debajo de los efectos: se arrastran a un clip (o «Aplicar» al
       seleccionado), se renombran y se borran. Las partes que lleva sustituyen a las del
       clip (con deshacer); lo demás no cambia. Se guardan en la configuración de la app
       (QSettings «efectos/ajustes», JSON): sirven en todos los proyectos. Núcleo sin Qt:
       src/core/clip_preset.h.
     - [x] Petición de Kyo: cuadro de selección en la línea de tiempo (arrastrar desde una
       zona vacía; Mayús o Ctrl suman a lo seleccionado; un clic suelto sigue quitando la
       selección y llevando el cabezal ahí). Los efectos y los ajustes guardados que se
       sueltan sobre uno de los clips seleccionados (o con «Aplicar») van a todos sus clips
       de video en un paso de deshacer (la pantalla verde busca el color en ese clip y lo
       pone en todos), y «Desvincular audio y video de todos» separa los de la selección.
     - [x] Tras la prueba de Kyo con 3 VODs de una hora en vertical: los atajos (Espacio, C,
       V, Supr) ya no se pierden si un campo de texto se quedó con el teclado; franja
       amarilla en la regla (ahí se lleva el cabezal) y un clic para llevar el cabezal
       pausa; miniaturas de la línea de tiempo con el fotograma clave cercano (12 veces
       menos CPU), en 2 hilos de baja prioridad y cancelables; Propiedades no se rehace en
       cada fotograma al reproducir; la vista deja de seguir al cabezal si se mueve con la
       rueda. Copiar, cortar y pegar clips (Ctrl+C/X/V, también entre secuencias y
       proyectos): en el cabezal, en las mismas pistas, sobrescribiendo.
       Después, a petición de Kyo (seguía el lag al hacer zoom): cada clip de video muestra
       solo su primer fotograma, al inicio, en vez de una tira de miniaturas.
     - [x] Transiciones como clips (petición de Kyo, como en Premiere Composer): se
       arrastran a la línea de tiempo y quedan como un clip en una pista libre, centradas en
       el corte más cercano, con su efecto de sonido en una pista de audio. Afectan a todo
       lo que hay debajo (también varias pistas: gameplay, facecam y textos), y se pueden
       poner al principio o al final de un clip, mover, cortar y recortar. En Propiedades:
       tipo, duración, intensidad, desenfoque y curva. Archivo .sharkp versión 9.
     - [x] Biblioteca de efectos de sonido (pestaña Sonidos), con los que Kyo usa en
       Premiere como guía (sin copiarlos: sintetizados, CC0): clic de ratón, clic suave,
       pop hueco, pop de burbuja y más swooshes, además de los whoosh, impactos, glitch y
       riser de las transiciones. Se arrastran a una pista de audio o se ponen con su golpe
       en el cabezal (+).
     - [ ] Idea de Kyo (sin decidir, para más adelante): efectos animados de fábrica, como
       los ajustes de estilo del texto; p. ej. un desenfoque que aparece en barrido
       diagonal de izquierda a derecha. Encaja con los ajustes guardados (5.3: serían
       ajustes de fábrica en la misma lista); un barrido
       necesitaría una máscara de desenfoque con dirección, que hoy no existe.
     Hace falta: keyframes de las propiedades del clip, efectos con parámetros en el
     compositor (mismo motor para vista previa y exportación) y presets de usuario con
     nombre (guardar, aplicar arrastrando, renombrar, quitar).
     - [x] 5.4 Punto de anclaje (idea de Kyo, seguimiento de movimiento): un clip (texto,
       imagen, video pequeño) se pega a algo que se mueve en otro clip, por ejemplo una
       mano, y lo sigue. En Propiedades, «Punto de anclaje»: se elige el clip que se sigue
       (los que se ven en el cabezal) y se dibuja en el visor un recuadro sobre la zona.
       SharkCut la busca fotograma a fotograma desde el cabezal hacia delante, en segundo
       plano y con el proxy si lo hay (src/media/motion_tracker.h: correlación cruzada
       normalizada sobre la luminancia, de baja resolución a fina, con ajuste por debajo
       del píxel y un patrón que se adapta poco a poco sin derivar; si la zona se tapa unos
       fotogramas, rellena entre la posición de antes y la de después). El recorrido pasa
       al lienzo con la colocación del clip seguido en cada momento (escala, giro,
       posición, también animadas) y se convierte en keyframes de posición del clip
       anclado (src/core/motion_anchor.h), sin los que sobran (a menos de 1 px de la
       línea entre sus vecinos), que se pueden corregir a mano. Si la zona se pierde, el
       clip la sigue hasta ahí y se avisa para marcarla otra vez desde ese punto. Un paso
       de deshacer.
       Pendiente (opcional): dos puntos para seguir también escala y giro.
9. Fase 5: entrega (instalador, licencias, sonidos, 0.1.0 Early Access).

### Decisiones tomadas
- [x] Nombre: **SharkCut**.
- [x] Arquitectura: **nativa en C++20 + Qt 6** (revisión 2; antes Electron).
- [x] Transiciones prioritarias: Fade to Black, Pan (Left), Rotate,
      Shake & Distort, Zoom In, Zoom Out, Zoom & Rotate Out.
- [x] Tarjeta gráfica: NVIDIA RTX 5070 12 GB (desde octubre de 2026; antes RTX 3060) →
      NVDEC/NVENC por defecto.
- [x] Proyecto por defecto: 1080p60.
- [x] Grabación de OBS: MP4 fragmentado, una sola pista de audio.
- [x] Extensión del proyecto: `.sharkp`.
- [x] Kyo Estudio: SharkCut se hace desde cero; conexión eventual más adelante.
- [x] Lenguaje: C++ (Rust descartado por la madurez de sus bibliotecas de interfaz).
- [x] Estilo visual del editor: tema "Marea Profunda" (sección 15).
- [x] SharkCut se comparte gratis con el círculo de Kyo: la app queda neutral (sin
      plantillas de marca) y se cumplen las licencias de FFmpeg y Qt (fase 5).
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
| Franja de la regla (donde se mueve el cabezal) | `#E0B440` (petición de Kyo) |
| Acento secundario | `#3A7BD5` (Aleta) |
| Clip de video / audio / transición / texto | `#3D5A8A` / `#2F6F6A` / `#7D74A8` / `#9C8340` |
| Fila de pista de video / audio | `#161C29` / `#14201F` |
| Cabecera de pista de video / audio | `#1F2940` / `#1C2E2C` |
| OK / aviso / error | `#4FBF8F` / `#E0B440` / `#D0465F` |

La paleta completa de Marea Nocturna sigue disponible en el tema
(`Theme.marca`) para la pantalla de inicio, el logo y los textos y títulos de
marca que se insertan en los videos.

### Tipografía
- Interfaz: fuente del sistema (Segoe UI en Windows) por legibilidad.
- Más adelante se puede incluir una fuente propia para la interfaz y las
  fuentes de marca (Fredoka, Chakra Petch…) para los títulos de los videos.

Implementación: `src/app/qml/Theme.qml`.
