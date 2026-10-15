import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Panel izquierdo del editor, con pestañas: Medios (biblioteca), Transiciones y Efectos.
// Las propiedades de lo seleccionado tienen su propio panel (PanelPropiedades).
Rectangle {
    id: panel

    required property Editor editor
    required property Item fantasma          // lo que se ve al arrastrar un medio
    property int pestana: 0
    signal importar()
    signal nuevaSecuencia()
    signal cerrarProyecto(var uid)
    // Carpeta abierta en Medios: lo que se importa va ahí.
    readonly property var carpeta: biblioteca.carpeta

    color: Theme.panel
    radius: Theme.radio
    border.color: zonaArchivos.containsDrag ? Theme.acento : Theme.borde

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Pestañas del panel
        Row {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.topMargin: 4
            spacing: 2
            Repeater {
                model: ["Medios", "Transiciones", "Efectos"]
                delegate: Item {
                    required property string modelData
                    required property int index
                    width: etiqueta.implicitWidth + 28
                    height: 36
                    Text {
                        id: etiqueta
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: panel.pestana === parent.index ? Theme.texto : Theme.textoSecundario
                        font.pixelSize: Theme.tamTexto
                        font.bold: panel.pestana === parent.index
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: panel.pestana === parent.index ? Theme.acento : "transparent"
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: panel.pestana = parent.index
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borde }

        // --- Medios ---
        PanelBiblioteca {
            id: biblioteca
            visible: panel.pestana === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            editor: panel.editor
            fantasma: panel.fantasma
            onImportar: panel.importar()
            onNuevaSecuencia: panel.nuevaSecuencia()
            onCerrarProyecto: (uid) => panel.cerrarProyecto(uid)
        }

        // --- Transiciones ---
        ColumnLayout {
            visible: panel.pestana === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 10
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Arrastra una transición al corte entre dos clips seguidos de una pista "
                      + "de video. Lleva su efecto de sonido, que puedes cambiar o quitar en "
                      + "Propiedades."
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: panel.editor.transitionNames
                    delegate: Rectangle {
                        id: tarjeta
                        required property string modelData
                        required property int index
                        readonly property string sonido: panel.editor.transitionSounds[index] || ""
                        Layout.fillWidth: true
                        implicitHeight: 54
                        radius: Theme.radio
                        color: arrastreTransicion.containsMouse ? Theme.borde : Theme.panelElevado
                        border.color: arrastreTransicion.containsMouse ? Theme.clipTransicion
                                                                        : Theme.borde
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                implicitWidth: 32
                                implicitHeight: 32
                                radius: 5
                                color: Theme.clipTransicion
                                Icono {
                                    anchors.centerIn: parent
                                    nombre: "transicion"
                                    tam: 18
                                    color: Theme.texto
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: tarjeta.modelData
                                    color: Theme.texto
                                    font.pixelSize: Theme.tamTexto
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: tarjeta.sonido.length > 0 ? "♪ " + tarjeta.sonido
                                                                    : "Sin sonido"
                                    color: Theme.textoSecundario
                                    font.pixelSize: Theme.tamTextoPequeno
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        ArrastreTransicion {
                            id: arrastreTransicion
                            anchors.fill: parent
                            indice: tarjeta.index
                            nombre: tarjeta.modelData
                            fantasma: panel.fantasma
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Para probar los ajustes con calma sigue estando la pestaña Transición."
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
            }
            Item { Layout.fillHeight: true }
        }

        // --- Efectos ---
        ColumnLayout {
            visible: panel.pestana === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 8
            Repeater {
                model: ["Movimiento (keyframes y ajustes guardados)", "Blanco y negro · desenfoque",
                        "Esquinas redondeadas · sombra", "Croma (pantalla verde)"]
                delegate: Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 42
                    radius: Theme.radio
                    color: "transparent"
                    border.color: Theme.borde
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        Text {
                            Layout.fillWidth: true
                            text: parent.parent.modelData
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTexto
                        }
                        Chip { texto: "ETAPA 5"; colorTexto: Theme.textoDesactivado }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

    }

    // Archivos arrastrados desde el explorador: a la biblioteca.
    DropArea {
        id: zonaArchivos
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (soltado) => {
            if (soltado.hasUrls) {
                panel.editor.importFiles(soltado.urls, -1, -1, panel.carpeta)
                soltado.acceptProposedAction()
            }
        }
    }
}
