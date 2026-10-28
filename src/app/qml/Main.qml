import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

Window {
    id: ventana

    width: 1600
    height: 940
    minimumWidth: 1280
    minimumHeight: 760
    visible: true
    title: editor.projectName + (editor.modified ? " •" : "")
           + (editor.hasSequence ? " · " + editor.sequenceName : "")
           + " — SharkCut " + AppInfo.appVersion
    color: Theme.fondo

    // Pestaña superior: "proyectos", "editor" o "exportar"
    property string pagina: "editor"
    readonly property bool enProyectos: pagina === "proyectos"
    readonly property bool enEditor: pagina === "editor"
    readonly property bool enExportar: pagina === "exportar"

    function irA(nueva) {
        if (nueva === pagina)
            return
        if (nueva === "proyectos")
            editor.pause()
        pagina = nueva
    }

    // Nombre legible de la API gráfica que usa Qt Quick en esta ventana.
    readonly property string apiGrafica: {
        switch (contenido.GraphicsInfo.api) {
        case GraphicsInfo.Direct3D11: return "Direct3D 11"
        case GraphicsInfo.Direct3D12: return "Direct3D 12"
        case GraphicsInfo.Vulkan: return "Vulkan"
        case GraphicsInfo.OpenGL: return "OpenGL"
        case GraphicsInfo.Metal: return "Metal"
        case GraphicsInfo.Software: return "Software"
        default: return "Desconocida"
        }
    }
    readonly property bool esWindows: Qt.platform.os === "windows"
    readonly property bool apiEsperada: !esWindows
                                        || contenido.GraphicsInfo.api === GraphicsInfo.Direct3D11

    Editor {
        id: editor
    }

    // Una vez por refresco de pantalla: avanza la reproducción.
    FrameAnimation {
        running: true
        onTriggered: editor.advance()
    }

    FileDialog {
        id: dialogoImportar
        title: "Importar medios"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Videos, audio e imágenes (*.mp4 *.mkv *.mov *.m4v *.webm *.avi *.flv *.ts *.mp3 *.wav *.m4a *.aac *.flac *.ogg *.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)",
                      "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)",
                      "Todos los archivos (*)"]
        onAccepted: editor.importFiles(selectedFiles, -1, -1, panelMedios.carpeta)
    }
    FileDialog {
        id: dialogoAbrirProyecto
        title: "Abrir proyecto"
        nameFilters: ["Proyectos de SharkCut (*.sharkp)"]
        onAccepted: {
            editor.openProject(selectedFile)
            ventana.irA("editor")
        }
    }
    FileDialog {
        id: dialogoGuardarProyecto
        title: "Guardar proyecto"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "sharkp"
        nameFilters: ["Proyectos de SharkCut (*.sharkp)"]
        onAccepted: editor.saveAs(selectedFile)
        onRejected: ventana.accionPendiente = ""
    }
    // --- Actualizaciones: al abrir se busca una versión nueva (si está activado) ---
    DialogoActualizacion {
        id: dialogoActualizacion
        onCerrarEInstalar: ventana.close()
    }
    Connections {
        target: Updater
        function onUpdateFound() { dialogoActualizacion.open() }
    }
    Timer {
        // Un poco después de abrir: que la ventana no espere a la red.
        interval: 4000
        running: true
        onTriggered: Updater.check(true)
    }

    // --- Subtítulos: opciones y archivos .srt ---
    DialogoSubtitulos {
        id: dialogoSubtitulos
        editor: editor
        onAceptado: (opciones) => {
            if (modo === "dividir")
                editor.relayoutCaptionTrack(pista, opciones)
            else
                editor.createCaptions(opciones)
        }
    }
    FileDialog {
        id: dialogoImportarSrt
        title: "Importar subtítulos"
        nameFilters: ["Subtítulos (*.srt)", "Todos los archivos (*)"]
        onAccepted: editor.importSrt(selectedFile)
    }
    FileDialog {
        id: dialogoExportarSrt
        property int pista: 0
        title: "Exportar subtítulos"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: ["Subtítulos (*.srt)"]
        onAccepted: editor.exportSrt(pista, selectedFile)
    }

    DialogoNuevoProyecto {
        id: dialogoNuevo
        onCrear: (nombre, ancho, alto, fps) => {
            editor.newProject(nombre, ancho, alto, fps)
            ventana.irA("editor")
        }
    }
    DialogoNuevoProyecto {
        id: dialogoSecuencia
        secuencia: true
        onCrear: (nombre, ancho, alto, fps) => editor.newSequence(nombre, ancho, alto, fps)
    }

    // --- Proyectos: guardar y cambios sin guardar ---

    // Acción que espera a que se guarden (o descarten) los cambios del proyecto activo:
    // "cerrarProyecto" (el de `proyectoPendiente`) o "cerrar" (la ventana).
    property string accionPendiente: ""
    property var proyectoPendiente: 0
    property bool cerrarSinPreguntar: false

    function guardar() {
        if (!editor.save())
            dialogoGuardarProyecto.open()
    }
    function cerrarProyecto(uid) {
        if (!editor.projectModified(uid)) {
            editor.closeProject(uid)
            return
        }
        editor.activateProject(uid)
        proyectoPendiente = uid
        accionPendiente = "cerrarProyecto"
        dialogoCambios.open()
    }
    // Al cerrar la ventana se pregunta por cada proyecto con cambios, uno a uno.
    function revisarCierre() {
        const pendientes = editor.unsavedProjects()
        if (pendientes.length === 0) {
            cerrarSinPreguntar = true
            Qt.callLater(ventana.close)
            return
        }
        editor.activateProject(pendientes[0])
        accionPendiente = "cerrar"
        dialogoCambios.open()
    }
    function ejecutar(accion, descartado) {
        accionPendiente = ""
        if (accion === "cerrarProyecto") {
            editor.closeProject(proyectoPendiente)
        } else if (accion === "cerrar") {
            if (descartado)
                editor.closeProject(editor.activeProject) // para no volver a preguntar
            revisarCierre()
        }
    }

    // ¿Guardar los cambios? (propio, para que los botones estén en español y con el tema)
    Popup {
        id: dialogoCambios
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        closePolicy: Popup.CloseOnEscape
        property bool respondido: false
        onOpened: respondido = false
        onClosed: {
            if (!respondido)
                ventana.accionPendiente = "" // Esc: como cancelar
        }
        function responder(respuesta) {
            respondido = true
            close()
            if (respuesta === "guardar")
                ventana.guardar() // la acción sigue al terminar de guardar
            else if (respuesta === "descartar")
                ventana.ejecutar(ventana.accionPendiente, true)
            else
                ventana.accionPendiente = ""
        }
        background: Rectangle {
            color: Theme.panel
            radius: Theme.radio
            border.color: Theme.borde
        }
        Overlay.modal: Rectangle { color: Theme.velo }

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "¿Guardar los cambios de «" + editor.projectName + "»?"
                color: Theme.texto
                font.pixelSize: 16
                font.bold: true
            }
            Text {
                text: "Si no los guardas, se perderán."
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTexto
            }
            RowLayout {
                Layout.topMargin: 6
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Boton { texto: "Cancelar"; onPulsado: dialogoCambios.responder("cancelar") }
                Boton { texto: "No guardar"; onPulsado: dialogoCambios.responder("descartar") }
                Boton {
                    texto: "Guardar"
                    acentuado: true
                    onPulsado: dialogoCambios.responder("guardar")
                }
            }
        }
    }
    Connections {
        target: editor
        function onSaved(ok) {
            if (ok && ventana.accionPendiente.length > 0)
                ventana.ejecutar(ventana.accionPendiente, false)
            else if (!ok)
                ventana.accionPendiente = ""
        }
    }
    onClosing: (cierre) => {
        if (!cerrarSinPreguntar && editor.unsavedProjects().length > 0) {
            cierre.accepted = false
            revisarCierre()
        }
    }

    // Archivos al abrir (p. ej. "SharkCut.exe a.mp4 b.mp4"): un proyecto .sharkp se abre;
    // los videos se importan y se colocan en la línea de tiempo, uno detrás de otro. Sin
    // archivos se empieza en Proyectos.
    Component.onCompleted: {
        const archivos = AppInfo.initialFiles
        if (archivos.length === 0) {
            pagina = "proyectos"
            return
        }
        if (archivos[0].toLowerCase().endsWith(".sharkp"))
            editor.openProjectPath(archivos[0])
        else
            editor.importPaths(archivos, 0)
    }

    // Mover el cabezal con el teclado o los botones del visor: la línea de tiempo lo
    // mantiene a la vista.
    function navegar(mover) {
        mover()
        lineaTiempo.mostrarCabezal()
    }

    // Atajos de teclado (los habituales de los editores de video)
    readonly property bool conSecuencia: !enProyectos && editor.hasSequence
    Shortcut { sequence: "Space"; enabled: ventana.conSecuencia; onActivated: editor.togglePlay() }
    Shortcut { sequence: "Left"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.step(-1)) }
    Shortcut { sequence: "Right"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.step(1)) }
    Shortcut { sequence: "Shift+Left"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.step(-10)) }
    Shortcut { sequence: "Shift+Right"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.step(10)) }
    // Flechas arriba/abajo: al corte anterior / siguiente.
    Shortcut { sequence: "Up"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.goToEdit(-1)) }
    Shortcut { sequence: "Down"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.goToEdit(1)) }
    Shortcut {
        sequence: "Home"
        enabled: ventana.conSecuencia
        onActivated: ventana.navegar(() => { editor.pause(); editor.seek(0) })
    }
    Shortcut {
        sequence: "End"
        enabled: ventana.conSecuencia
        onActivated: ventana.navegar(() => { editor.pause(); editor.seek(editor.frameCount) })
    }
    Shortcut { sequences: [StandardKey.Open]; onActivated: dialogoAbrirProyecto.open() }
    Shortcut { sequences: [StandardKey.New]; onActivated: dialogoNuevo.open() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: ventana.guardar() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: dialogoGuardarProyecto.open() }
    Shortcut { sequence: "Ctrl+M"; onActivated: ventana.irA("exportar") }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: editor.frameCount > 0 && !editor.exporting
        onActivated: editor.quickExport()
    }
    // J/K/L: atrás, pausa, adelante; otra pulsación dobla la velocidad.
    Shortcut { sequence: "J"; enabled: ventana.conSecuencia; onActivated: editor.shuttle(-1) }
    Shortcut { sequence: "K"; enabled: ventana.conSecuencia; onActivated: editor.pause() }
    Shortcut { sequence: "L"; enabled: ventana.conSecuencia; onActivated: editor.shuttle(1) }
    // Marcadores: M pone uno en el cabezal; Mayús+M va al siguiente y Ctrl+Mayús+M al anterior.
    Shortcut { sequence: "M"; enabled: ventana.conSecuencia; onActivated: editor.addMarkerAtPlayhead() }
    Shortcut { sequence: "Shift+M"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.goToMarker(1)) }
    Shortcut { sequence: "Ctrl+Shift+M"; enabled: ventana.conSecuencia; onActivated: ventana.navegar(() => editor.goToMarker(-1)) }
    Shortcut { sequence: "1"; onActivated: ventana.irA("proyectos") }
    Shortcut { sequence: "2"; onActivated: ventana.irA("editor") }
    Shortcut { sequence: "3"; onActivated: ventana.irA("exportar") }

    // Edición (pestaña Editor)
    Shortcut { sequence: "Ctrl+K"; enabled: ventana.enEditor; onActivated: editor.splitAtPlayhead() }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: ventana.enEditor
        onActivated: editor.removeSelection()
    }
    Shortcut {
        sequences: ["Shift+Delete", "Shift+Backspace"]
        enabled: ventana.enEditor
        onActivated: editor.rippleDeleteSelection()
    }
    // Q / W: recortar con rizo desde el borde anterior hasta el cabezal / desde el cabezal
    // hasta el borde siguiente.
    // Copiar, cortar y pegar clips (en el cabezal).
    Shortcut { sequences: [StandardKey.Copy]; enabled: ventana.enEditor; onActivated: editor.copySelection() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: ventana.enEditor; onActivated: editor.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: ventana.enEditor; onActivated: editor.pasteAtPlayhead() }
    Shortcut { sequence: "Q"; enabled: ventana.enEditor; onActivated: editor.rippleTrimPrevious() }
    Shortcut { sequence: "W"; enabled: ventana.enEditor; onActivated: editor.rippleTrimNext() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: ventana.enEditor; onActivated: editor.undo() }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        enabled: ventana.enEditor
        onActivated: editor.redo()
    }
    Shortcut { sequence: "V"; enabled: ventana.enEditor; onActivated: lineaTiempo.herramienta = "seleccion" }
    Shortcut { sequence: "C"; enabled: ventana.enEditor; onActivated: lineaTiempo.herramienta = "cuchilla" }
    Shortcut { sequence: "S"; enabled: ventana.enEditor; onActivated: lineaTiempo.iman = !lineaTiempo.iman }
    Shortcut { sequences: ["+", "="]; enabled: ventana.enEditor; onActivated: lineaTiempo.acercar() }
    Shortcut { sequence: "-"; enabled: ventana.enEditor; onActivated: lineaTiempo.alejar() }
    Shortcut { sequence: "\\"; enabled: ventana.enEditor; onActivated: lineaTiempo.encajar() }
    Shortcut { sequence: "Ctrl+I"; enabled: ventana.enEditor; onActivated: dialogoImportar.open() }

    ColumnLayout {
        id: contenido
        anchors.fill: parent
        anchors.margins: Theme.margen
        spacing: Theme.margen

        // Barra superior: marca, pestañas de trabajo y proyecto
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                implicitWidth: 26
                implicitHeight: 26
                radius: 7
                color: Theme.marca.abisal
                border.color: Theme.marca.aleta
                Icono {
                    anchors.centerIn: parent
                    nombre: "aleta"
                    tam: 16
                    color: Theme.cabezal
                }
            }
            Text {
                text: "SharkCut"
                color: Theme.texto
                font.pixelSize: Theme.tamTitulo
                font.bold: true
            }

            Item { implicitWidth: 12 }
            Pestana {
                texto: "Proyectos"
                ayuda: "Proyectos (1)"
                activa: ventana.enProyectos
                onPulsada: ventana.irA("proyectos")
            }
            Pestana {
                texto: "Editor"
                ayuda: "Editor (2)"
                activa: ventana.enEditor
                onPulsada: ventana.irA("editor")
            }
            Pestana {
                texto: "Exportar"
                ayuda: "Exportar (3 o Ctrl+M)"
                activa: ventana.enExportar
                onPulsada: ventana.irA("exportar")
            }

            Item { Layout.fillWidth: true }
            // Hay una versión nueva: abre su diálogo.
            BotonIcono {
                visible: Updater.state === "disponible" || Updater.state === "descargando"
                         || Updater.state === "lista"
                acentuado: true
                implicitHeight: 28
                icono: "importar"
                texto: Updater.state === "lista" ? "Instalar " + Updater.latestVersion
                       : "Actualización " + Updater.latestVersion
                ayuda: "Hay una versión nueva de SharkCut"
                onPulsado: dialogoActualizacion.open()
            }
            Text {
                text: editor.projectName
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                elide: Text.ElideMiddle
                Layout.maximumWidth: 280
            }
            Rectangle {
                visible: editor.modified
                implicitWidth: 7
                implicitHeight: 7
                radius: 4
                color: Theme.acento
                Layout.rightMargin: 4
            }
            BotonIcono {
                icono: "nuevo"
                texto: "Nuevo"
                ayuda: "Nuevo proyecto (Ctrl+N)"
                onPulsado: dialogoNuevo.open()
            }
            BotonIcono {
                icono: "abrir"
                texto: "Abrir"
                ayuda: "Abrir proyecto (Ctrl+O)"
                onPulsado: dialogoAbrirProyecto.open()
            }
            BotonIcono {
                icono: "guardar"
                texto: "Guardar"
                ayuda: "Guardar (Ctrl+S) · Guardar como: Ctrl+Mayús+S"
                acentuado: editor.modified
                enabled: !editor.fileBusy
                onPulsado: ventana.guardar()
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ventana.enProyectos ? 0 : ventana.enEditor ? 1 : 2

            // --- Proyectos ---
            PaginaProyectos {
                editor: editor
                apiGrafica: ventana.apiGrafica
                apiEsperada: ventana.apiEsperada
                onNuevoProyecto: dialogoNuevo.open()
                onAbrirProyecto: dialogoAbrirProyecto.open()
                onIrAlEditor: ventana.irA("editor")
            }

            // --- Editor ---
            ColumnLayout {
                spacing: Theme.margen

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.margen

                    // Medios · Transiciones · Efectos
                    PanelMedios {
                        id: panelMedios
                        Layout.preferredWidth: 400
                        Layout.fillHeight: true
                        editor: editor
                        fantasma: fantasma
                        onImportar: dialogoImportar.open()
                        onCrearSubtitulos: dialogoSubtitulos.crear()
                        onImportarSrt: dialogoImportarSrt.open()
                        onDividirSubtitulos: (pista) => dialogoSubtitulos.dividir(pista)
                        onExportarSrt: (pista) => {
                            dialogoExportarSrt.pista = pista
                            dialogoExportarSrt.open()
                        }
                        onNuevaSecuencia: dialogoSecuencia.open()
                        onCerrarProyecto: (uid) => ventana.cerrarProyecto(uid)
                    }

                    // Propiedades de lo seleccionado
                    PanelPropiedades {
                        Layout.preferredWidth: 310
                        Layout.fillHeight: true
                        editor: editor
                    }

                    // Programa: visor + controles
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.panel
                        radius: Theme.radio
                        border.color: zonaSoltar.containsDrag ? Theme.acento : Theme.borde
                        border.width: zonaSoltar.containsDrag ? 2 : 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.margen
                            spacing: Theme.margen

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 4
                                spacing: 8
                                Text {
                                    text: "Programa"
                                    color: Theme.texto
                                    font.pixelSize: Theme.tamTexto
                                    font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                Chip {
                                    visible: editor.useProxies && editor.proxiesAvailable
                                    texto: editor.proxiesTotal > 0
                                           ? "PROXIES " + editor.proxiesReady + "/" + editor.proxiesTotal
                                           : "PROXIES"
                                    colorTexto: Theme.acento
                                }
                                Chip {
                                    texto: "LIENZO " + editor.canvasLabel
                                    colorTexto: Theme.texto
                                }
                                Text {
                                    text: editor.projectDescription
                                    color: Theme.textoSecundario
                                    font.pixelSize: Theme.tamTextoPequeno
                                }
                            }

                            VideoViewer {
                                id: visor
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                source: editor
                                backgroundColor: Theme.visorFondo

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    visible: editor.frameCount === 0

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: editor.hasSequence ? "Secuencia vacía" : "Sin secuencia abierta"
                                        color: Theme.texto
                                        font.pixelSize: Theme.tamTitulo
                                        font.bold: true
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        horizontalAlignment: Text.AlignHCenter
                                        text: editor.hasSequence
                                              ? "Importa medios (Ctrl+I) y arrástralos a la línea de tiempo."
                                              : "Abre una secuencia desde Medios o crea una nueva."
                                        color: Theme.textoSecundario
                                        font.pixelSize: Theme.tamTexto
                                    }
                                }

                                MarcoReencuadre {
                                    anchors.fill: parent
                                    visible: editor.frameCount > 0
                                }
                                MarcoAnclaje {
                                    anchors.fill: parent
                                    visible: editor.anchorPicking
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                enabled: editor.frameCount > 0
                                opacity: enabled ? 1 : 0.4
                                spacing: 6

                                BotonIcono { icono: "inicio"; ayuda: "Al inicio (Inicio)"; onPulsado: ventana.navegar(() => { editor.pause(); editor.seek(0) }) }
                                BotonIcono { icono: "anterior"; ayuda: "Fotograma anterior (←) · Mayús+←: 10 atrás · ↑: corte anterior"; onPulsado: ventana.navegar(() => editor.step(-1)) }
                                BotonIcono {
                                    icono: editor.playing ? "pausa" : "reproducir"
                                    ayuda: "Reproducir / pausa (Espacio) · J / K / L: atrás, pausa, adelante (otra vez: más rápido)"
                                    acentuado: true
                                    implicitWidth: 52
                                    onPulsado: editor.togglePlay()
                                }
                                BotonIcono { icono: "siguiente"; ayuda: "Fotograma siguiente (→) · Mayús+→: 10 adelante · ↓: corte siguiente"; onPulsado: ventana.navegar(() => editor.step(1)) }
                                BotonIcono {
                                    icono: "final"
                                    ayuda: "Al final (Fin)"
                                    onPulsado: ventana.navegar(() => { editor.pause(); editor.seek(editor.frameCount) })
                                }
                                Item { implicitWidth: 8 }
                                BotonIcono {
                                    icono: editor.muted ? "silencio" : "sonido"
                                    ayuda: editor.muted ? "Activar el sonido" : "Silenciar"
                                    onPulsado: editor.muted = !editor.muted
                                }
                                // Sin proyecto guardado no hay dónde crear los proxies: no se
                                // desactiva (así su ayuda explica por qué) y un clic lo guarda.
                                BotonIcono {
                                    icono: "proxy"
                                    texto: editor.useProxies && editor.proxiesAvailable && editor.proxiesTotal > 0
                                           ? "Proxies " + editor.proxiesReady + "/" + editor.proxiesTotal
                                           : "Proxies"
                                    activo: editor.useProxies && editor.proxiesAvailable
                                    opacity: editor.proxiesAvailable ? 1 : 0.5
                                    ayuda: {
                                        if (!editor.proxiesAvailable)
                                            return "Los proxies se guardan junto al archivo del proyecto: "
                                                   + "guárdalo para poder usarlos (clic para guardar)"
                                        if (editor.proxiesTotal === 0)
                                            return "Proxies: copias ligeras de los videos para que la vista "
                                                   + "previa vaya fluida. Aún no hay videos en el proyecto"
                                        const cuenta = editor.proxiesReady + " de " + editor.proxiesTotal
                                                       + (editor.proxiesTotal === 1 ? " video tiene" : " videos tienen")
                                                       + " proxy"
                                        if (editor.useProxies)
                                            return "Usando proxies en la vista previa (" + cuenta + "; "
                                                   + "los que faltan se están creando o usan el original). "
                                                   + "La exportación usa siempre los originales"
                                        return "Usar proxies: vista previa más fluida con VODs largos. "
                                               + cuenta + "; al activarlos se crean los que falten"
                                    }
                                    onPulsado: {
                                        if (!editor.proxiesAvailable)
                                            ventana.guardar()
                                        else
                                            editor.useProxies = !editor.useProxies
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                // Velocidad con J/L (a 1× no se muestra)
                                Chip {
                                    visible: editor.playbackSpeed !== 0 && editor.playbackSpeed !== 1
                                    texto: (editor.playbackSpeed < 0 ? "◀ " : "▶ ")
                                           + Math.abs(editor.playbackSpeed) + "×"
                                    colorTexto: Theme.acento
                                }
                                CodigoTiempo {
                                    actual: editor.timecode
                                    total: editor.durationTimecode
                                }
                            }
                        }

                        DropArea {
                            id: zonaSoltar
                            anchors.fill: parent
                            keys: ["text/uri-list"]
                            onDropped: (drop) => {
                                if (!drop.hasUrls || drop.urls.length === 0)
                                    return
                                editor.importFiles(drop.urls, -1, -1, panelMedios.carpeta)
                                drop.acceptProposedAction()
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false // su altura es la de la línea de tiempo, no se estira
                    Layout.preferredHeight: lineaTiempo.implicitHeight
                    spacing: Theme.margen
                    LineaTiempo {
                        id: lineaTiempo
                        editor: editor
                        onNuevaSecuencia: dialogoSecuencia.open()
                        onExportarSrt: (pista) => {
                            dialogoExportarSrt.pista = pista
                            dialogoExportarSrt.open()
                        }
                        onDividirSubtitulos: (pista) => dialogoSubtitulos.dividir(pista)
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    Medidor {
                        Layout.fillHeight: true
                        izquierdo: editor.meterLeft
                        derecho: editor.meterRight
                    }
                }
            }

            // --- Exportar ---
            PaginaExportar {
                editor: editor
            }
        }

        // Pie: estado, exportación en curso y «Exportar rápido»
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                Layout.fillWidth: true
                readonly property string error: editor.errorString
                text: error.length > 0 ? "Aviso: " + error
                      : editor.fileBusy ? "Leyendo o guardando el proyecto…"
                      : editor.importing ? "Importando…"
                      : editor.busy ? "Cargando…" : "Listo"
                color: error.length > 0 ? Theme.aviso : Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
                elide: Text.ElideRight
            }

            // Proxies en marcha
            Text {
                visible: editor.proxyStatus.length > 0
                text: editor.proxyStatus
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
            BotonIcono {
                visible: editor.proxyStatus.length > 0
                icono: "cerrar"
                ayuda: "Cancelar los proxies pendientes"
                implicitWidth: 28
                implicitHeight: 26
                onPulsado: editor.cancelProxies()
            }

            // Exportación en curso o terminada
            Text {
                visible: editor.exportMessage.length > 0 && !ventana.enExportar
                Layout.maximumWidth: 520
                text: editor.exportMessage
                color: editor.exportMessage.startsWith("No se") ? Theme.aviso : Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
                elide: Text.ElideMiddle
            }
            Rectangle {
                visible: editor.exporting && !ventana.enExportar
                implicitWidth: 160
                implicitHeight: 6
                radius: 3
                color: Theme.panelElevado
                Rectangle {
                    width: parent.width * editor.exportProgress
                    height: parent.height
                    radius: 3
                    color: Theme.acento
                }
            }
            BotonIcono {
                visible: editor.exporting && !ventana.enExportar
                icono: "cerrar"
                texto: "Cancelar"
                onPulsado: editor.cancelExport()
            }
            BotonIcono {
                visible: !ventana.enExportar
                icono: "exportar"
                texto: "Exportar rápido"
                ayuda: "La secuencia entera con los últimos ajustes de Exportar (Ctrl+E)"
                acentuado: true
                implicitHeight: 36
                implicitWidth: 160
                enabled: editor.frameCount > 0 && !editor.exporting
                onPulsado: editor.quickExport()
            }
        }
    }

    // Reencuadre en el visor (pestaña Editor): clic sobre un clip visible para
    // seleccionarlo (el de más arriba), arrastrarlo para moverlo y arrastrar sus esquinas
    // para escalarlo. Se ajusta a los bordes y al centro del lienzo (Mayús: sin ajuste).
    // Mientras se arrastra solo es una vista previa; al soltar se aplica (un paso de
    // deshacer).
    // Punto de anclaje: mientras se marca la zona a seguir, se dibuja un recuadro en el
    // visor (encima del reencuadre, que no recibe el ratón). Se ve el contorno del clip
    // seguido. Al soltar empieza el seguimiento; Esc o clic sin arrastrar, cancelar.
    component MarcoAnclaje: Item {
        id: marcoAncla
        clip: true

        readonly property size lienzo: editor.canvas
        readonly property real escalaVista: Math.min(width / Math.max(1, lienzo.width),
                                                     height / Math.max(1, lienzo.height))
        readonly property real x0: (width - lienzo.width * escalaVista) / 2
        readonly property real y0: (height - lienzo.height * escalaVista) / 2
        readonly property var caja: editor.anchorTargetBox
        property point inicio
        property point fin
        property bool marcando: false

        function aLienzo(px, py) {
            return Qt.point((px - x0) / escalaVista, (py - y0) / escalaVista)
        }

        Shortcut {
            sequence: "Escape"
            enabled: marcoAncla.visible
            onActivated: editor.cancelAnchorPick()
        }

        // Contorno del clip que se sigue.
        Rectangle {
            visible: marcoAncla.caja.width !== undefined
            x: visible ? marcoAncla.x0 + marcoAncla.caja.left * marcoAncla.escalaVista : 0
            y: visible ? marcoAncla.y0 + marcoAncla.caja.top * marcoAncla.escalaVista : 0
            width: visible ? marcoAncla.caja.width * marcoAncla.escalaVista : 0
            height: visible ? marcoAncla.caja.height * marcoAncla.escalaVista : 0
            color: "transparent"
            border.color: Theme.aviso
            border.width: 1
            transform: Rotation {
                origin.x: marcoAncla.caja.width !== undefined
                          ? (marcoAncla.caja.pivotX - marcoAncla.caja.left) * marcoAncla.escalaVista : 0
                origin.y: marcoAncla.caja.width !== undefined
                          ? (marcoAncla.caja.pivotY - marcoAncla.caja.top) * marcoAncla.escalaVista : 0
                angle: marcoAncla.caja.rotation || 0
            }
        }
        // El recuadro que se está dibujando.
        Rectangle {
            visible: marcoAncla.marcando
            x: Math.min(marcoAncla.inicio.x, marcoAncla.fin.x)
            y: Math.min(marcoAncla.inicio.y, marcoAncla.fin.y)
            width: Math.abs(marcoAncla.fin.x - marcoAncla.inicio.x)
            height: Math.abs(marcoAncla.fin.y - marcoAncla.inicio.y)
            color: "transparent"
            border.color: Theme.acento
            border.width: 2
            Rectangle {
                anchors.fill: parent
                color: Theme.acento
                opacity: 0.15
            }
        }
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 10
            width: aviso.implicitWidth + 24
            height: aviso.implicitHeight + 12
            radius: Theme.radio
            color: Theme.panelElevado
            border.color: Theme.acento
            Text {
                id: aviso
                anchors.centerIn: parent
                text: "Dibuja un recuadro sobre lo que quieres seguir · Esc: cancelar"
                color: Theme.texto
                font.pixelSize: Theme.tamTextoPequeno
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            onPressed: (m) => {
                marcoAncla.inicio = Qt.point(m.x, m.y)
                marcoAncla.fin = marcoAncla.inicio
                marcoAncla.marcando = true
            }
            onPositionChanged: (m) => {
                if (marcoAncla.marcando)
                    marcoAncla.fin = Qt.point(m.x, m.y)
            }
            onReleased: {
                marcoAncla.marcando = false
                const a = marcoAncla.aLienzo(Math.min(marcoAncla.inicio.x, marcoAncla.fin.x),
                                             Math.min(marcoAncla.inicio.y, marcoAncla.fin.y))
                const w = Math.abs(marcoAncla.fin.x - marcoAncla.inicio.x) / marcoAncla.escalaVista
                const h = Math.abs(marcoAncla.fin.y - marcoAncla.inicio.y) / marcoAncla.escalaVista
                if (w * marcoAncla.escalaVista < 4 || h * marcoAncla.escalaVista < 4)
                    editor.cancelAnchorPick()
                else
                    editor.startAnchor(a.x, a.y, w, h)
            }
        }
    }

    component MarcoReencuadre: Item {
        id: marco
        clip: true // la caja de un clip que se sale del lienzo no tapa el resto de la ventana

        readonly property var sel: editor.selectedClip
        readonly property size lienzo: editor.canvas
        readonly property real escalaVista: Math.min(width / Math.max(1, lienzo.width),
                                                     height / Math.max(1, lienzo.height))
        readonly property real x0: (width - lienzo.width * escalaVista) / 2
        readonly property real y0: (height - lienzo.height * escalaVista) / 2
        readonly property bool hayCaja: sel.count === 1 && sel.video !== undefined
            && sel.video !== 0 && !sel.videoHidden
            && editor.currentFrame >= sel.start && editor.currentFrame < sel.end
        readonly property var caja: hayCaja ? sel.box : null
        readonly property bool editable: hayCaja && !sel.videoLocked

        // Arrastre en curso: "" | "mover" | "escalar"
        property string modo: ""
        property real guiaX: -1 // guías de ajuste en píxeles del lienzo (-1 = ninguna)
        property real guiaY: -1

        function aLienzo(px, py) {
            return Qt.point((px - x0) / escalaVista, (py - y0) / escalaVista)
        }
        // El punto con el giro del clip deshecho (la caja es la del clip sin girar).
        function sinGiro(p) {
            if (caja === null || !caja.rotation)
                return p
            const a = -caja.rotation * Math.PI / 180
            const dx = p.x - caja.pivotX
            const dy = p.y - caja.pivotY
            return Qt.point(caja.pivotX + dx * Math.cos(a) - dy * Math.sin(a),
                            caja.pivotY + dx * Math.sin(a) + dy * Math.cos(a))
        }
        function contiene(punto) {
            const p = sinGiro(punto)
            return caja !== null && p.x >= caja.left && p.x < caja.left + caja.width
                   && p.y >= caja.top && p.y < caja.top + caja.height
        }
        // Desplazamiento que lleva el borde o centro más cercano a una guía del lienzo.
        function ajuste(inicio, tam, total, libre) {
            if (libre)
                return { d: 0, guia: -1 }
            const umbral = 8 / escalaVista
            let mejor = { d: 0, guia: -1 }
            let distancia = umbral
            for (const borde of [inicio, inicio + tam / 2, inicio + tam]) {
                for (const guia of [0, total / 2, total]) {
                    const d = guia - borde
                    if (Math.abs(d) < distancia) {
                        distancia = Math.abs(d)
                        mejor = { d: d, guia: guia }
                    }
                }
            }
            return mejor
        }

        MouseArea {
            id: zonaMarco
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: marco.modo === "mover" ? Qt.ClosedHandCursor
                         : marco.editable && marco.contiene(marco.aLienzo(mouseX, mouseY))
                           ? Qt.OpenHandCursor : Qt.ArrowCursor
            property point inicio
            property var transformInicial: null
            property var cajaInicial: null
            property bool movido: false

            onPressed: (m) => {
                const p = marco.aLienzo(m.x, m.y)
                // Manda el clip de más arriba bajo el ratón (la facecam sobre el gameplay),
                // aunque haya otro seleccionado debajo.
                const id = editor.clipAtCanvas(p.x, p.y)
                if (id !== 0 && id !== marco.sel.video)
                    editor.select(id, false)
                else if (id === 0 && !marco.contiene(p))
                    editor.clearSelection()
                if (!marco.editable || !marco.contiene(p))
                    return
                inicio = p
                transformInicial = marco.sel.transform
                cajaInicial = marco.caja
                movido = false
                marco.modo = "mover"
            }
            onPositionChanged: (m) => {
                if (marco.modo !== "mover" || !pressed)
                    return
                const p = marco.aLienzo(m.x, m.y)
                let dx = p.x - inicio.x
                let dy = p.y - inicio.y
                if (!movido && Math.hypot(dx, dy) * marco.escalaVista < 3)
                    return
                movido = true
                const libre = (m.modifiers & Qt.ShiftModifier) !== 0
                const ax = marco.ajuste(cajaInicial.left + dx, cajaInicial.width, marco.lienzo.width, libre)
                const ay = marco.ajuste(cajaInicial.top + dy, cajaInicial.height, marco.lienzo.height, libre)
                dx += ax.d
                dy += ay.d
                marco.guiaX = ax.guia
                marco.guiaY = ay.guia
                editor.previewTransform(marco.sel.video, { x: transformInicial.x + dx,
                                                           y: transformInicial.y + dy })
            }
            onReleased: {
                if (marco.modo === "mover") {
                    if (movido)
                        editor.setClipTransform(marco.sel.video, {}, "Mover en el visor")
                    else
                        editor.endPreview()
                }
                marco.modo = ""
                marco.guiaX = -1
                marco.guiaY = -1
            }
            onCanceled: {
                editor.endPreview()
                marco.modo = ""
                marco.guiaX = -1
                marco.guiaY = -1
            }
        }

        // Guías de ajuste
        Rectangle {
            visible: marco.guiaX >= 0
            x: marco.x0 + marco.guiaX * marco.escalaVista
            y: marco.y0
            width: 1
            height: marco.lienzo.height * marco.escalaVista
            color: Theme.acento
        }
        Rectangle {
            visible: marco.guiaY >= 0
            x: marco.x0
            y: marco.y0 + marco.guiaY * marco.escalaVista
            width: marco.lienzo.width * marco.escalaVista
            height: 1
            color: Theme.acento
        }

        // Caja del clip seleccionado y sus esquinas
        Rectangle {
            id: cajaVista
            visible: marco.hayCaja
            x: marco.caja ? marco.x0 + marco.caja.left * marco.escalaVista : 0
            y: marco.caja ? marco.y0 + marco.caja.top * marco.escalaVista : 0
            width: marco.caja ? marco.caja.width * marco.escalaVista : 0
            height: marco.caja ? marco.caja.height * marco.escalaVista : 0
            color: "transparent"
            border.color: marco.editable ? Theme.acento : Theme.textoDesactivado
            border.width: 1
            // Girada como el clip, alrededor del centro del video.
            transform: Rotation {
                origin.x: marco.caja ? (marco.caja.pivotX - marco.caja.left) * marco.escalaVista : 0
                origin.y: marco.caja ? (marco.caja.pivotY - marco.caja.top) * marco.escalaVista : 0
                angle: marco.caja ? marco.caja.rotation : 0
            }

            // Tirador de giro: arriba, en el centro. Mayús: de 15° en 15°.
            Rectangle {
                visible: marco.editable
                x: cajaVista.width / 2 - 1
                y: -22
                width: 2
                height: 16
                color: Theme.acento
            }
            Rectangle {
                id: tiradorGiro
                visible: marco.editable
                x: cajaVista.width / 2 - width / 2
                y: -30
                width: 12
                height: 12
                radius: 6
                color: Theme.visorFondo
                border.color: Theme.acento
                border.width: 2
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    preventStealing: true
                    cursorShape: Qt.CrossCursor
                    property real anguloInicial: 0
                    property real giroInicial: 0
                    property bool movido: false
                    function anguloEn(m) {
                        const q = mapToItem(marco, m.x, m.y)
                        const p = marco.aLienzo(q.x, q.y)
                        return Math.atan2(p.y - marco.caja.pivotY, p.x - marco.caja.pivotX) * 180 / Math.PI
                    }
                    onPressed: (m) => {
                        anguloInicial = anguloEn(m)
                        giroInicial = marco.sel.transform.rotation || 0
                        movido = false
                        marco.modo = "girar"
                    }
                    onPositionChanged: (m) => {
                        if (!pressed)
                            return
                        let d = anguloEn(m) - anguloInicial
                        if (d > 180) d -= 360
                        if (d < -180) d += 360
                        let giro = giroInicial + d
                        if (m.modifiers & Qt.ShiftModifier)
                            giro = Math.round(giro / 15) * 15
                        movido = true
                        editor.previewTransform(marco.sel.video, { rotation: giro })
                    }
                    onReleased: {
                        if (movido)
                            editor.setClipTransform(marco.sel.video, {}, "Girar en el visor")
                        else
                            editor.endPreview()
                        marco.modo = ""
                    }
                    onCanceled: {
                        editor.endPreview()
                        marco.modo = ""
                    }
                }
            }

            Repeater {
                model: marco.editable ? [[0, 0], [1, 0], [0, 1], [1, 1]] : []
                delegate: Rectangle {
                    required property var modelData
                    x: modelData[0] * cajaVista.width - width / 2
                    y: modelData[1] * cajaVista.height - height / 2
                    width: 10
                    height: 10
                    radius: 2
                    color: Theme.visorFondo
                    border.color: Theme.acento
                    border.width: 2

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        preventStealing: true
                        cursorShape: (parent.modelData[0] === parent.modelData[1])
                                     ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        property point centro
                        property real distanciaInicial: 1
                        property real escalaInicial: 1
                        property bool movido: false
                        onPressed: (m) => {
                            const t = marco.sel.transform
                            centro = Qt.point(marco.lienzo.width / 2 + t.x, marco.lienzo.height / 2 + t.y)
                            const p = marco.aLienzo(mapToItem(marco, m.x, m.y).x, mapToItem(marco, m.x, m.y).y)
                            distanciaInicial = Math.max(1, Math.hypot(p.x - centro.x, p.y - centro.y))
                            escalaInicial = t.scale
                            movido = false
                            marco.modo = "escalar"
                        }
                        onPositionChanged: (m) => {
                            if (!pressed)
                                return
                            const q = mapToItem(marco, m.x, m.y)
                            const p = marco.aLienzo(q.x, q.y)
                            const d = Math.hypot(p.x - centro.x, p.y - centro.y)
                            movido = true
                            editor.previewTransform(marco.sel.video,
                                                    { scale: escalaInicial * d / distanciaInicial })
                        }
                        onReleased: {
                            if (movido)
                                editor.setClipTransform(marco.sel.video, {}, "Escalar en el visor")
                            else
                                editor.endPreview()
                            marco.modo = ""
                        }
                        onCanceled: {
                            editor.endPreview()
                            marco.modo = ""
                        }
                    }
                }
            }
        }
    }

    component CodigoTiempo: Row {
        property string actual
        property string total
        spacing: 6
        Text {
            text: parent.actual
            color: Theme.texto
            font.pixelSize: Theme.tamTexto
            font.family: "Consolas, monospace"
        }
        Text {
            text: "/ " + parent.total
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTexto
            font.family: "Consolas, monospace"
        }
    }

    // Lo que se ve al arrastrar un medio de la biblioteca (o una transición) a la línea
    // de tiempo.
    Rectangle {
        id: fantasma
        property var mediaId: undefined
        property int transicion: -1 // índice del catálogo si se arrastra una transición
        property string efecto      // nombre del efecto si se arrastra uno ("croma")
        property string sonido      // id del efecto de sonido si se arrastra uno
        property string ajusteTexto // estilo guardado de un texto que se arrastra ("": el de siempre)
        property string nombre
        visible: Drag.active
        z: 100
        width: Math.min(220, textoFantasma.implicitWidth + 20)
        height: 28
        radius: Theme.radio
        color: Theme.panelElevado
        border.color: Drag.keys.indexOf("sharkcut/texto") >= 0 ? Theme.clipTexto
                      : transicion >= 0 || efecto !== "" ? Theme.clipTransicion : Theme.acento
        border.width: 2
        opacity: 0.9
        Drag.keys: ["sharkcut/medio"]
        Drag.hotSpot.x: 10
        Drag.hotSpot.y: 14
        Text {
            id: textoFantasma
            anchors.verticalCenter: parent.verticalCenter
            x: 10
            width: parent.width - 20
            text: fantasma.nombre
            color: Theme.texto
            font.pixelSize: Theme.tamTextoPequeno
            elide: Text.ElideRight
        }
    }
}
