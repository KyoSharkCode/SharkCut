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

### Etapa 2: reproducir un clip de OBS

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
   - [ ] Los colores se ven igual que en Premiere u OBS (ni lavados ni saturados).
4. Envía una captura con el panel **Clip** visible.

### Etapa 1: ventana y diagnóstico (validada)

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
