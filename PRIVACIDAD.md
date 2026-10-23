# Política de privacidad de SharkCut

<!-- El mismo texto que web/privacidad.html: cambiar los dos a la vez. -->

*Última actualización: 23 de octubre de 2026*

**En resumen:** SharkCut no recoge datos tuyos. No tiene cuentas, publicidad,
estadísticas de uso ni telemetría. Tus videos, proyectos y registros se quedan en tu
equipo.

## Lo que SharkCut no hace

- No envía tus videos, audios, imágenes, proyectos ni subtítulos a ningún sitio. La
  edición, la exportación y la transcripción de subtítulos se hacen en tu equipo.
- No recoge estadísticas de uso, informes de fallos ni identificadores de tu equipo.
- No usa cookies, publicidad ni servicios de análisis.

## Cuándo se conecta a internet

SharkCut solo se conecta en estos casos:

1. **Buscar actualizaciones.** Al abrirse, pregunta a GitHub
   (`api.github.com`) cuál es la última versión publicada. Puedes desactivarlo en la
   pestaña Proyectos («Buscar al abrir SharkCut») y buscarlas a mano con «Buscar
   actualizaciones». Si aceptas una actualización, el instalador se descarga de GitHub.
2. **Descargar el modelo de subtítulos.** La primera vez que creas subtítulos
   automáticos, SharkCut descarga el modelo de reconocimiento de voz de Hugging Face
   (`huggingface.co`). Solo se descarga el modelo: tu audio no sale de tu equipo.
3. **Abrir enlaces.** Si pulsas un enlace (GitHub, la página de SharkCut, las
   licencias…), se abre en tu navegador.

En esas conexiones, GitHub y Hugging Face reciben lo que recibe cualquier página web que
visitas: tu dirección IP y datos técnicos de la petición (en la búsqueda de
actualizaciones, también la versión de SharkCut que usas). Lo que hagan con esos datos
se rige por sus propias políticas de privacidad:

- GitHub: <https://docs.github.com/es/site-policy/privacy-policies/github-general-privacy-statement>
- Hugging Face: <https://huggingface.co/privacy>

## Lo que se guarda en tu equipo

- **Registros de cada sesión** (las últimas 8) y, si SharkCut se cierra por un fallo, un
  volcado del fallo (`ultimo-cierre.dmp`), en la carpeta de datos de la app. Sirven para
  investigar errores y **nunca se envían solos**: solo los recibe alguien si tú decides
  mandarlos (por ejemplo, adjuntándolos a un *issue* de GitHub). Pueden incluir las rutas
  de los archivos que abriste.
- **Copias de recuperación** de los proyectos sin guardar, los ajustes de la app y la
  lista de proyectos recientes.
- **Cachés** de cada proyecto (proxies, miniaturas y ondas de audio), junto al archivo
  del proyecto.

Para borrarlo todo, desinstala SharkCut y borra su carpeta de datos y las carpetas
`.sharkp.cache` de tus proyectos.

## La página de descarga

La página de descarga (<https://kyosharkcode.github.io/SharkCut/>) está alojada en GitHub
Pages, consulta la última versión a GitHub y carga sus tipografías de Google Fonts. No
usa cookies ni estadísticas propias.

## Cambios y contacto

Si esta política cambia, se actualizará aquí y en la página de descarga, con su fecha.
Para cualquier duda, abre un *issue* en
<https://github.com/KyoSharkCode/SharkCut/issues>.
