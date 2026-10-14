import QtQuick
import QtQuick.Controls.Basic
import SharkCut

// Botón con icono (y texto opcional). `activo`: herramienta o modo encendido.
// `ayuda`: nombre y atajo, se muestran al pasar el ratón.
Rectangle {
    id: boton

    property string icono
    property string texto
    property string ayuda
    property bool activo: false
    property bool acentuado: false // acción principal (borde y texto de acento)
    signal pulsado()

    implicitWidth: texto.length > 0 ? fila.implicitWidth + 22 : 34
    implicitHeight: 30
    radius: Theme.radio
    opacity: enabled ? 1 : 0.4
    color: zona.pressed ? Theme.borde
           : activo || zona.containsMouse ? Theme.panelElevado
           : "transparent"
    border.color: activo || acentuado ? Theme.acento : Theme.borde

    Row {
        id: fila
        anchors.centerIn: parent
        spacing: 7
        Icono {
            anchors.verticalCenter: parent.verticalCenter
            visible: boton.icono.length > 0
            nombre: boton.icono
            tam: 16
            color: boton.activo || boton.acentuado ? Theme.acento : Theme.texto
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: boton.texto.length > 0
            text: boton.texto
            color: boton.activo || boton.acentuado ? Theme.acento : Theme.texto
            font.pixelSize: Theme.tamTexto
        }
    }

    MouseArea {
        id: zona
        anchors.fill: parent
        hoverEnabled: true
        enabled: boton.enabled
        onClicked: boton.pulsado()
    }

    ToolTip {
        id: ayudaEmergente
        visible: boton.ayuda.length > 0 && zona.containsMouse
        delay: 500
        text: boton.ayuda
        contentItem: Text {
            text: ayudaEmergente.text
            color: Theme.texto
            font.pixelSize: Theme.tamTextoPequeno
        }
        background: Rectangle {
            color: Theme.panelElevado
            border.color: Theme.borde
            radius: 4
        }
    }
}
