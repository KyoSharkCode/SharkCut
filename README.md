# SharkCut

Editor de video gratuito y de código abierto para Windows, pensado para editar VODs
largos de Twitch y sacar de ellos clips y shorts.

**[Descargar SharkCut](https://kyosharkcode.github.io/SharkCut/)** ·
[Todas las versiones](https://github.com/KyoSharkCode/SharkCut/releases) ·
[Reportar un error o proponer una idea](https://github.com/KyoSharkCode/SharkCut/issues)

> SharkCut está en acceso anticipado: ya se puede usar de verdad, pero cambia a menudo y
> puede tener errores. Guarda tus proyectos con frecuencia.

<!-- Capturas: cuando las haya, añadirlas aquí (por ejemplo, docs/capturas/*.png). -->

## Qué hace

- **Hecho para VODs largos.** Proyectos de horas con fluidez: proxies para la vista
  previa, una línea de tiempo que no se atasca con miles de cortes y edición con rizo
  (Q/W, eliminar con rizo, cerrar huecos).
- **Shorts en vertical.** Lienzos 16:9, 9:16 y 1:1, varias secuencias por proyecto y
  reencuadre directamente en el visor.
- **Subtítulos automáticos.** Transcripción en tu equipo con whisper.cpp, sin subir tu
  audio a ningún sitio (el modelo se descarga una sola vez), división en subtítulos a tu
  gusto, estilos y archivos `.srt`.
- **Transiciones con sonido.** Zoom, giro, paneo, temblor y más, como clips que afectan a
  todo lo que tienen debajo, con su efecto de sonido enlazado.
- **Efectos y keyframes.** Pantalla verde, desenfoque, esquinas redondeadas, sombra y
  resplandor; posición, escala, rotación, opacidad y efectos animables con curvas.
- **Punto de anclaje.** Pega un texto o una imagen a algo que se mueve en el video y lo
  sigue solo.
- **Rápido por la GPU.** Decodifica y exporta con la tarjeta gráfica (NVDEC y NVENC en
  NVIDIA), con H.264 o H.265 y sonido AAC.
- **Edición no destructiva.** Tus archivos originales nunca se modifican.

## Requisitos

- Windows 10 u 11 de 64 bits.
- Recomendado: una tarjeta gráfica NVIDIA para decodificar y exportar por la GPU. Sin
  ella, SharkCut funciona por el procesador (más lento).
- Espacio en disco para la caché de cada proyecto (proxies, miniaturas y ondas).

## Instalar

1. Descarga el instalador (`SharkCut-X.Y.Z-Setup.exe`) desde la
   [página de descarga](https://kyosharkcode.github.io/SharkCut/). También hay una
   versión portable en `.zip`, sin instalar.
2. Ejecútalo. No pide permisos de administrador.
3. Si Windows avisa («Windows protegió tu PC»), pulsa **Más información → Ejecutar de
   todas formas**: SharkCut aún no tiene firma digital.

SharkCut busca actualizaciones al abrirse y te las ofrece. Se puede desactivar en la
pestaña Proyectos.
<!-- Apoyo: cuando haya página de Ko-fi, añadir aquí una sección «Apoya SharkCut» con el enlace. -->

## Ayuda y sugerencias

¿Algo falla o se te ocurre una mejora? Abre un *issue* en
[GitHub](https://github.com/KyoSharkCode/SharkCut/issues). No se aceptan contribuciones de
código, pero los reportes y las ideas ayudan muchísimo: ver
[CONTRIBUTING.md](CONTRIBUTING.md).

## Privacidad

SharkCut no recoge datos: no hay cuentas, publicidad ni telemetría. Solo se conecta a
internet para buscar actualizaciones en GitHub y para descargar una vez el modelo de
subtítulos. Ver [PRIVACIDAD.md](PRIVACIDAD.md).

## Licencia

SharkCut es software libre bajo la licencia GPL 3.0 (ver [LICENSE](LICENSE)). Las
licencias de las bibliotecas que usa (FFmpeg, Qt, whisper.cpp, miniaudio y
nlohmann/json) y dónde está su código fuente están en
[licenses/LEEME.md](licenses/LEEME.md).

El nombre «SharkCut», el logotipo y el icono **no** están cubiertos por la GPL. Si
repartes una versión modificada, ponle otro nombre y otro icono. Ver
[MARCA.md](MARCA.md).

## Desarrollo

- Plan del proyecto: [docs/PLAN.md](docs/PLAN.md)
- Preparar el PC para compilarlo: [docs/SETUP-WINDOWS.md](docs/SETUP-WINDOWS.md)
- Instrucciones para las sesiones de desarrollo: [CLAUDE.md](CLAUDE.md)
- Firma del código: [docs/FIRMA.md](docs/FIRMA.md)

---

© 2026 KyoSumiVT
