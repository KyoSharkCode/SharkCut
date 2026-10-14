import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

Window {
    id: ventana

    width: 1360
    height: 820
    minimumWidth: 1000
    minimumHeight: 620
    visible: true
    title: reproductor.fileName.length > 0
           ? reproductor.fileName + " — SharkCut " + AppInfo.appVersion
           : "SharkCut " + AppInfo.appVersion
    color: Theme.fondo

    // "origen": ver un video y marcar tramos · "transicion": probar la transición A → B
    property string modo: "origen"
    readonly property bool enOrigen: modo === "origen"

    function cambiarModo(nuevo) {
        if (nuevo === modo)
            return
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

    // Archivos al abrir: uno se abre en Origen; con dos, se cargan como clips A y B.
    property int cargaInicial: AppInfo.initialFiles.length >= 2 ? 0 : -1
    Component.onCompleted: {
        if (AppInfo.initialFiles.length > 0)
            reproductor.openPath(AppInfo.initialFiles[0])
    }
    Connections {
        target: reproductor
        enabled: ventana.cargaInicial >= 0
        function onLoadedChanged() {
            if (!reproductor.loaded)
                return
            if (ventana.cargaInicial === 0) {
                laboratorio.setClipA(reproductor)
                ventana.cargaInicial = 1
                reproductor.openPath(AppInfo.initialFiles[1])
            } else {
                laboratorio.setClipB(reproductor)
                ventana.cargaInicial = -1
                ventana.cambiarModo("transicion")
            }
        }
    }

    // Atajos de teclado (estilo Premiere). Actúan sobre la pestaña visible.
    readonly property var activo: enOrigen ? reproductor : laboratorio
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
    Shortcut { sequences: [StandardKey.Open]; onActivated: dialogoAbrir.open() }
    Shortcut { sequence: "I"; enabled: ventana.enOrigen; onActivated: reproductor.setMarkIn() }
    Shortcut { sequence: "O"; enabled: ventana.enOrigen; onActivated: reproductor.setMarkOut() }
    Shortcut { sequence: "T"; enabled: !ventana.enOrigen; onActivated: laboratorio.playTransition() }
    Shortcut { sequence: "1"; onActivated: ventana.cambiarModo("origen") }
    Shortcut { sequence: "2"; onActivated: ventana.cambiarModo("transicion") }

    ColumnLayout {
        id: contenido
        anchors.fill: parent
        anchors.margins: Theme.margen
        spacing: Theme.margen

        // Barra superior
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: "SharkCut"
                color: Theme.texto
                font.pixelSize: Theme.tamTitulo
                font.bold: true
            }
            Chip { texto: "FASE 1 · TRANSICIONES" }

            Item { implicitWidth: 12 }
            Pestana {
                texto: "1 · Origen"
                activa: ventana.enOrigen
                onPulsada: ventana.cambiarModo("origen")
            }
            Pestana {
                texto: "2 · Transición"
                activa: !ventana.enOrigen
                onPulsada: ventana.cambiarModo("transicion")
            }

            Item { Layout.fillWidth: true }
            Boton {
                texto: "Abrir video…"
                onPulsado: dialogoAbrir.open()
            }
            Text {
                text: "v" + AppInfo.appVersion
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTexto
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.margen

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

                    VideoViewer {
                        id: visor
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: ventana.enOrigen ? reproductor : laboratorio
                        backgroundColor: Theme.visorFondo

                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            visible: ventana.enOrigen ? !reproductor.loaded : !laboratorio.ready

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: ventana.enOrigen
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
                                text: ventana.enOrigen
                                      ? "Arrastra un video aquí o pulsa «Abrir video…» (Ctrl+O)."
                                      : "En la pestaña Origen, marca un tramo y envíalo como clip A\n"
                                        + "y otro como clip B. Después vuelve aquí."
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTexto
                            }
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

                            Boton { texto: "|◀"; onPulsado: { reproductor.pause(); reproductor.seek(0) } }
                            Boton { texto: "◀"; onPulsado: reproductor.step(-1) }
                            Boton {
                                texto: reproductor.playing ? "❚❚" : "▶"
                                acentuado: true
                                implicitWidth: 56
                                onPulsado: reproductor.togglePlay()
                            }
                            Boton { texto: "▶"; onPulsado: reproductor.step(1) }
                            Boton {
                                texto: "▶|"
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
                        visible: !ventana.enOrigen
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

                            Boton { texto: "|◀"; onPulsado: { laboratorio.pause(); laboratorio.seek(0) } }
                            Boton { texto: "◀"; onPulsado: laboratorio.step(-1) }
                            Boton {
                                texto: laboratorio.playing ? "❚❚" : "▶"
                                acentuado: true
                                implicitWidth: 56
                                onPulsado: laboratorio.togglePlay()
                            }
                            Boton { texto: "▶"; onPulsado: laboratorio.step(1) }
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
                        if (drop.hasUrls && drop.urls.length > 0) {
                            ventana.cambiarModo("origen")
                            reproductor.open(drop.urls[0])
                            drop.acceptProposedAction()
                        }
                    }
                }
            }

            // Panel lateral
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 16
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
                        visible: !ventana.enOrigen
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

        // Barra de estado
        Text {
            Layout.fillWidth: true
            readonly property string error: ventana.enOrigen ? reproductor.errorString
                                                             : laboratorio.errorString
            readonly property bool ocupado: ventana.enOrigen ? reproductor.busy : laboratorio.busy
            text: error.length > 0 ? "Aviso: " + error : ocupado ? "Cargando…" : "Listo"
            color: error.length > 0 ? Theme.aviso : Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            elide: Text.ElideRight
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
}
