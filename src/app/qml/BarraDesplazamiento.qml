import QtQuick
import QtQuick.Controls.Basic
import SharkCut

// Barra de desplazamiento con el tema: siempre visible mientras haya algo que desplazar.
ScrollBar {
    id: barra
    readonly property bool hayQueDesplazar: size < 1
    policy: hayQueDesplazar ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    padding: 2
    minimumSize: 0.05
    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: barra.pressed ? Theme.acento
               : barra.hovered ? Theme.textoSecundario : Theme.textoDesactivado
        opacity: barra.pressed || barra.hovered ? 0.9 : 0.6
    }
    background: Rectangle {
        radius: 5
        color: Theme.fondo
        opacity: barra.hayQueDesplazar ? 1 : 0
    }
}
