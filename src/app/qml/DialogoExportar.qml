import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

// Ventana de exportación: nombre, ubicación y calidad. El formato es el lienzo del
// proyecto (se exporta tal cual se edita).
Popup {
    id: dialogo

    required property Editor editor
    property url carpeta
    property int calidad: 1

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 600
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.velo }
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radio
        border.color: Theme.borde
    }

    onAboutToShow: {
        nombre.text = editor.projectName
        carpeta = editor.defaultExportFolder
    }

    // "file:///C:/Videos" → "C:/Videos" (y "file:///home/…" → "/home/…").
    function rutaLegible(url) {
        let texto = url.toString()
        if (texto.startsWith("file:///") && texto.charAt(9) === ":")
            texto = texto.substring(8)
        else if (texto.startsWith("file://"))
            texto = texto.substring(7)
        return decodeURIComponent(texto)
    }

    function exportar() {
        if (nombre.text.trim().length === 0)
            return
        editor.exportToFolder(carpeta, nombre.text.trim(), calidad)
        close()
    }

    FolderDialog {
        id: elegirCarpeta
        title: "Carpeta de destino"
        currentFolder: dialogo.carpeta
        onAccepted: dialogo.carpeta = selectedFolder
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Cabecera
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 20
            Text {
                Layout.fillWidth: true
                text: "Exportar video"
                color: Theme.texto
                font.pixelSize: 17
                font.bold: true
            }
            BotonIcono { icono: "cerrar"; ayuda: "Cerrar (Esc)"; onPulsado: dialogo.close() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borde }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 18

            Etiqueta { texto: "Formato (el lienzo del proyecto)" }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: -10
                implicitHeight: 62
                radius: Theme.radio
                color: Theme.fondo
                border.color: Theme.borde
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    spacing: 14
                    Rectangle {
                        readonly property real proporcion: dialogo.editor.canvas.width
                                                           / Math.max(1, dialogo.editor.canvas.height)
                        implicitWidth: proporcion >= 1 ? 40 : 40 * proporcion
                        implicitHeight: proporcion >= 1 ? 40 / proporcion : 40
                        radius: 3
                        color: "transparent"
                        border.color: Theme.acento
                        border.width: 2
                    }
                    ColumnLayout {
                        spacing: 2
                        Text {
                            text: "Lienzo " + dialogo.editor.canvasLabel + " · "
                                  + dialogo.editor.projectDescription
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            font.bold: true
                        }
                        Text {
                            text: "Se exporta tal cual se edita."
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                        }
                    }
                }
            }

            Etiqueta { texto: "Nombre del archivo" }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: -10
                implicitHeight: 36
                radius: Theme.radio
                color: Theme.fondo
                border.color: nombre.activeFocus ? Theme.acento : Theme.borde
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    TextField {
                        id: nombre
                        Layout.fillWidth: true
                        color: Theme.texto
                        font.pixelSize: Theme.tamTexto
                        background: null
                        padding: 0
                        selectByMouse: true
                        onAccepted: dialogo.exportar()
                    }
                    Text { text: ".mp4"; color: Theme.textoDesactivado; font.pixelSize: Theme.tamTexto }
                }
            }

            Etiqueta { texto: "Ubicación" }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: -10
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    radius: Theme.radio
                    color: Theme.fondo
                    border.color: Theme.borde
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: dialogo.rutaLegible(dialogo.carpeta)
                        color: Theme.texto
                        font.pixelSize: Theme.tamTexto
                        elide: Text.ElideMiddle
                    }
                }
                BotonIcono {
                    icono: "abrir"
                    texto: "Elegir…"
                    implicitHeight: 36
                    onPulsado: elegirCarpeta.open()
                }
            }

            Etiqueta { texto: "Calidad" }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: -10
                spacing: 8
                Repeater {
                    model: [{ nombre: "Ligera", nota: "8 Mbps · más pequeño" },
                            { nombre: "Alta", nota: "16 Mbps · recomendada" },
                            { nombre: "Máxima", nota: "40 Mbps · para reeditar" }]
                    delegate: Rectangle {
                        id: opcion
                        required property var modelData
                        required property int index
                        readonly property bool elegida: dialogo.calidad === index
                        Layout.fillWidth: true
                        implicitHeight: 56
                        radius: Theme.radio
                        color: elegida ? Theme.panelElevado : Theme.fondo
                        border.color: elegida ? Theme.acento : Theme.borde
                        Column {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: opcion.modelData.nombre
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                font.bold: true
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: opcion.modelData.nota
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTextoPequeno
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: dialogo.calidad = opcion.index
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "H.264 por la GPU si está disponible (NVENC en tu RTX) · audio AAC 48 kHz · "
                      + "duración " + dialogo.editor.durationTimecode
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borde }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.rightMargin: 20
            spacing: 10
            Item { Layout.fillWidth: true }
            BotonIcono { texto: "Cancelar"; implicitHeight: 36; onPulsado: dialogo.close() }
            BotonIcono {
                icono: "exportar"
                texto: "Exportar"
                acentuado: true
                implicitHeight: 36
                enabled: nombre.text.trim().length > 0
                onPulsado: dialogo.exportar()
            }
        }
    }

    component Etiqueta: Text {
        property string texto
        text: texto.toUpperCase()
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
        font.letterSpacing: 0.6
    }
}
