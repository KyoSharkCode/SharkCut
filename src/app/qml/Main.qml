import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

Window {
    id: ventana

    width: 1600
    height: 940
    minimumWidth: 1180
    minimumHeight: 760
    visible: true
    title: (modo === "origen" && reproductor.fileName.length > 0
            ? reproductor.fileName
            : editor.projectName + (editor.modified ? " •" : ""))
           + " — SharkCut " + AppInfo.appVersion
    color: Theme.fondo

    // "editor": línea de tiempo · "origen": ver un video y marcar tramos ·
    // "transicion": probar la transición A → B
    property string modo: "editor"
    readonly property bool enEditor: modo === "editor"
    readonly property bool enOrigen: modo === "origen"
    readonly property bool enTransicion: modo === "transicion"

    function cambiarModo(nuevo) {
        if (nuevo === modo)
            return
        editor.pause()
        reproductor.pause()
        laboratorio.pause()
        modo = nuevo
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
    Player {
        id: reproductor
    }
    TransitionLab {
        id: laboratorio
    }

    // Una vez por refresco de pantalla: avanza la reproducción y mide los fps.
    FrameAnimation {
        id: reloj
        running: true
        onTriggered: {
            editor.advance()
            reproductor.advance()
            laboratorio.advance()
        }
    }
    readonly property real fpsVisor: reloj.smoothFrameTime > 0 ? 1 / reloj.smoothFrameTime : 0

    FileDialog {
        id: dialogoAbrir
        title: "Abrir video"
        nameFilters: ["Videos (*.mp4 *.mkv *.mov *.m4v *.webm *.avi *.flv *.ts)",
                      "Todos los archivos (*)"]
        onAccepted: {
            ventana.cambiarModo("origen")
            reproductor.open(selectedFile)
        }
    }

    FileDialog {
        id: dialogoExportar
        title: "Exportar prueba de transición"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: ["Video MP4 (*.mp4)"]
        onAccepted: laboratorio.exportTo(selectedFile)
    }

    FileDialog {
        id: dialogoImportar
        title: "Importar medios"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Videos y audio (*.mp4 *.mkv *.mov *.m4v *.webm *.avi *.flv *.ts *.mp3 *.wav *.m4a *.aac *.flac *.ogg)",
                      "Todos los archivos (*)"]
        onAccepted: editor.importFiles(selectedFiles)
    }

    FileDialog {
        id: dialogoAbrirProyecto
        title: "Abrir proyecto"
        nameFilters: ["Proyectos de SharkCut (*.sharkp)"]
        onAccepted: editor.openProject(selectedFile)
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
    DialogoExportar {
        id: dialogoExportarLinea
        editor: editor
    }
    DialogoNuevoProyecto {
        id: dialogoNuevo
        onCrear: (nombre, ancho, alto, fps) => editor.newProject(nombre, ancho, alto, fps)
    }

    // --- Proyecto: guardar, abrir y cambios sin guardar ---

    // Acción que espera a que se guarden (o descarten) los cambios: "nuevo", "abrir" o "cerrar".
    property string accionPendiente: ""
    property bool cerrarSinPreguntar: false

    function guardar() {
        if (!editor.save())
            dialogoGuardarProyecto.open()
    }
    function siGuardado(accion) {
        if (!editor.modified) {
            ejecutar(accion)
            return
        }
        accionPendiente = accion
        dialogoCambios.open()
    }
    function ejecutar(accion) {
        accionPendiente = ""
        if (accion === "nuevo") {
            dialogoNuevo.open()
        } else if (accion === "abrir") {
            dialogoAbrirProyecto.open()
        } else if (accion === "cerrar") {
            cerrarSinPreguntar = true
            Qt.callLater(ventana.close)
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
                ventana.ejecutar(ventana.accionPendiente)
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
                ventana.ejecutar(ventana.accionPendiente)
            else if (!ok)
                ventana.accionPendiente = ""
        }
    }
    onClosing: (cierre) => {
        if (editor.modified && !cerrarSinPreguntar) {
            cierre.accepted = false
            siGuardado("cerrar")
        }
    }

    // Archivos al abrir (p. ej. "SharkCut.exe a.mp4 b.mp4"): un proyecto .sharkp se abre;
    // los videos se importan y se colocan en la línea de tiempo, uno detrás de otro.
    Component.onCompleted: {
        const archivos = AppInfo.initialFiles
        if (archivos.length === 0)
            return
        if (archivos[0].toLowerCase().endsWith(".sharkp"))
            editor.openProjectPath(archivos[0])
        else
            editor.importPaths(archivos, 0)
    }

    // Atajos de teclado (estilo Premiere). Actúan sobre la pestaña visible.
    readonly property var activo: enEditor ? editor : enOrigen ? reproductor : laboratorio
    Shortcut { sequence: "Space"; onActivated: ventana.activo.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: ventana.activo.step(-1) }
    Shortcut { sequence: "Right"; onActivated: ventana.activo.step(1) }
    Shortcut { sequence: "Shift+Left"; onActivated: ventana.activo.step(-10) }
    Shortcut { sequence: "Shift+Right"; onActivated: ventana.activo.step(10) }
    Shortcut {
        sequence: "Home"
        onActivated: { ventana.activo.pause(); ventana.activo.seek(0) }
    }
    Shortcut {
        sequence: "End"
        onActivated: { ventana.activo.pause(); ventana.activo.seek(ventana.activo.frameCount - 1) }
    }
    Shortcut { sequence: "M"; onActivated: ventana.activo.muted = !ventana.activo.muted }
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: ventana.enEditor ? ventana.siGuardado("abrir") : dialogoAbrir.open()
    }
    Shortcut { sequences: [StandardKey.New]; onActivated: ventana.siGuardado("nuevo") }
    Shortcut { sequences: [StandardKey.Save]; onActivated: ventana.guardar() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: dialogoGuardarProyecto.open() }
    Shortcut {
        sequence: "Ctrl+M"
        enabled: ventana.enEditor && editor.frameCount > 0 && !editor.exporting
        onActivated: dialogoExportarLinea.open()
    }
    Shortcut { sequence: "I"; enabled: ventana.enOrigen; onActivated: reproductor.setMarkIn() }
    Shortcut { sequence: "O"; enabled: ventana.enOrigen; onActivated: reproductor.setMarkOut() }
    Shortcut { sequence: "T"; enabled: ventana.enTransicion; onActivated: laboratorio.playTransition() }
    Shortcut { sequence: "1"; onActivated: ventana.cambiarModo("editor") }
    Shortcut { sequence: "2"; onActivated: ventana.cambiarModo("origen") }
    Shortcut { sequence: "3"; onActivated: ventana.cambiarModo("transicion") }

    // Edición (pestaña Editor)
    Shortcut { sequence: "Ctrl+K"; enabled: ventana.enEditor; onActivated: editor.splitAtPlayhead() }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: ventana.enEditor
        onActivated: editor.removeSelection()
    }
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
                texto: "1 · Editor"
                activa: ventana.enEditor
                onPulsada: ventana.cambiarModo("editor")
            }
            Pestana {
                texto: "2 · Origen"
                activa: ventana.enOrigen
                onPulsada: ventana.cambiarModo("origen")
            }
            Pestana {
                texto: "3 · Transición"
                activa: ventana.enTransicion
                onPulsada: ventana.cambiarModo("transicion")
            }

            Item { Layout.fillWidth: true }
            Text {
                visible: ventana.enEditor
                text: editor.projectName
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                elide: Text.ElideMiddle
                Layout.maximumWidth: 280
            }
            Rectangle {
                visible: ventana.enEditor && editor.modified
                implicitWidth: 7
                implicitHeight: 7
                radius: 4
                color: Theme.acento
                Layout.rightMargin: 4
            }
            BotonIcono {
                visible: ventana.enEditor
                icono: "nuevo"
                texto: "Nuevo"
                ayuda: "Nuevo proyecto (Ctrl+N)"
                onPulsado: ventana.siGuardado("nuevo")
            }
            BotonIcono {
                visible: ventana.enEditor
                icono: "abrir"
                texto: "Abrir"
                ayuda: "Abrir proyecto (Ctrl+O)"
                onPulsado: ventana.siGuardado("abrir")
            }
            BotonIcono {
                visible: ventana.enEditor
                icono: "guardar"
                texto: "Guardar"
                ayuda: "Guardar (Ctrl+S) · Guardar como: Ctrl+Mayús+S"
                acentuado: editor.modified
                enabled: !editor.fileBusy
                onPulsado: ventana.guardar()
            }
            BotonIcono {
                visible: !ventana.enEditor
                icono: "abrir"
                texto: "Abrir video…"
                ayuda: "Abrir un video en Origen (Ctrl+O)"
                onPulsado: dialogoAbrir.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.margen

            // Editor: medios, transiciones, efectos y propiedades a la izquierda
            PanelMedios {
                visible: ventana.enEditor
                Layout.preferredWidth: 430
                Layout.fillHeight: true
                editor: editor
                fantasma: fantasma
                transiciones: laboratorio.transitionNames
                onImportar: dialogoImportar.open()
                onAbrirEnOrigen: (ruta) => {
                    ventana.cambiarModo("origen")
                    reproductor.openPath(ruta)
                }
            }

            // Visor + controles
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
                        visible: ventana.enEditor
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
                        source: ventana.activo
                        backgroundColor: Theme.visorFondo

                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            visible: ventana.enEditor ? editor.frameCount === 0
                                     : ventana.enOrigen ? !reproductor.loaded : !laboratorio.ready

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: ventana.enEditor ? "Línea de tiempo vacía"
                                      : ventana.enOrigen
                                      ? (reproductor.busy ? "Abriendo…" : "Origen")
                                      : "Transición"
                                color: Theme.texto
                                font.pixelSize: Theme.tamTitulo
                                font.bold: true
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: !reproductor.busy || !ventana.enOrigen
                                horizontalAlignment: Text.AlignHCenter
                                text: ventana.enEditor
                                      ? "Importa medios (Ctrl+I) y arrástralos a la línea de tiempo."
                                      : ventana.enOrigen
                                      ? "Arrastra un video aquí o pulsa «Abrir video…» (Ctrl+O)."
                                      : "En la pestaña Origen, marca un tramo y envíalo como clip A\n"
                                        + "y otro como clip B. Después vuelve aquí."
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTexto
                            }
                        }

                        MarcoReencuadre {
                            anchors.fill: parent
                            visible: ventana.enEditor && editor.frameCount > 0
                        }
                    }

                    // Controles de la pestaña Editor
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ventana.enEditor
                        enabled: editor.frameCount > 0
                        opacity: enabled ? 1 : 0.4
                        spacing: 6

                        BotonIcono { icono: "inicio"; ayuda: "Al inicio (Inicio)"; onPulsado: { editor.pause(); editor.seek(0) } }
                        BotonIcono { icono: "anterior"; ayuda: "Fotograma anterior (←)"; onPulsado: editor.step(-1) }
                        BotonIcono {
                            icono: editor.playing ? "pausa" : "reproducir"
                            ayuda: "Reproducir / pausa (Espacio)"
                            acentuado: true
                            implicitWidth: 52
                            onPulsado: editor.togglePlay()
                        }
                        BotonIcono { icono: "siguiente"; ayuda: "Fotograma siguiente (→)"; onPulsado: editor.step(1) }
                        BotonIcono {
                            icono: "final"
                            ayuda: "Al final (Fin)"
                            onPulsado: { editor.pause(); editor.seek(editor.frameCount - 1) }
                        }
                        Item { implicitWidth: 8 }
                        BotonIcono {
                            icono: editor.muted ? "silencio" : "sonido"
                            ayuda: editor.muted ? "Activar el sonido (M)" : "Silenciar (M)"
                            onPulsado: editor.muted = !editor.muted
                        }
                        Item { Layout.fillWidth: true }
                        CodigoTiempo {
                            actual: editor.timecode
                            total: editor.durationTimecode
                        }
                    }

                    // Controles de la pestaña Origen
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: ventana.enOrigen
                        enabled: reproductor.loaded
                        opacity: enabled ? 1 : 0.4
                        spacing: 6

                        BarraTiempo {
                            Layout.fillWidth: true
                            actual: reproductor.currentFrame
                            total: reproductor.frameCount
                            entrada: reproductor.markIn
                            salida: reproductor.markOut
                            onBuscar: (f) => reproductor.seek(f)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            BotonIcono { icono: "inicio"; ayuda: "Al inicio (Inicio)"; onPulsado: { reproductor.pause(); reproductor.seek(0) } }
                            BotonIcono { icono: "anterior"; ayuda: "Fotograma anterior (←)"; onPulsado: reproductor.step(-1) }
                            BotonIcono {
                            icono: reproductor.playing ? "pausa" : "reproducir"
                            ayuda: "Reproducir / pausa (Espacio)"
                            acentuado: true
                            implicitWidth: 52
                            onPulsado: reproductor.togglePlay()
                        }
                            BotonIcono { icono: "siguiente"; ayuda: "Fotograma siguiente (→)"; onPulsado: reproductor.step(1) }
                            BotonIcono {
                            icono: "final"
                            ayuda: "Al final (Fin)"
                            onPulsado: { reproductor.pause(); reproductor.seek(reproductor.frameCount - 1) }
                        }
                            Item { implicitWidth: 8 }
                            Boton { texto: "Entrada (I)"; onPulsado: reproductor.setMarkIn() }
                            Boton { texto: "Salida (O)"; onPulsado: reproductor.setMarkOut() }
                            Boton {
                                texto: "Borrar marcas"
                                enabled: reproductor.markIn >= 0 || reproductor.markOut >= 0
                                onPulsado: reproductor.clearMarks()
                            }
                            Boton {
                                texto: reproductor.muted ? "Sin sonido" : "Sonido"
                                visible: reproductor.hasAudio
                                onPulsado: reproductor.muted = !reproductor.muted
                            }
                            Item { Layout.fillWidth: true }
                            CodigoTiempo {
                                actual: reproductor.timecode
                                total: reproductor.durationTimecode
                            }
                        }
                    }

                    // Controles de la pestaña Transición
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: ventana.enTransicion
                        enabled: laboratorio.ready
                        opacity: enabled ? 1 : 0.4
                        spacing: 6

                        BarraTiempo {
                            Layout.fillWidth: true
                            segmentos: true
                            actual: laboratorio.currentFrame
                            total: laboratorio.frameCount
                            inicioTransicion: laboratorio.transitionStart
                            finTransicion: laboratorio.transitionEnd
                            onBuscar: (f) => laboratorio.seek(f)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            BotonIcono { icono: "inicio"; ayuda: "Al inicio (Inicio)"; onPulsado: { laboratorio.pause(); laboratorio.seek(0) } }
                            BotonIcono { icono: "anterior"; ayuda: "Fotograma anterior (←)"; onPulsado: laboratorio.step(-1) }
                            BotonIcono {
                            icono: laboratorio.playing ? "pausa" : "reproducir"
                            ayuda: "Reproducir / pausa (Espacio)"
                            acentuado: true
                            implicitWidth: 52
                            onPulsado: laboratorio.togglePlay()
                        }
                            BotonIcono { icono: "siguiente"; ayuda: "Fotograma siguiente (→)"; onPulsado: laboratorio.step(1) }
                            Item { implicitWidth: 8 }
                            Boton {
                                texto: "Ver transición (T)"
                                acentuado: true
                                onPulsado: laboratorio.playTransition()
                            }
                            Boton {
                                texto: laboratorio.muted ? "Sin sonido" : "Sonido"
                                onPulsado: laboratorio.muted = !laboratorio.muted
                            }
                            Item { Layout.fillWidth: true }
                            CodigoTiempo {
                                actual: laboratorio.timecode
                                total: laboratorio.durationTimecode
                            }
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
                        if (ventana.enEditor) {
                            editor.importFiles(drop.urls)
                        } else {
                            ventana.cambiarModo("origen")
                            reproductor.open(drop.urls[0])
                        }
                        drop.acceptProposedAction()
                    }
                }
            }

            // Panel lateral (Origen y Transición)
            Rectangle {
                visible: !ventana.enEditor
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 16
                    visible: !ventana.enEditor
                    contentHeight: ventana.enOrigen ? panelOrigen.implicitHeight
                                                    : panelTransicion.implicitHeight
                    clip: true

                    // --- Pestaña Origen: clip, marcas y diagnóstico ---
                    ColumnLayout {
                        id: panelOrigen
                        visible: ventana.enOrigen
                        width: parent.width
                        spacing: 12

                        Titulo { texto: "Tramo marcado" }
                        FilaDato {
                            etiqueta: "Entrada → salida"
                            valor: reproductor.markInTimecode + "  →  " + reproductor.markOutTimecode
                        }
                        FilaDato {
                            etiqueta: "Duración del tramo"
                            valor: reproductor.loaded ? reproductor.markedDurationTimecode : "—"
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            text: "Coloca el cabezal y pulsa I para marcar la entrada y O para la "
                                  + "salida. Sin marcas se usa el clip entero."
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Enviar el tramo a la línea de tiempo:"
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Boton {
                                Layout.fillWidth: true
                                texto: "Al final"
                                acentuado: true
                                enabled: reproductor.loaded
                                onPulsado: editor.addFromSource(reproductor, false)
                            }
                            Boton {
                                Layout.fillWidth: true
                                texto: "En el cabezal"
                                enabled: reproductor.loaded
                                onPulsado: editor.addFromSource(reproductor, true)
                            }
                        }
                        Separador {}
                        Titulo { texto: "Prueba de transición" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Boton {
                                Layout.fillWidth: true
                                texto: "Usar como clip A"
                                acentuado: true
                                enabled: reproductor.loaded
                                onPulsado: laboratorio.setClipA(reproductor)
                            }
                            Boton {
                                Layout.fillWidth: true
                                texto: "Usar como clip B"
                                acentuado: true
                                enabled: reproductor.loaded
                                onPulsado: laboratorio.setClipB(reproductor)
                            }
                        }
                        ResumenClips {}
                        Boton {
                            Layout.fillWidth: true
                            visible: laboratorio.ready
                            texto: "Ir a la transición →"
                            onPulsado: ventana.cambiarModo("transicion")
                        }

                        Separador {}
                        Titulo { texto: "Clip" }
                        FilaDato {
                            etiqueta: "Archivo"
                            valor: reproductor.fileName.length > 0 ? reproductor.fileName : "—"
                        }
                        FilaDato {
                            etiqueta: "Video"
                            valor: reproductor.loaded ? reproductor.videoDescription : "—"
                        }
                        FilaDato {
                            etiqueta: "Fotogramas por segundo"
                            valor: !reproductor.loaded ? "—"
                                   : reproductor.frameRateText
                                     + (reproductor.variableFrameRate ? " (variables)" : "")
                            colorValor: reproductor.variableFrameRate ? Theme.aviso : Theme.texto
                        }
                        FilaDato {
                            etiqueta: "Duración"
                            valor: reproductor.loaded
                                   ? reproductor.durationTimecode + " (" + reproductor.frameCount
                                     + " fotogramas)"
                                   : "—"
                        }
                        FilaDato { etiqueta: "Audio"; valor: reproductor.audioDescription }
                        FilaDato {
                            etiqueta: "Decodificación"
                            valor: reproductor.decoderText
                            colorValor: !reproductor.loaded ? Theme.texto
                                        : reproductor.hardwareDecoding ? Theme.ok
                                        : ventana.esWindows ? Theme.aviso : Theme.texto
                        }

                        Separador {}
                        Titulo { texto: "Sistema" }
                        FilaDato {
                            etiqueta: "API gráfica"
                            valor: ventana.apiGrafica
                            colorValor: ventana.apiEsperada ? Theme.ok : Theme.aviso
                        }
                        FilaDato {
                            etiqueta: "Fps del visor"
                            valor: Math.round(ventana.fpsVisor).toString()
                        }
                        FilaDato {
                            etiqueta: "Salida de audio"
                            valor: reproductor.audioOutputText
                            colorValor: reproductor.audioOutputAvailable ? Theme.texto : Theme.aviso
                        }
                        FilaDato { etiqueta: "Qt"; valor: AppInfo.qtVersion }
                        FilaDato { etiqueta: "Sistema"; valor: AppInfo.operatingSystem }
                        FilaDato {
                            etiqueta: "SharkCut"
                            valor: AppInfo.appVersion + " (" + AppInfo.buildType + ")"
                        }
                    }

                    // --- Pestaña Transición: clips y parámetros ---
                    ColumnLayout {
                        id: panelTransicion
                        visible: ventana.enTransicion
                        width: parent.width
                        spacing: 14

                        ResumenClips {}
                        Boton {
                            Layout.fillWidth: true
                            texto: "Intercambiar A ↔ B"
                            enabled: laboratorio.hasClipA && laboratorio.hasClipB
                            onPulsado: laboratorio.swapClips()
                        }

                        Separador {}
                        Titulo { texto: "Transición" }
                        Selector {
                            etiqueta: "Tipo"
                            model: laboratorio.transitionNames
                            indice: laboratorio.transitionIndex
                            onElegido: (i) => laboratorio.transitionIndex = i
                        }
                        Deslizador {
                            etiqueta: "Duración"
                            textoValor: laboratorio.durationText
                            from: 4
                            to: 120
                            stepSize: 1
                            valor: laboratorio.durationFrames
                            onCambiado: (v) => laboratorio.durationFrames = Math.round(v)
                        }
                        Deslizador {
                            etiqueta: "Intensidad"
                            textoValor: Math.round(laboratorio.intensity * 100) + " %"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            valor: laboratorio.intensity
                            onCambiado: (v) => laboratorio.intensity = v
                        }
                        Deslizador {
                            etiqueta: "Desenfoque de movimiento"
                            textoValor: Math.round(laboratorio.blur * 100) + " %"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            valor: laboratorio.blur
                            onCambiado: (v) => laboratorio.blur = v
                        }
                        Selector {
                            etiqueta: "Curva de movimiento"
                            model: laboratorio.easingNames
                            indice: laboratorio.easingIndex
                            onElegido: (i) => laboratorio.easingIndex = i
                        }
                        Boton {
                            Layout.fillWidth: true
                            texto: "Restablecer valores de esta transición"
                            onPulsado: laboratorio.resetParams()
                        }
                        Separador {}
                        Titulo { texto: "Exportar" }
                        Boton {
                            Layout.fillWidth: true
                            visible: !laboratorio.exporting
                            enabled: laboratorio.ready
                            acentuado: true
                            texto: "Exportar prueba a MP4…"
                            onPulsado: dialogoExportar.open()
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: laboratorio.exporting
                            spacing: 6
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 6
                                radius: 3
                                color: Theme.panelElevado
                                Rectangle {
                                    width: parent.width * laboratorio.exportProgress
                                    height: parent.height
                                    radius: 3
                                    color: Theme.acento
                                }
                            }
                            Boton {
                                Layout.fillWidth: true
                                texto: "Cancelar exportación"
                                onPulsado: laboratorio.cancelExport()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: laboratorio.exportMessage.length > 0
                            wrapMode: Text.WrapAnywhere
                            color: laboratorio.exportMessage.startsWith("No se") ? Theme.aviso
                                                                                 : Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            text: laboratorio.exportMessage
                        }

                        Separador {}
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            text: "Pulsa «Ver transición» (T) para reproducir desde un segundo antes "
                                  + "hasta un segundo después. Los cambios se ven al instante, "
                                  + "también en pausa: avanza fotograma a fotograma con ← y →."
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: ventana.enEditor
            Layout.fillWidth: true
            Layout.fillHeight: false // su altura es la de la línea de tiempo, no se estira
            Layout.preferredHeight: lineaTiempo.implicitHeight
            spacing: Theme.margen
            LineaTiempo {
                id: lineaTiempo
                editor: editor
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            Medidor {
                Layout.fillHeight: true
                izquierdo: editor.meterLeft
                derecho: editor.meterRight
            }
        }

        // Pie: estado y, en el editor, exportar en la esquina inferior derecha
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                Layout.fillWidth: true
                readonly property string error: ventana.activo.errorString
                readonly property bool ocupado: ventana.activo.busy || (ventana.enEditor && editor.importing)
                text: error.length > 0 ? "Aviso: " + error
                      : ventana.enEditor && editor.fileBusy ? "Leyendo o guardando el proyecto…"
                      : ventana.enEditor && editor.importing ? "Importando…"
                      : ocupado ? "Cargando…" : "Listo"
                color: error.length > 0 ? Theme.aviso : Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
                elide: Text.ElideRight
            }

            // Exportación en curso o terminada
            Text {
                visible: ventana.enEditor && editor.exportMessage.length > 0
                Layout.maximumWidth: 520
                text: editor.exportMessage
                color: editor.exportMessage.startsWith("No se") ? Theme.aviso : Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
                elide: Text.ElideMiddle
            }
            Rectangle {
                visible: ventana.enEditor && editor.exporting
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
                visible: ventana.enEditor && editor.exporting
                icono: "cerrar"
                texto: "Cancelar"
                onPulsado: editor.cancelExport()
            }
            BotonIcono {
                visible: ventana.enEditor
                icono: "exportar"
                texto: "Exportar"
                ayuda: "Exportar la línea de tiempo (Ctrl+M)"
                acentuado: true
                implicitHeight: 36
                implicitWidth: 132
                enabled: editor.frameCount > 0 && !editor.exporting
                onPulsado: dialogoExportarLinea.open()
            }
        }
    }

    // Reencuadre en el visor (pestaña Editor): clic sobre un clip visible para
    // seleccionarlo (el de más arriba), arrastrarlo para moverlo y arrastrar sus esquinas
    // para escalarlo. Se ajusta a los bordes y al centro del lienzo (Mayús: sin ajuste).
    // Mientras se arrastra solo es una vista previa; al soltar se aplica (un paso de
    // deshacer).
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
        function contiene(p) {
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

    // Resumen de los clips A y B del laboratorio.
    component ResumenClips: ColumnLayout {
        Layout.fillWidth: true
        spacing: 10

        FilaDato {
            etiqueta: "Clip A"
            valor: laboratorio.hasClipA ? laboratorio.clipAName : "Sin asignar"
            colorValor: laboratorio.hasClipA ? Theme.texto : Theme.textoDesactivado
        }
        Text {
            visible: laboratorio.hasClipA
            text: laboratorio.clipARange
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }
        FilaDato {
            etiqueta: "Clip B"
            valor: laboratorio.hasClipB ? laboratorio.clipBName : "Sin asignar"
            colorValor: laboratorio.hasClipB ? Theme.texto : Theme.textoDesactivado
        }
        Text {
            visible: laboratorio.hasClipB
            text: laboratorio.clipBRange
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }
    }

    component Titulo: Text {
        property string texto
        text: texto
        color: Theme.texto
        font.pixelSize: 16
        font.bold: true
    }

    component Separador: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 2
        implicitHeight: 1
        color: Theme.borde
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

    // Lo que se ve al arrastrar un medio de la biblioteca a la línea de tiempo.
    Rectangle {
        id: fantasma
        property var mediaId: undefined
        property string nombre
        visible: Drag.active
        z: 100
        width: Math.min(220, textoFantasma.implicitWidth + 20)
        height: 28
        radius: Theme.radio
        color: Theme.panelElevado
        border.color: Theme.acento
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
