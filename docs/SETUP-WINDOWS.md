# Preparar el PC para desarrollar SharkCut (Windows)

Guía paso a paso. Solo hay que hacerla una vez.

## 1. Compilador de C++ (Build Tools for Visual Studio)

1. Abre el **Visual Studio Installer**.
2. En **Build Tools** pulsa **Modificar**.
3. Marca la carga de trabajo **"Desarrollo para el escritorio con C++"**.
4. En el panel de la derecha comprueba que estén marcados:
   - **MSVC** (herramientas de compilación de C++ x64/x86, la versión más reciente).
   - **Windows SDK** (Windows 11).
   - **Herramientas de CMake de C++ para Windows** (instala CMake y Ninja).
5. Pulsa **Modificar** y espera a que termine.

## 2. Qt 6.10.3

1. Descarga el **Qt Online Installer** desde qt.io (sección *Download → Open Source*).
   Pide crear una cuenta de Qt gratuita.
2. Elige **Instalación personalizada** y carpeta **`C:\Qt`**.
3. En la lista de componentes, despliega **Qt → Qt 6.10.3** y marca solo:
   - **MSVC 2022 64-bit**
   - **Additional Libraries → Qt Shader Tools**
   > Si no aparece 6.10.3, marca el filtro **Archive** a la derecha y pulsa **Filtrar**.
   > Las bibliotecas "MSVC 2022" funcionan con Build Tools 2026 sin problema.
4. Desmarca todo lo demás (Android, WebAssembly, Qt Creator opcional, etc.) para no
   descargar gigas que no se usan.
5. Instala.

## 3. Variables de entorno

1. Pulsa Inicio y busca **"Editar las variables de entorno de esta cuenta"**.
2. En **Variables de usuario**, pulsa **Nueva…**:
   - Nombre: `QT_ROOT_DIR`
   - Valor: `C:\Qt\6.10.3\msvc2022_64`
3. Selecciona la variable **Path**, pulsa **Editar… → Nuevo** y añade:
   - `C:\Qt\6.10.3\msvc2022_64\bin`
   > Esto permite ejecutar SharkCut desde la carpeta de compilación (encuentra las DLL de Qt).
4. Acepta todo y **cierra y vuelve a abrir VS Code** para que lea las variables nuevas.

## 4. VS Code

Extensiones (VS Code las sugiere al abrir el proyecto):

- **C/C++** (Microsoft)
- **CMake Tools** (Microsoft)
- **Qt Extension Pack** (The Qt Company)
- *(opcional)* **Shader languages support** (slevesque)

## 5. Compilar y ejecutar

1. Abre la carpeta `C:\Dev\SharkCut` en VS Code.
2. CMake Tools preguntará por un **preset de configuración**: elige **Windows · Debug**.
   (Si no pregunta: `Ctrl + Shift + P` → **CMake: Select Configure Preset**.)
   La primera vez descarga Catch2 (biblioteca de tests) y **FFmpeg 9.0.2 precompilado**
   (unos 60 MB, versión y huella fijadas en `cmake/FFmpeg.cmake`). Se guarda en la carpeta
   `.deps` del proyecto y solo se descarga una vez.
3. Compilar: **F7** o el botón **Build** de la barra inferior.
4. Ejecutar: `Shift + F5`, o el botón ▶ de la barra inferior (objetivo **SharkCut**).
5. Tests: `Ctrl + Shift + P` → **CMake: Run Tests**, o en la terminal:
   ```
   ctest --preset windows-debug
   ```

### Si la configuración falla diciendo que no encuentra el compilador (`cl`)

CMake Tools no ha detectado Build Tools. Alternativa que siempre funciona:

1. Inicio → busca **"x64 Native Tools Command Prompt"** (de Build Tools) y ábrelo.
2. Escribe `cd C:\Dev\SharkCut` y luego `code .`
3. VS Code se abre con el entorno del compilador ya cargado.

## 6. Qué comprobar

### Fase 3, etapa 1: la interfaz nueva (validada)

La pestaña **1 · Editor** tiene ya la distribución de la maqueta.

- [ ] **Izquierda**, panel con pestañas: **Medios** (buscador, botón lista/cuadrícula,
  Importar…), **Transiciones**, **Efectos** y **Propiedades** (las tres últimas
  muestran lo que llegará; Propiedades enseña el clip seleccionado).
- [ ] En **cuadrícula**, cada video muestra una **miniatura** (tarda un momento la primera
  vez) con su duración. Arrastrar y el botón **+** funcionan igual que en la lista.
- [ ] **Derecha**, el visor **Programa** con la etiqueta del lienzo (`LIENZO 16:9`).
- [ ] **Abajo**, la línea de tiempo con las pistas **V2, V1, A1, A2** (V2 y A2 atenuadas:
  llegan en la etapa 2) y las **herramientas en vertical** a la izquierda. Al pasar el
  ratón por cada icono aparece su nombre y su atajo.
- [ ] A la derecha de la línea de tiempo, el medidor **Mezcla**: al reproducir, las barras
  L y R suben y bajan con el volumen (verde, amarillo cerca del máximo, rojo si satura).
- [ ] **Exportar** está abajo a la derecha. Abre una ventana con el formato (el lienzo
  del proyecto), **nombre**, **ubicación** (Elegir…) y **calidad**. El progreso se ve
  abajo, junto al botón, y se puede cancelar.
- [ ] **Nuevo** pide nombre, **lienzo** (16:9, 9:16 o 1:1) y fps. Con 9:16 el visor
  muestra el lienzo vertical; un video de OBS entra ajustado (con franjas negras):
  moverlo y escalarlo para reencuadrar llega en la etapa 2.
- [ ] ¿La distribución se ve bien en tu pantalla? ¿Algo que cambiarías?

### Fase 2: editor básico (línea de tiempo, proyectos y exportación) (validada)

SharkCut abre ahora en la pestaña **1 · Editor**: visor arriba, biblioteca de medios a
la derecha y línea de tiempo abajo, con una pista de video (**V1**) y una de audio (**A1**).
Las pestañas **2 · Origen** y **3 · Transición** siguen como antes.

**Montar:**

- [ ] **Importar…** (o `Ctrl + I`, o arrastrar archivos desde el explorador a la
  biblioteca) añade los videos a **Medios**, con su duración y formato.
- [ ] Arrastra un medio de la biblioteca a la línea de tiempo: aparece una línea donde va a
  caer y, al soltar, el clip se coloca ahí (video en V1 y audio en A1, enlazados).
  El botón **+** lo añade al final.
- [ ] El primer clip fija el formato del proyecto (panel **Proyecto**: `1920×1080 · 60 fps`).
- [ ] Doble clic en un medio lo abre en **Origen**: marca un tramo con I y O y envíalo con
  **Al final** o **En el cabezal**.
- [ ] También puedes arrastrar archivos del explorador directamente a las pistas.

**Editar** (con la herramienta **Selección**, tecla `V`):

- [ ] Clic en un clip lo selecciona (también su audio); `Ctrl`/`Mayús` + clic suma clips.
- [ ] Arrastrar un clip lo mueve. Si cae encima de otro, lo **sobrescribe**. El
  hueco que deja no se cierra solo (la eliminación con rizo llega en la fase 4).
- [ ] Arrastrar el **borde** izquierdo o derecho de un clip lo recorta. No deja pasar del
  principio o final del video ni pisar el clip vecino.
- [ ] Con el **Imán** (`S`) activado, los clips y los recortes se pegan a los bordes de
  otros clips y al cabezal.
- [ ] `Ctrl + K` divide los clips bajo el cabezal. La **Cuchilla** (`C`) divide donde haces clic.
- [ ] `Supr` (o Retroceso) quita los clips seleccionados.
- [ ] `Ctrl + Z` deshace y `Ctrl + Mayús + Z` (o `Ctrl + Y`) rehace, también varias veces.

**Ver:**

- [ ] Espacio reproduce la línea de tiempo con su audio. Al pasar por un corte la imagen
  y el sonido siguen fluidos (sin parones ni chasquidos). En los huecos se ve negro.
- [ ] Clic o arrastre en la regla (arriba de las pistas) mueve el cabezal.
- [ ] `Ctrl + rueda` hace zoom; la rueda sola desplaza. `+`, `-` y **Ajustar** (`\`) también.
- [ ] Prueba con un VOD largo (40 min) cortado en muchos trozos: ¿va fluido?

**Proyecto** (botones arriba a la derecha):

- [ ] **Guardar** (`Ctrl + S`) pide un nombre la primera vez y crea un archivo `.sharkp`.
  Mientras hay cambios sin guardar, el nombre del proyecto lleva un punto (•).
- [ ] Cierra SharkCut con cambios sin guardar: pregunta si quieres guardarlos.
  Lo mismo con **Nuevo** (`Ctrl + N`) y **Abrir…** (`Ctrl + O`).
- [ ] **Abrir…** el proyecto guardado: vuelve todo tal cual (clips, cortes, biblioteca).
  También vale hacer doble clic en el `.sharkp` si lo asocias a SharkCut, o
  `SharkCut.exe proyecto.sharkp`.
- [ ] Mueve la carpeta del proyecto junto con sus videos a otro sitio y ábrelo: los
  encuentra solo. Si falta un video, la barra de estado dice cuál.
- [ ] `Ctrl + Mayús + S` guarda con otro nombre.

**Exportar** (`Ctrl + M` o el botón **Exportar** de abajo a la derecha):

- [ ] Exporta la línea de tiempo entera a MP4. El panel derecho muestra el progreso, se
  puede cancelar y al final dice el tiempo y el códec (**h264_nvenc**).
- [ ] El MP4 se ve y se oye igual que en SharkCut: cortes, huecos en negro y audio en su sitio.
- [ ] Monta un short sencillo de principio a fin sin abrir otro editor. ¿Qué echas de menos?

### Fase 1: laboratorio de transiciones y exportación (validada)

Pestañas **2 · Origen** y **3 · Transición** (antes 1 y 2).

**Marcas de entrada y salida (monitor de origen).** En vez de cortar
los videos en archivos separados, eliges un tramo dentro de un video:

1. En **Origen**, abre un video y lleva el cabezal al punto donde quieres que empiece
   el tramo. Pulsa **I** (entrada).
2. Lleva el cabezal al punto donde quieres que termine y pulsa **O** (salida). El tramo
   queda resaltado en la barra. Sin marcas se usa el video entero.
3. Pulsa **Usar como clip A**.
4. Marca otro tramo (del mismo video o de otro) y pulsa **Usar como clip B**.
5. Ve a **3 · Transición** (o pulsa **Ir a la transición →**, o la tecla **3**).

> La transición solapa el **final de A** con el **principio de B**: deja un poco de
> margen al final del tramo A y al principio del B.

Atajo: `SharkCut.exe a.mp4 b.mp4` importa los videos y los coloca en la línea de tiempo.

**En la pestaña Transición:**

- [ ] La barra muestra A (azul), B (verde) y la zona de transición (morado).
- [ ] **Ver transición (T)** reproduce desde 1 s antes hasta 1 s después y se detiene.
- [ ] Prueba las 7 transiciones del selector **Tipo**: cada una carga sus valores por defecto.
- [ ] **Duración**, **Intensidad**, **Desenfoque** y **Curva** cambian el resultado al
  instante (también en pausa: avanza fotograma a fotograma con ← y →).
- [ ] El audio hace un fundido cruzado suave durante la transición.
- [ ] **Intercambiar A ↔ B** funciona.
- [ ] **Exportar prueba a MP4…** guarda la secuencia. El mensaje final indica el tiempo y
  el códec: debe ser **h264_nvenc** (tu RTX 3060). Ábrelo en otro reproductor y comprueba
  que se ve igual que en SharkCut, con el audio.
- [ ] Compara con las transiciones que usas hoy: anota cuáles se parecen y cuáles no, y qué cambiarías
  (más rápida, más desenfoque, otra curva…). ¡Capturas o videos del móvil ayudan mucho!

### Fase 0, etapa 3: audio sincronizado (validada)

Usa un clip de OBS en el que se te vea hablar (o con sonidos claros, como palmadas).

1. En el panel **Clip**, **Audio** debe indicar el códec (p. ej. `AAC · 48 kHz · estéreo`).
2. En **Sistema**, **Salida de audio** debe mostrar `WASAPI · <tus altavoces/auriculares>`.
3. Reproduce y comprueba:
   - [ ] Se oye el audio y la voz va **sincronizada con los labios** (o la palmada con la imagen).
   - [ ] Al saltar con la barra mientras reproduce, el audio continúa desde el punto nuevo
     y sigue sincronizado.
   - [ ] Pausa/reanudar (Espacio) no desincroniza.
   - [ ] **M** (o el botón **Sonido**) silencia y vuelve a activar el audio.
   - [ ] Reproduce el clip hasta el final: al terminar se detiene sola.
4. Si notas desfase, di si la voz va **adelantada** o **retrasada** respecto a la imagen.

### Fase 0, etapa 2: reproducir un clip de OBS (validada)

1. Abre SharkCut y pulsa **Abrir video…** (o `Ctrl + O`), o arrastra un MP4 al visor.
   También funciona `SharkCut.exe ruta\al\clip.mp4`.
2. En el panel **Clip** comprueba:
   - [ ] **Video:** códec y resolución correctos (p. ej. `H264 · 1920×1080`).
   - [ ] **Fotogramas por segundo:** `60`, sin la palabra "(variables)".
   - [ ] **Decodificación:** `GPU (d3d11va)` **en verde**. Si sale `CPU (software)` en
     amarillo, la GPU no se está usando: avisar.
3. Reproducción:
   - [ ] **Espacio** reproduce y pausa; el video va fluido y a velocidad real.
   - [ ] **← / →** avanzan o retroceden un fotograma; **Mayús + ← / →**, diez.
   - [ ] Al hacer clic en la barra salta a ese punto al instante.
   - [ ] Los colores se ven igual que en tu reproductor o en OBS (ni lavados ni saturados).
4. Envía una captura con el panel **Clip** visible.

### Fase 0, etapa 1: ventana y diagnóstico (validada)

Al ejecutar SharkCut debe abrirse una ventana oscura con el visor y el panel
**Diagnóstico**:

- [ ] **Qt:** `6.10.3`
- [ ] **Sistema:** tu versión de Windows
- [ ] **API gráfica:** `Direct3D 11` **en verde** (si sale en amarillo, avisar)
- [ ] **Fps del visor:** cercano a la frecuencia de tu monitor (60, 144…)
- [ ] El tiburón cruza el visor con movimiento suave, sin tirones
- [ ] Los tests pasan (`100% tests passed`)

Envía una captura de la ventana para validar.

## Alternativa sin compilar

Cada vez que se sube código, GitHub compila SharkCut en Windows. En el repositorio:
**Actions → Compilación → la ejecución más reciente → Artifacts → SharkCut-windows**.
Descomprime el zip y ejecuta `bin\SharkCut.exe`.
