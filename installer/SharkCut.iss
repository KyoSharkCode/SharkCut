; Instalador de SharkCut para Windows (Inno Setup 6).
;
; Lo compila la integración continua (.github/workflows/build.yml) a partir de la carpeta
; de `cmake --install`:
;   iscc /DAppVersion=0.1.0 /DSourceDir=<carpeta instalada> /DOutputDir=<salida> SharkCut.iss
;
; Se instala para el usuario (sin permisos de administrador), así las actualizaciones que
; descarga SharkCut se instalan sin avisos de Windows. Asocia los .sharkp con SharkCut y,
; al terminar (también en una actualización silenciosa), vuelve a abrirlo.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\SharkCut"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
; Identificador fijo: con él, una versión nueva sustituye a la anterior.
AppId={{8C1F3A52-6B7E-4E44-9B1E-5A2C7D3F9E10}
AppName=SharkCut
AppVersion={#AppVersion}
AppVerName=SharkCut {#AppVersion}
AppPublisher=KyoSumiVT
AppPublisherURL=https://kyosharkcode.github.io/SharkCut/
AppSupportURL=https://github.com/KyoSharkCode/SharkCut/issues
AppUpdatesURL=https://kyosharkcode.github.io/SharkCut/
DefaultDirName={autopf}\SharkCut
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename=SharkCut-{#AppVersion}-Setup
SetupIconFile=..\src\app\icons\sharkcut.ico
UninstallDisplayIcon={app}\bin\SharkCut.exe
UninstallDisplayName=SharkCut
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Cierra SharkCut si está abierto (al actualizar) y no lo reabre por su cuenta: lo hace
; la entrada de [Run].
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "escritorio"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\SharkCut"; Filename: "{app}\bin\SharkCut.exe"
Name: "{autodesktop}\SharkCut"; Filename: "{app}\bin\SharkCut.exe"; Tasks: escritorio

[Registry]
; Proyectos .sharkp: se abren con SharkCut (doble clic) y llevan su icono.
Root: HKA; Subkey: "Software\Classes\.sharkp"; ValueType: string; ValueName: ""; ValueData: "SharkCut.Proyecto"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\SharkCut.Proyecto"; ValueType: string; ValueName: ""; ValueData: "Proyecto de SharkCut"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SharkCut.Proyecto\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\SharkCut.exe,0"
Root: HKA; Subkey: "Software\Classes\SharkCut.Proyecto\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\SharkCut.exe"" ""%1"""

[Run]
; Sin skipifsilent: tras una actualización silenciosa también se vuelve a abrir.
Filename: "{app}\bin\SharkCut.exe"; Description: "Abrir SharkCut"; Flags: nowait postinstall
