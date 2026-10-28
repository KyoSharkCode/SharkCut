import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Selector de color con toda la gama: cuadro de saturación y brillo, barra de tono, código
// #RRGGBB y unos colores rápidos. Se abre desde una muestra de color; `elegido` llega al
// pulsar «Aplicar» (o Intro en el código), así cada elección es un solo paso de deshacer.
//
// Los colores que se eligen aquí son del video (efectos, pantalla verde, subtítulos), no
// del tema: por eso la gama y las muestras usan colores sueltos.
Popup {
    id: raiz

    property string color: "#FFFFFF" // el actual, al abrir
    property string titulo: ""       // opcional, arriba
    signal elegido(string color)

    // Lo que se está eligiendo, en HSV (0–1).
    property real tono: 0
    property real saturacion: 0
    property real brillo: 1
    readonly property color nuevo: Qt.hsva(tono, saturacion, brillo, 1)
    readonly property string hex: aHex(nuevo)

    width: 260
    padding: 10
    margins: 8 // sin salirse de la ventana
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function aHex(c) {
        const dos = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return ("#" + dos(c.r) + dos(c.g) + dos(c.b)).toUpperCase()
    }
    function desdeHex(texto) {
        const c = Qt.color(texto)
        // Sin saturación el tono no cuenta: se conserva el que había.
        if (c.hsvSaturation > 0)
            tono = Math.max(0, c.hsvHue)
        saturacion = c.hsvSaturation
        brillo = c.hsvValue
    }
    function aplicar() {
        raiz.elegido(raiz.hex)
        raiz.close()
    }

    onOpened: {
        desdeHex(/^#[0-9a-fA-F]{6}$/.test(color) ? color : "#FFFFFF")
        codigo.text = Qt.binding(() => raiz.hex)
    }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radio
        border.color: Theme.borde
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            visible: raiz.titulo.length > 0
            text: raiz.titulo
            color: Theme.texto
            font.pixelSize: Theme.tamTexto
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // Saturación (de izquierda a derecha) y brillo (de abajo arriba) del tono elegido.
            Rectangle {
                id: cuadro
                Layout.fillWidth: true
                implicitHeight: 150
                radius: 4
                color: Qt.hsva(raiz.tono, 1, 1, 1)
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#FFFFFFFF" }
                        GradientStop { position: 1; color: "#00FFFFFF" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    border.color: Theme.borde
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#00000000" }
                        GradientStop { position: 1; color: "#FF000000" }
                    }
                }
                // Marca del color elegido.
                Rectangle {
                    x: raiz.saturacion * cuadro.width - width / 2
                    y: (1 - raiz.brillo) * cuadro.height - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: "transparent"
                    border.color: raiz.brillo > 0.5 ? "#000000" : "#FFFFFF"
                    border.width: 2
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    function elegir(m) {
                        raiz.saturacion = Math.max(0, Math.min(1, m.x / width))
                        raiz.brillo = Math.max(0, Math.min(1, 1 - m.y / height))
                    }
                    onPressed: (m) => elegir(m)
                    onPositionChanged: (m) => elegir(m)
                }
            }

            // Tono.
            Rectangle {
                id: barraTono
                implicitWidth: 16
                Layout.fillHeight: true
                radius: 4
                border.color: Theme.borde
                gradient: Gradient {
                    GradientStop { position: 0 / 6; color: "#FF0000" }
                    GradientStop { position: 1 / 6; color: "#FFFF00" }
                    GradientStop { position: 2 / 6; color: "#00FF00" }
                    GradientStop { position: 3 / 6; color: "#00FFFF" }
                    GradientStop { position: 4 / 6; color: "#0000FF" }
                    GradientStop { position: 5 / 6; color: "#FF00FF" }
                    GradientStop { position: 6 / 6; color: "#FF0000" }
                }
                Rectangle {
                    x: -2
                    y: raiz.tono * barraTono.height - height / 2
                    width: barraTono.width + 4
                    height: 4
                    radius: 2
                    color: Theme.texto
                    border.color: Theme.fondo
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    function elegir(m) {
                        raiz.tono = Math.max(0, Math.min(0.9999, m.y / height))
                    }
                    onPressed: (m) => elegir(m)
                    onPositionChanged: (m) => elegir(m)
                }
            }
        }

        // Antes y después, y el código.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Row {
                Rectangle {
                    width: 22
                    height: 24
                    color: raiz.color
                    border.color: Theme.borde
                }
                Rectangle {
                    width: 22
                    height: 24
                    color: raiz.nuevo
                    border.color: Theme.borde
                }
            }
            TextField {
                id: codigo
                Layout.fillWidth: true
                implicitHeight: 26
                color: Theme.texto
                font.pixelSize: Theme.tamTextoPequeno
                font.family: "monospace"
                selectByMouse: true
                maximumLength: 7
                validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                background: Rectangle {
                    radius: Theme.radio
                    color: Theme.fondo
                    border.color: codigo.activeFocus ? Theme.acento : Theme.borde
                }
                onTextEdited: {
                    const hex = text.startsWith("#") ? text : "#" + text
                    if (/^#[0-9a-fA-F]{6}$/.test(hex))
                        raiz.desdeHex(hex)
                }
                onAccepted: raiz.aplicar()
                onEditingFinished: text = Qt.binding(() => raiz.hex)
            }
        }

        // Colores rápidos.
        Flow {
            Layout.fillWidth: true
            spacing: 5
            Repeater {
                model: ["#FFFFFF", "#BFC3CC", "#7F838C", "#000000", "#FF3B30", "#FF8A3D",
                        "#FFD800", "#4CD964", "#00FF00", "#3AA8FF", "#5856D6", "#FF2D92"]
                delegate: Rectangle {
                    required property string modelData
                    width: 16
                    height: 16
                    radius: 8
                    color: modelData
                    border.color: raiz.hex === modelData ? Theme.acento : Theme.borde
                    border.width: raiz.hex === modelData ? 2 : 1
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: raiz.desdeHex(parent.modelData)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Item { Layout.fillWidth: true }
            Boton {
                texto: "Cancelar"
                onPulsado: raiz.close()
            }
            Boton {
                texto: "Aplicar"
                acentuado: true
                onPulsado: raiz.aplicar()
            }
        }
    }
}
