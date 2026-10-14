import QtQuick
import QtQuick.Layouts
import SharkCut

Window {
    id: ventana

    width: 1280
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "SharkCut " + AppInfo.appVersion
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
    readonly property bool apiEsperada: Qt.platform.os !== "windows"
                                        || contenido.GraphicsInfo.api === GraphicsInfo.Direct3D11

    // Mide los fotogramas por segundo reales del render.
    FrameAnimation {
        id: reloj
        running: true
    }
    readonly property real fpsVisor: reloj.smoothFrameTime > 0 ? 1 / reloj.smoothFrameTime : 0

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
                texto: "FASE 0 · CIMIENTOS"
            }
            Item { Layout.fillWidth: true }
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

            // Visor (de momento solo comprueba que el render en GPU funciona)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde

                Rectangle {
                    id: visor
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.margen,
                                    (parent.height - 2 * Theme.margen) * 16 / 9)
                    height: width * 9 / 16
                    color: Theme.visorFondo
                    radius: 4
                    clip: true

                    // Aleta que nada de lado a lado: si se mueve suave, el render va bien.
                    Text {
                        id: aleta
                        text: "🦈"
                        font.pixelSize: visor.height * 0.12
                        y: visor.height * 0.55
                        x: (visor.width + width) * ((reloj.elapsedTime / 6) % 1) - width
                    }

                    Column {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -visor.height * 0.12
                        spacing: 6

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Visor"
                            color: Theme.texto
                            font.pixelSize: Theme.tamTitulo
                            font.bold: true
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Aquí se reproducirá el video en la siguiente etapa de la fase 0."
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTexto
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

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text {
                        text: "Diagnóstico"
                        color: Theme.texto
                        font.pixelSize: 16
                        font.bold: true
                    }

                    FilaDato { etiqueta: "SharkCut"; valor: AppInfo.appVersion + " (" + AppInfo.buildType + ")" }
                    FilaDato { etiqueta: "Qt"; valor: AppInfo.qtVersion }
                    FilaDato { etiqueta: "Sistema"; valor: AppInfo.operatingSystem }
                    FilaDato {
                        etiqueta: "API gráfica"
                        valor: ventana.apiGrafica
                        colorValor: ventana.apiEsperada ? Theme.ok : Theme.aviso
                    }
                    FilaDato {
                        etiqueta: "Fps del visor"
                        valor: Math.round(ventana.fpsVisor).toString()
                    }

                    Item { Layout.fillHeight: true }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textoSecundario
                        font.pixelSize: Theme.tamTextoPequeno
                        text: ventana.apiEsperada
                              ? "Todo en orden: el render usa la API prevista."
                              : "Aviso: en Windows se esperaba Direct3D 11."
                    }
                }
            }
        }

        // Barra de estado
        Text {
            Layout.fillWidth: true
            text: "Listo"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
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
            elide: Text.ElideRight
        }
    }
}
