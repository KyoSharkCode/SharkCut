import QtQuick
import QtQuick.Layouts
import SharkCut

// Medidor de la mezcla: nivel de cada canal (L y R) de lo que suena, en dB.
// 20 segmentos de 3 dB, de 0 dB arriba a −60 abajo: verde, amarillo cerca del
// máximo y rojo en los últimos 6 dB.
Rectangle {
    id: medidor

    property real izquierdo: -60
    property real derecho: -60

    color: Theme.panel
    radius: Theme.radio
    border.color: Theme.borde
    implicitWidth: 76

    readonly property real maximo: Math.max(izquierdo, derecho)

    component Barra: Column {
        property real nivel: -60
        width: 12
        spacing: 2
        Repeater {
            model: 20
            delegate: Rectangle {
                required property int index
                width: 12
                height: (parent.height - 19 * 2) / 20
                radius: 1
                readonly property bool encendido: nivel > -3 * (index + 1)
                color: !encendido ? Theme.panelElevado
                       : index < 2 ? Theme.error
                       : index < 6 ? Theme.aviso
                       : Theme.ok
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Mezcla"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.bold: true
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Escala en dB
            Repeater {
                model: [0, -6, -12, -18, -24, -36, -48]
                delegate: Text {
                    required property int modelData
                    x: 0
                    width: 20
                    y: (-modelData / 60) * parent.height - height / 2
                    horizontalAlignment: Text.AlignRight
                    text: modelData
                    color: Theme.textoDesactivado
                    font.pixelSize: 9
                    font.family: "Consolas, monospace"
                }
            }
            Row {
                x: 26
                height: parent.height
                spacing: 4
                Barra { height: parent.height; nivel: medidor.izquierdo }
                Barra { height: parent.height; nivel: medidor.derecho }
            }
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            leftPadding: 18
            spacing: 9
            Text { text: "L"; color: Theme.textoDesactivado; font.pixelSize: 10 }
            Text { text: "R"; color: Theme.textoDesactivado; font.pixelSize: 10 }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: medidor.maximo <= -59.5 ? "−∞ dB" : medidor.maximo.toFixed(1) + " dB"
            color: medidor.maximo > -0.5 ? Theme.error : Theme.texto
            font.pixelSize: Theme.tamTextoPequeno
            font.family: "Consolas, monospace"
        }
    }
}
