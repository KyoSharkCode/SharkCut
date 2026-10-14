import QtQuick
import SharkCut

// Botón sencillo con el estilo del tema.
Rectangle {
    id: boton

    property string texto
    property bool acentuado: false
    property string ayuda // texto de ayuda (tooltip) opcional
    signal pulsado()

    implicitWidth: Math.max(36, etiqueta.implicitWidth + 20)
    implicitHeight: 30
    radius: Theme.radio
    opacity: enabled ? 1 : 0.4
    color: zona.pressed ? Theme.borde
           : zona.containsMouse ? Theme.panelElevado
           : acentuado ? Theme.panelElevado : "transparent"
    border.color: acentuado ? Theme.acento : Theme.borde

    Text {
        id: etiqueta
        anchors.centerIn: parent
        text: boton.texto
        color: boton.acentuado ? Theme.acento : Theme.texto
        font.pixelSize: Theme.tamTexto
    }
    MouseArea {
        id: zona
        anchors.fill: parent
        hoverEnabled: true
        enabled: boton.enabled
        onClicked: boton.pulsado()
    }
}
