import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Control deslizante con etiqueta y valor, con el estilo del tema.
ColumnLayout {
    id: raiz

    property string etiqueta
    property string textoValor
    property alias from: control.from
    property alias to: control.to
    property alias stepSize: control.stepSize
    property real valor
    signal cambiado(real valor)

    Layout.fillWidth: true
    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: raiz.etiqueta.toUpperCase()
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.letterSpacing: 1
        }
        Item { Layout.fillWidth: true }
        Text {
            text: raiz.textoValor
            color: Theme.texto
            font.pixelSize: Theme.tamTextoPequeno
        }
    }

    Slider {
        id: control
        Layout.fillWidth: true
        value: raiz.valor
        onMoved: raiz.cambiado(value)

        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: control.availableWidth
            height: 4
            radius: 2
            color: Theme.panelElevado

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.acento
            }
        }
        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: control.pressed ? Theme.cabezal : Theme.texto
            border.color: Theme.fondo
        }
    }
}
