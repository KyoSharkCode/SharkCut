import QtQuick
import QtQuick.Controls.Basic
import SharkCut

// Menú contextual con los colores del tema.
Menu {
    id: menu
    padding: 4
    background: Rectangle {
        implicitWidth: 300
        color: Theme.panelElevado
        border.color: Theme.borde
        radius: 4
    }
    delegate: MenuItem {
        id: opcion
        implicitHeight: 28
        contentItem: Text {
            leftPadding: 6
            text: opcion.text
            color: opcion.enabled ? Theme.texto : Theme.textoDesactivado
            font.pixelSize: Theme.tamTexto
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 3
            color: opcion.highlighted ? Theme.borde : "transparent"
        }
    }
}
