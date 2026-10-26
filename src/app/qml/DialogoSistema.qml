import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Información del equipo: la GPU con la que dibuja el visor, cómo se decodifica el video
// y qué codificadores usaría la exportación (y si es el respaldo por software).
Popup {
    id: dialogo

    required property var editor
    readonly property var info: editor.hardwareInfo

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    focus: true
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.velo }
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radio
        border.color: Theme.borde
    }

    onOpened: editor.refreshHardwareInfo()

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: "Hardware y codificadores"
            color: Theme.texto
            font.pixelSize: 18
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "SharkCut usa la GPU para mostrar, decodificar y exportar el video siempre "
                  + "que puede. Si no, usa el procesador (respaldo por software): funciona "
                  + "igual, pero más lento."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: datos.implicitHeight + 20
            radius: Theme.radio
            color: Theme.fondo
            border.color: Theme.borde
            GridLayout {
                id: datos
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                columns: 2
                columnSpacing: 14
                rowSpacing: 7

                Nombre { texto: "GPU" }
                Valor {
                    texto: dialogo.info.gpu
                           ? dialogo.info.gpu + " (" + dialogo.info.api + ")"
                           : "Se sabrá al abrir el editor"
                    pendiente: !dialogo.info.gpu
                }
                Nombre { texto: "Decodificación" }
                Valor {
                    texto: dialogo.info.decoder || "Comprobando…"
                    pendiente: !dialogo.info.decoder
                    respaldo: !!dialogo.info.decoder && !dialogo.info.decoderGpu
                }
                Nombre { texto: "Exportar en H.264" }
                Valor {
                    texto: dialogo.info.h264 || "Comprobando…"
                    pendiente: !dialogo.info.h264
                    respaldo: dialogo.info.h264Software
                }
                Nombre { texto: "Exportar en H.265" }
                Valor {
                    texto: dialogo.info.h265 || "Comprobando…"
                    pendiente: !dialogo.info.h265
                    respaldo: dialogo.info.h265Software
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: dialogo.info.probed === true
                     && (!dialogo.info.decoderGpu || dialogo.info.h264Software
                         || dialogo.info.h265Software)
            wrapMode: Text.WordWrap
            text: "En amarillo, lo que va por software. Actualizar el controlador de la "
                  + "tarjeta gráfica suele bastar para que vuelva a usarse la GPU."
            color: Theme.aviso
            font.pixelSize: Theme.tamTextoPequeno
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Boton {
                texto: dialogo.info.probing ? "Comprobando…" : "Volver a comprobar"
                enabled: !dialogo.info.probing
                onPulsado: dialogo.editor.refreshHardwareInfo()
            }
            Item { Layout.fillWidth: true }
            Boton {
                texto: "Cerrar"
                onPulsado: dialogo.close()
            }
        }
    }

    component Nombre: Text {
        property string texto
        Layout.alignment: Qt.AlignTop
        text: texto
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
    }

    component Valor: Text {
        property string texto
        property bool pendiente: false
        property bool respaldo: false
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: texto
        color: pendiente ? Theme.textoSecundario : respaldo ? Theme.aviso : Theme.texto
        font.pixelSize: Theme.tamTextoPequeno
    }
}
