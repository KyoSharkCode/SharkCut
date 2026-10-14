import QtQuick
import SharkCut

// Pestaña de la barra superior.
Rectangle {
    id: pestana

    property string texto
    property bool activa: false
    signal pulsada()

    implicitWidth: etiqueta.implicitWidth + 28
    implicitHeight: 30
    radius: Theme.radio
    color: activa ? Theme.panelElevado : zona.containsMouse ? Theme.panel : "transparent"
    border.color: activa ? Theme.acento : "transparent"

    Text {
        id: etiqueta
        anchors.centerIn: parent
        text: pestana.texto
        color: pestana.activa ? Theme.texto : Theme.textoSecundario
        font.pixelSize: Theme.tamTexto
        font.bold: pestana.activa
    }
    MouseArea {
        id: zona
        anchors.fill: parent
        hoverEnabled: true
        onClicked: pestana.pulsada()
    }
}
