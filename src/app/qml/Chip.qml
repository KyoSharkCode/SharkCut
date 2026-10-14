import QtQuick
import SharkCut

// Etiqueta discreta en mayúsculas con borde.
Rectangle {
    property string texto
    property color colorTexto: Theme.acento

    implicitWidth: etiqueta.implicitWidth + 14
    implicitHeight: etiqueta.implicitHeight + 6
    radius: height / 2
    color: Theme.panelElevado
    border.color: Theme.borde

    Text {
        id: etiqueta
        anchors.centerIn: parent
        text: parent.texto
        color: parent.colorTexto
        font.pixelSize: Theme.tamTextoPequeno
        font.bold: true
        font.letterSpacing: 1
    }
}
