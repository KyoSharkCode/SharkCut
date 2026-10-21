import QtQuick
import QtQuick.Shapes
import SharkCut

// Icono vectorial de trazo (rejilla de 24 × 24, como los de la maqueta del rediseño).
// Se dibuja con Shape: escala sin perder nitidez y toma el color del tema.
Item {
    id: icono

    property string nombre
    property color color: Theme.texto
    property real tam: 16

    implicitWidth: tam
    implicitHeight: tam

    // Los que se pintan llenos (el resto, solo el trazo).
    readonly property bool relleno: nombre === "reproducir" || nombre === "pausa"
                                    || nombre === "estrellaLlena" || nombre === "keyframeLleno"

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            scale: Qt.size(icono.tam / 24, icono.tam / 24)
            strokeColor: icono.relleno ? "transparent" : icono.color
            strokeWidth: 1.7
            fillColor: icono.relleno ? icono.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Trazos.trazos[icono.nombre] || "" }
        }
    }
}
