# Firma del código (SignPath)

La política pública está en la página de descarga (`web/firma.html`). Aquí se explica cómo
está preparada la firma en la integración continua y qué falta para activarla.

## Cómo funciona

`.github/workflows/build.yml` firma, **solo al publicar una versión** (etiqueta `vX.Y.Z`) y
**solo si SignPath está configurado** (ver abajo):

1. Sube la carpeta instalada (`SharkCut-windows`) y pide la firma de `bin/SharkCut.exe`
   (configuración de artefacto `ejecutable`). Espera a que se apruebe y sustituye la
   carpeta por la firmada.
2. Con la carpeta firmada crea el instalador y el `.zip`.
3. Sube el instalador (`SharkCut-instalador`) y pide su firma (configuración
   `instalador`). Lo sustituye por el firmado.
4. Publica la versión con el instalador y el `.zip` firmados.

Mientras no estén configuradas las variables, esos pasos se saltan y las versiones salen
sin firmar, como hasta ahora.

## Activarla, cuando SignPath Foundation apruebe el proyecto

1. En SignPath, crear el proyecto enlazado al repositorio de GitHub (GitHub como sistema
   de compilación de confianza) con dos configuraciones de artefacto:

   `ejecutable` (la carpeta instalada; el ejecutable está en `bin/`):

   ```xml
   <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
     <zip-file>
       <pe-file path="bin/SharkCut.exe">
         <authenticode-sign/>
       </pe-file>
     </zip-file>
   </artifact-configuration>
   ```

   `instalador`:

   ```xml
   <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
     <zip-file>
       <pe-file-set>
         <include path="SharkCut-*-Setup.exe"/>
         <for-each>
           <authenticode-sign/>
         </for-each>
       </pe-file-set>
     </zip-file>
   </artifact-configuration>
   ```

2. En GitHub, *Settings → Secrets and variables → Actions*:
   - **Variables:** `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG` y
     `SIGNPATH_POLICY_SLUG` (la política de firma, p. ej. `release-signing`).
   - **Secreto:** `SIGNPATH_API_TOKEN` (el token de un usuario de CI de SignPath con
     permiso para enviar peticiones en ese proyecto).
3. En `web/firma.html`, cambiar el párrafo «Pendiente» por la atribución que pide SignPath
   Foundation («Firma de código gratuita de SignPath.io, certificado de SignPath
   Foundation»), en español y en inglés. En `web/index.html` y en el README, quitar el
   aviso de «Windows protegió tu PC» (o suavizarlo: la reputación del certificado tarda un
   poco en ganarse).

## Límites conocidos

- El desinstalador que crea Inno Setup (`unins000.exe`) no se firma: para firmarlo habría
  que integrar la firma en el propio Inno Setup (`SignTool`).
- Las DLL de Qt y de FFmpeg no se firman con el certificado de SharkCut: son de otros
  autores (ver la política de firma).
