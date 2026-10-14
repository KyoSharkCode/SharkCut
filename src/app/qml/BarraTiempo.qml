import QtQuick
import SharkCut

// Barra para ver y saltar a cualquier punto. Opcionalmente muestra un tramo marcado
// (entrada/salida) o los segmentos A · transición · B de la secuencia de prueba.
Item {
    id: barra

    property real actual: 0
    property real total: 1
    property real entrada: -1 // marca de entrada (-1 = ninguna)
    property real salida: -1  // marca de salida (-1 = ninguna)
    property bool segmentos: false
    property real inicioTransicion: 0
    property real finTransicion: 0
    signal buscar(real fotograma)

    implicitHeight: segmentos ? 30 : 22

    function posicion(fotograma) {
        return total > 1 ? fotograma / (total - 1) * width : 0
    }

    // Pista
    Rectangle {
        id: pista
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: barra.segmentos ? 18 : 6
        radius: 3
        color: Theme.panelElevado
        clip: true

        // Secuencia: A, B y la zona de transición encima.
        Rectangle {
            visible: barra.segmentos
            width: barra.posicion(barra.finTransicion)
            height: parent.height / 2
            color: Theme.clipVideo
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: "A"
                color: Theme.texto
                font.pixelSize: 9
                font.bold: true
            }
        }
        Rectangle {
            visible: barra.segmentos
            x: barra.posicion(barra.inicioTransicion)
            y: parent.height / 2
            width: parent.width - x
            height: parent.height / 2
            color: Theme.clipAudio
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: "B"
                color: Theme.texto
                font.pixelSize: 9
                font.bold: true
            }
        }
        Rectangle {
            visible: barra.segmentos
            x: barra.posicion(barra.inicioTransicion)
            width: Math.max(2, barra.posicion(barra.finTransicion) - x)
            height: parent.height
            color: Theme.clipTransicion
            opacity: 0.85
        }

        // Tramo marcado
        Rectangle {
            visible: !barra.segmentos && (barra.entrada >= 0 || barra.salida >= 0)
            x: barra.posicion(barra.entrada >= 0 ? barra.entrada : 0)
            width: barra.posicion(barra.salida >= 0 ? barra.salida : barra.total - 1) - x
            height: parent.height
            color: Theme.acento
            opacity: 0.35
        }
        // Progreso (solo sin segmentos, para no tapar los colores)
        Rectangle {
            visible: !barra.segmentos
            width: barra.posicion(barra.actual)
            height: parent.height
            color: Theme.acento
            opacity: 0.5
        }
    }

    // Marcas de entrada y salida
    Rectangle {
        visible: !barra.segmentos && barra.entrada >= 0
        x: barra.posicion(barra.entrada) - 1
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: 18
        color: Theme.aviso
    }
    Rectangle {
        visible: !barra.segmentos && barra.salida >= 0
        x: barra.posicion(barra.salida) - 1
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: 18
        color: Theme.aviso
    }

    // Cabezal
    Rectangle {
        x: barra.posicion(barra.actual) - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 4
        height: barra.segmentos ? 28 : 18
        radius: 2
        color: Theme.cabezal
    }

    MouseArea {
        anchors.fill: parent
        function saltar(mouseX) {
            const fraccion = Math.max(0, Math.min(1, mouseX / width))
            barra.buscar(Math.round(fraccion * (barra.total - 1)))
        }
        onPressed: (mouse) => saltar(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) saltar(mouse.x) }
    }
}
