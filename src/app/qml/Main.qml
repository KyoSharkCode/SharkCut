import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

Window {
    id: ventana

    width: 1280
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: reproductor.fileName.length > 0
           ? reproductor.fileName + " — SharkCut " + AppInfo.appVersion
           : "SharkCut " + AppInfo.appVersion
    color: Theme.fondo

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

    // Una vez por refresco de pantalla: avanza la reproducción y mide los fps.
    FrameAnimation {
        id: reloj
        running: true
        onTriggered: reproductor.advance()
    }
    readonly property real fpsVisor: reloj.smoothFrameTime > 0 ? 1 / reloj.smoothFrameTime : 0

    FileDialog {
        id: dialogoAbrir
        title: "Abrir video"
        nameFilters: ["Videos (*.mp4 *.mkv *.mov *.m4v *.webm *.avi *.flv *.ts)",
                      "Todos los archivos (*)"]
        onAccepted: reproductor.open(selectedFile)
    }

    Component.onCompleted: {
        if (AppInfo.initialFile.length > 0)
            reproductor.openPath(AppInfo.initialFile)
    }

    // Atajos de teclado (estilo Premiere).
    Shortcut { sequence: "Space"; onActivated: reproductor.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: reproductor.step(-1) }
    Shortcut { sequence: "Right"; onActivated: reproductor.step(1) }
    Shortcut { sequence: "Shift+Left"; onActivated: reproductor.step(-10) }
    Shortcut { sequence: "Shift+Right"; onActivated: reproductor.step(10) }
    Shortcut { sequence: "Home"; onActivated: { reproductor.pause(); reproductor.seek(0) } }
    Shortcut {
        sequence: "End"
        onActivated: { reproductor.pause(); reproductor.seek(reproductor.frameCount - 1) }
    }
    Shortcut { sequences: [StandardKey.Open]; onActivated: dialogoAbrir.open() }

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
            Chip {
                texto: "FASE 0 · REPRODUCTOR"
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

            // Visor + controles de reproducción
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
                        player: reproductor
                        backgroundColor: Theme.visorFondo

                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            visible: !reproductor.loaded

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: reproductor.busy ? "Abriendo…" : "Visor"
                                color: Theme.texto
                                font.pixelSize: Theme.tamTitulo
                                font.bold: true
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: !reproductor.busy
                                text: "Arrastra un video aquí o pulsa «Abrir video…» (Ctrl+O)."
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTexto
                            }
                        }
                    }

                    ControlesReproduccion {
                        Layout.fillWidth: true
                        enabled: reproductor.loaded
                        opacity: enabled ? 1 : 0.4
                    }
                }

                DropArea {
                    id: zonaSoltar
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (drop) => {
                        if (drop.hasUrls && drop.urls.length > 0) {
                            reproductor.open(drop.urls[0])
                            drop.acceptProposedAction()
                        }
                    }
                }
            }

            // Panel de diagnóstico
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 16
                    contentHeight: columnaDatos.implicitHeight
                    clip: true

                    ColumnLayout {
                        id: columnaDatos
                        width: parent.width
                        spacing: 12

                        Text {
                            text: "Clip"
                            color: Theme.texto
                            font.pixelSize: 16
                            font.bold: true
                        }
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
                        FilaDato {
                            etiqueta: "Decodificación"
                            valor: reproductor.decoderText
                            colorValor: !reproductor.loaded ? Theme.texto
                                        : reproductor.hardwareDecoding ? Theme.ok
                                        : esWindows ? Theme.aviso : Theme.texto
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            implicitHeight: 1
                            color: Theme.borde
                        }

                        Text {
                            text: "Sistema"
                            color: Theme.texto
                            font.pixelSize: 16
                            font.bold: true
                        }
                        FilaDato {
                            etiqueta: "API gráfica"
                            valor: ventana.apiGrafica
                            colorValor: ventana.apiEsperada ? Theme.ok : Theme.aviso
                        }
                        FilaDato {
                            etiqueta: "Fps del visor"
                            valor: Math.round(ventana.fpsVisor).toString()
                        }
                        FilaDato { etiqueta: "Qt"; valor: AppInfo.qtVersion }
                        FilaDato { etiqueta: "Sistema"; valor: AppInfo.operatingSystem }
                        FilaDato {
                            etiqueta: "SharkCut"
                            valor: AppInfo.appVersion + " (" + AppInfo.buildType + ")"
                        }
                    }
                }
            }
        }

        // Barra de estado
        Text {
            Layout.fillWidth: true
            text: reproductor.errorString.length > 0 ? "Error: " + reproductor.errorString
                  : reproductor.busy ? "Cargando…"
                  : "Listo"
            color: reproductor.errorString.length > 0 ? Theme.error : Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            elide: Text.ElideRight
        }
    }

    // Botones, código de tiempo y barra de búsqueda bajo el visor.
    component ControlesReproduccion: ColumnLayout {
        spacing: 6

        BarraBusqueda {
            Layout.fillWidth: true
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

            Item { Layout.fillWidth: true }

            Text {
                text: reproductor.timecode
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                font.family: "Consolas, monospace"
            }
            Text {
                text: "/ " + reproductor.durationTimecode
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTexto
                font.family: "Consolas, monospace"
            }
        }
    }

    // Barra para saltar a cualquier punto del clip.
    component BarraBusqueda: Item {
        id: barra
        implicitHeight: 20

        readonly property real progreso: reproductor.frameCount > 1
                                          ? reproductor.currentFrame / (reproductor.frameCount - 1)
                                          : 0

        Rectangle {
            id: pista
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 4
            radius: 2
            color: Theme.panelElevado

            Rectangle {
                width: pista.width * barra.progreso
                height: parent.height
                radius: parent.radius
                color: Theme.acento
            }
        }
        Rectangle {
            x: pista.width * barra.progreso - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: 16
            radius: 2
            color: Theme.cabezal
        }
        MouseArea {
            anchors.fill: parent
            function buscar(mouseX) {
                const fraccion = Math.max(0, Math.min(1, mouseX / width))
                reproductor.seek(Math.round(fraccion * (reproductor.frameCount - 1)))
            }
            onPressed: (mouse) => buscar(mouse.x)
            onPositionChanged: (mouse) => { if (pressed) buscar(mouse.x) }
        }
    }

    // Botón sencillo con el estilo del tema.
    component Boton: Rectangle {
        id: boton
        property string texto
        property bool acentuado: false
        signal pulsado()

        implicitWidth: Math.max(36, etiquetaBoton.implicitWidth + 20)
        implicitHeight: 30
        radius: Theme.radio
        color: zonaBoton.pressed ? Theme.borde
               : zonaBoton.containsMouse ? Theme.panelElevado
               : acentuado ? Theme.panelElevado : "transparent"
        border.color: acentuado ? Theme.acento : Theme.borde

        Text {
            id: etiquetaBoton
            anchors.centerIn: parent
            text: boton.texto
            color: boton.acentuado ? Theme.acento : Theme.texto
            font.pixelSize: Theme.tamTexto
        }
        MouseArea {
            id: zonaBoton
            anchors.fill: parent
            hoverEnabled: true
            onClicked: boton.pulsado()
        }
    }

    // Etiqueta discreta en mayúsculas con borde de acento.
    component Chip: Rectangle {
        property string texto

        implicitWidth: etiquetaChip.implicitWidth + 14
        implicitHeight: etiquetaChip.implicitHeight + 6
        radius: height / 2
        color: Theme.panelElevado
        border.color: Theme.borde

        Text {
            id: etiquetaChip
            anchors.centerIn: parent
            text: parent.texto
            color: Theme.acento
            font.pixelSize: Theme.tamTextoPequeno
            font.bold: true
            font.letterSpacing: 1
        }
    }

    // Fila "etiqueta: valor" del panel de diagnóstico.
    component FilaDato: ColumnLayout {
        property string etiqueta
        property string valor
        property color colorValor: Theme.texto

        Layout.fillWidth: true
        spacing: 2

        Text {
            text: parent.etiqueta.toUpperCase()
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.letterSpacing: 1
        }
        Text {
            Layout.fillWidth: true
            text: parent.valor
            color: parent.colorValor
            font.pixelSize: Theme.tamTexto
            elide: Text.ElideMiddle
        }
    }
}
