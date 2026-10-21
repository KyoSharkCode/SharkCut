# Licencias de los componentes de SharkCut

SharkCut usa estas bibliotecas de otros autores. Se reparten junto al programa en esta
carpeta (`licenses/`), con el texto de cada licencia.

SharkCut es software libre y gratuito bajo la licencia GPL 3.0 (`GPL-3.0.txt`; el archivo
`LICENSE` del repositorio). Su código está en <https://github.com/KyoSharkCode/SharkCut>.

| Componente | Para qué | Licencia | Texto | Código |
|---|---|---|---|---|
| FFmpeg 9.0.2 (compilación «full shared» de Gyan Doshi; incluye x264, x265, dav1d y otras) | Leer y escribir video y audio | GPL 3.0 (por x264/x265) | `FFmpeg.txt`, `GPL-3.0.txt` | <https://ffmpeg.org/download.html>, <https://github.com/GyanD/codexffmpeg/releases/tag/9.0.2> |
| Qt 6.10.3 (Core, Gui, Qml, Quick, Quick Controls, Shapes, Network) | Interfaz y dibujo por la GPU | LGPL 3.0 | `LGPL-3.0.txt`, `GPL-3.0.txt` | <https://download.qt.io/official_releases/qt/6.10/6.10.3/> |
| whisper.cpp 1.9.5 y ggml | Transcripción de los subtítulos | MIT | `whisper.cpp-MIT.txt` | <https://github.com/ggml-org/whisper.cpp> |
| miniaudio 0.11.25 | Salida de audio | Dominio público (Unlicense) o MIT-0 | `miniaudio.txt` | <https://github.com/mackron/miniaudio> |
| nlohmann/json 3.11.3 | Archivos de proyecto (.sharkp) | MIT | `nlohmann-json-MIT.txt` | <https://github.com/nlohmann/json> |

## Notas

- **Qt** va como bibliotecas dinámicas (`Qt6*.dll`): se pueden sustituir por otras
  versiones compatibles de Qt, como permite la LGPL.
- **FFmpeg** va como bibliotecas dinámicas (`av*.dll`, `sw*.dll`). La compilación incluye
  componentes GPL, así que el conjunto se reparte bajo la GPL 3.0. El código de esa
  versión exacta está en los enlaces de la tabla.
- Los **efectos de sonido** de `presets/sfx/` son propios de SharkCut (sintetizados) y
  van con licencia CC0 1.0. Ver `presets/sfx/LICENCIAS.md`.
- Los **modelos de whisper** no van con SharkCut: se descargan aparte la primera vez que
  se crean subtítulos (de <https://huggingface.co/ggerganov/whisper.cpp>, licencia MIT).
