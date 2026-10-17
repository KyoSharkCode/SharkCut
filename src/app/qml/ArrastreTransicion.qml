import QtQuick

// Zona de una transición del catálogo: arrastrarla al corte entre dos clips de una pista
// de video (con el `fantasma` que sigue al ratón).
MouseArea {
    id: zona

    required property int indice
    required property string nombre
    required property Item fantasma

    hoverEnabled: true
    drag.target: fantasma
    drag.threshold: 6
    cursorShape: Qt.OpenHandCursor

    onPressed: (m) => {
        const p = mapToItem(fantasma.parent, m.x, m.y)
        fantasma.x = p.x - 10
        fantasma.y = p.y - 14
        fantasma.mediaId = undefined
        fantasma.transicion = indice
        fantasma.efecto = ""
        fantasma.nombre = nombre
        fantasma.Drag.keys = ["sharkcut/transicion"]
    }
    onPositionChanged: {
        if (drag.active && !fantasma.Drag.active)
            fantasma.Drag.active = true
    }
    onReleased: {
        if (fantasma.Drag.active) {
            fantasma.Drag.drop()
            fantasma.Drag.active = false
        }
    }
}
