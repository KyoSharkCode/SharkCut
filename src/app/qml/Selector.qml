import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Lista desplegable con etiqueta, con el estilo del tema.
ColumnLayout {
    id: raiz

    property string etiqueta
    property alias model: control.model
    property int indice: 0
    signal elegido(int indice)

    Layout.fillWidth: true
    spacing: 4

    Text {
        text: raiz.etiqueta.toUpperCase()
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
        font.letterSpacing: 1
    }

    ComboBox {
        id: control
        Layout.fillWidth: true
        currentIndex: raiz.indice
        onActivated: (index) => raiz.elegido(index)

        contentItem: Text {
            leftPadding: 10
            text: control.displayText
            color: Theme.texto
            font.pixelSize: Theme.tamTexto
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 32
            radius: Theme.radio
            color: control.hovered ? Theme.panelElevado : Theme.fondo
            border.color: control.popup.visible ? Theme.acento : Theme.borde
        }
        indicator: Text {
            x: control.width - width - 10
            y: (control.height - height) / 2
            text: "▾"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTexto
        }
        delegate: ItemDelegate {
            required property int index
            required property string modelData
            width: control.width
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                text: modelData
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: highlighted ? Theme.panelElevado : Theme.panel
            }
        }
        popup: Popup {
            y: control.height + 2
            width: control.width
            padding: 2
            // Las listas largas no pasan de 360 px: se desplazan.
            implicitHeight: Math.min(contentItem.implicitHeight, 360) + 4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: BarraDesplazamiento {}
            }
            background: Rectangle {
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde
            }
        }
    }
}
