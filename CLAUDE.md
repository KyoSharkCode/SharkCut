# SharkCut

Editor de video de escritorio para Windows, de uso personal (KyoSumiVT).
El plan completo está en docs/PLAN.md: léelo antes de proponer cambios grandes.
La sección 13 del plan recoge ajustes técnicos que prevalecen sobre las
secciones anteriores cuando difieren.

## Stack
- Electron + JavaScript (módulos ES). Sin frameworks de UI salvo que se acuerde.
- Render: WebGL2 + shaders GLSL. Vista previa y exportación usan el MISMO motor.
- Codificación: ffmpeg nativo en vendor/ffmpeg, invocado desde el proceso principal
  con `spawn` y argumentos en lista.
- Seguridad Electron: contextIsolation activado, nodeIntegration desactivado,
  sandbox activado, toda la comunicación por preload.js + IPC. Medios locales
  servidos con un protocolo propio (`media://`), nunca con webSecurity desactivado.
- Archivo de proyecto: `.sharkp` (JSON con campo `version`).
- Tiempos internos en enteros (microsegundos o fotogramas), no en segundos float.

## Reglas
- Edición no destructiva: nunca modificar los archivos de medios originales.
  Proxies, miniaturas y ondas van a la carpeta de caché del proyecto.
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
