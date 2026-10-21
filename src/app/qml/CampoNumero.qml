import QtQuick
import QtQuick.Layouts
import SharkCut

// Valor numérico editable, como en Premiere: arrastra a izquierda o derecha sobre el
// número para cambiarlo (se ve en directo; con Mayús va 10 veces más rápido y con Ctrl,
// 10 veces más fino) o haz clic para escribirlo. `cambiando` llega durante el arrastre
// y `cambiado` al soltar o al confirmar lo escrito (un solo paso de deshacer).
RowLayout {
    id: raiz

    property string etiqueta
    property int anchoEtiqueta: 86
    property real valor
    property real desde: -1e9
    property real hasta: 1e9
    property real paso: 1 // cambio por píxel arrastrado
    property int decimales: 0
    property string unidad
    property bool activo: true
    signal cambiando(real valor)
    signal cambiado(real valor)

    Layout.fillWidth: true
    spacing: 8
    opacity: activo ? 1 : 0.45

    function limitar(v) {
        return Math.max(desde, Math.min(hasta, v))
    }

    Text {
        Layout.preferredWidth: raiz.anchoEtiqueta
        text: raiz.etiqueta
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTexto
        elide: Text.ElideRight
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 26
        radius: 4
        color: Theme.fondo
        border.color: entrada.visible ? Theme.acento
                      : zona.containsMouse || zona.pressed ? Theme.borde : "transparent"

        Text {
            visible: !entrada.visible
            anchors.verticalCenter: parent.verticalCenter
            x: 8
            text: raiz.valor.toFixed(raiz.decimales) + (raiz.unidad.length > 0 ? " " + raiz.unidad : "")
            color: raiz.activo ? Theme.acento : Theme.textoSecundario
            font.pixelSize: Theme.tamTexto
            font.family: "Consolas, monospace"
        }

        TextInput {
            id: entrada
            visible: false
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.texto
            selectionColor: Theme.acentoSecundario
            selectedTextColor: Theme.texto
            font.pixelSize: Theme.tamTexto
            font.family: "Consolas, monospace"
            selectByMouse: true
            function confirmar() {
                const v = parseFloat(text.replace(",", "."))
                visible = false
                if (!isNaN(v))
                    raiz.cambiado(raiz.limitar(v))
            }
            onAccepted: confirmar()
            onActiveFocusChanged: {
                if (!activeFocus && visible)
                    confirmar()
            }
            Keys.onEscapePressed: visible = false
        }

        MouseArea {
            id: zona
            anchors.fill: parent
            hoverEnabled: true
            enabled: raiz.activo && !entrada.visible
            cursorShape: Qt.SizeHorCursor
            preventStealing: true
            property real xInicial: 0
            property real valorInicial: 0
            property real provisional: 0
            property bool movido: false

            onPressed: (m) => {
                xInicial = m.x
                valorInicial = raiz.valor
                provisional = raiz.valor
                movido = false
            }
            onPositionChanged: (m) => {
                if (!pressed)
                    return
                const dx = m.x - xInicial
                if (!movido && Math.abs(dx) < 3)
                    return
                movido = true
                const factor = (m.modifiers & Qt.ShiftModifier) ? 10
                             : (m.modifiers & Qt.ControlModifier) ? 0.1 : 1
                provisional = raiz.limitar(valorInicial + dx * raiz.paso * factor)
                raiz.cambiando(provisional)
            }
            onReleased: {
                if (movido) {
                    raiz.cambiado(provisional)
                } else {
                    entrada.text = raiz.valor.toFixed(raiz.decimales)
                    entrada.visible = true
                    entrada.forceActiveFocus()
                    entrada.selectAll()
                }
            }
        }
    }
}
