import QtQuick

// Zona de un medio de la biblioteca: arrastrarlo a la línea de tiempo (con el
// `fantasma` que sigue al ratón) o hacer doble clic para abrirlo en Origen.
MouseArea {
    id: zona

    required property var medio
    required property Item fantasma
    signal abrir(string ruta)

    hoverEnabled: true
    drag.target: fantasma
    drag.threshold: 6

    onPressed: (m) => {
        const p = mapToItem(fantasma.parent, m.x, m.y)
        fantasma.x = p.x - 10
        fantasma.y = p.y - 14
        fantasma.mediaId = medio.id
        fantasma.transicion = -1
        fantasma.efecto = ""
        fantasma.nombre = medio.name
        fantasma.Drag.keys = ["sharkcut/medio"]
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
    onDoubleClicked: zona.abrir(medio.path)
}
