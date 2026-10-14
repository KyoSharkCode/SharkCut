import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Panel izquierdo del editor, con pestañas: Medios (biblioteca), Transiciones,
// Efectos y Propiedades del clip seleccionado. Cada cosa tiene ya su sitio, aunque
// algunas lleguen en etapas posteriores.
Rectangle {
    id: panel

    required property Editor editor
    required property Item fantasma          // lo que se ve al arrastrar un medio
    property var transiciones: []            // nombres del catálogo de transiciones
    property int pestana: 0
    property bool cuadricula: false
    signal importar()
    signal abrirEnOrigen(string ruta)

    color: Theme.panel
    radius: Theme.radio
    border.color: zonaArchivos.containsDrag ? Theme.acento : Theme.borde

    readonly property var mediosFiltrados: {
        const texto = buscador.text.trim().toLowerCase()
        const todos = editor.mediaItems
        return texto.length === 0 ? todos
                                  : todos.filter((m) => m.name.toLowerCase().indexOf(texto) >= 0)
    }

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
                model: ["Medios", "Transiciones", "Efectos", "Propiedades"]
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
        ColumnLayout {
            visible: panel.pestana === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    radius: Theme.radio
                    color: Theme.fondo
                    border.color: buscador.activeFocus ? Theme.acento : Theme.borde
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        spacing: 6
                        Icono { nombre: "buscar"; tam: 14; color: Theme.textoDesactivado }
                        TextField {
                            id: buscador
                            Layout.fillWidth: true
                            placeholderText: "Buscar medios"
                            placeholderTextColor: Theme.textoDesactivado
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            background: null
                            padding: 0
                        }
                    }
                }
                BotonIcono {
                    icono: "lista"
                    ayuda: "Ver como lista"
                    activo: !panel.cuadricula
                    onPulsado: panel.cuadricula = false
                }
                BotonIcono {
                    icono: "cuadricula"
                    ayuda: "Ver como cuadrícula"
                    activo: panel.cuadricula
                    onPulsado: panel.cuadricula = true
                }
                BotonIcono {
                    icono: "importar"
                    texto: "Importar…"
                    ayuda: "Importar medios (Ctrl+I)"
                    acentuado: true
                    onPulsado: panel.importar()
                }
            }

            // Lista
            ListView {
                id: lista
                visible: !panel.cuadricula
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: panel.mediosFiltrados
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    width: ListView.view.width
                    height: 42
                    radius: Theme.radio
                    color: arrastreFila.containsMouse ? Theme.panelElevado : Theme.fondo
                    border.color: Theme.borde

                    ArrastreMedio {
                        id: arrastreFila
                        anchors.fill: parent
                        medio: fila.modelData
                        fantasma: panel.fantasma
                        onAbrir: (ruta) => panel.abrirEnOrigen(ruta)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8
                        Icono {
                            nombre: fila.modelData.video ? "video" : "audio"
                            tam: 15
                            color: fila.modelData.video ? Theme.clipVideoClaro : Theme.clipAudioClaro
                        }
                        Text {
                            Layout.fillWidth: true
                            text: fila.modelData.name
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: fila.modelData.duration
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            font.family: "Consolas, monospace"
                        }
                        Text {
                            Layout.preferredWidth: 118
                            text: fila.modelData.details
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            elide: Text.ElideRight
                        }
                        BotonIcono {
                            icono: "mas"
                            ayuda: "Añadir al final de la línea de tiempo"
                            implicitWidth: 28
                            implicitHeight: 26
                            onPulsado: panel.editor.addMedia(fila.modelData.id, -1)
                        }
                    }
                }
            }

            // Cuadrícula con miniaturas
            GridView {
                id: rejilla
                visible: panel.cuadricula
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: Math.floor(width / 3)
                cellHeight: cellWidth * 9 / 16 + 30
                model: panel.mediosFiltrados
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: celda
                    required property var modelData
                    width: rejilla.cellWidth
                    height: rejilla.cellHeight

                    Rectangle {
                        id: marco
                        x: 3
                        width: parent.width - 6
                        height: width * 9 / 16
                        radius: Theme.radio
                        color: Theme.fondo
                        border.color: arrastreCelda.containsMouse ? Theme.acento : Theme.borde
                        clip: true

                        Icono {
                            anchors.centerIn: parent
                            visible: miniatura.status !== Image.Ready
                            nombre: celda.modelData.video ? "video" : "audio"
                            tam: 26
                            color: Theme.textoDesactivado
                        }
                        Image {
                            id: miniatura
                            anchors.fill: parent
                            anchors.margins: 1
                            source: celda.modelData.thumbnail
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 320
                        }
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 5
                            width: duracion.implicitWidth + 10
                            height: duracion.implicitHeight + 2
                            radius: 3
                            color: Theme.visorFondo
                            Text {
                                id: duracion
                                anchors.centerIn: parent
                                text: celda.modelData.duration
                                color: Theme.texto
                                font.pixelSize: 10
                                font.family: "Consolas, monospace"
                            }
                        }
                        ArrastreMedio {
                            id: arrastreCelda
                            anchors.fill: parent
                            medio: celda.modelData
                            fantasma: panel.fantasma
                            onAbrir: (ruta) => panel.abrirEnOrigen(ruta)
                        }
                        BotonIcono {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 4
                            visible: arrastreCelda.containsMouse || hovered
                            property bool hovered: false
                            icono: "mas"
                            ayuda: "Añadir al final de la línea de tiempo"
                            implicitWidth: 26
                            implicitHeight: 24
                            color: Theme.panelElevado
                            onPulsado: panel.editor.addMedia(celda.modelData.id, -1)
                            HoverHandler { onHoveredChanged: parent.hovered = hovered }
                        }
                    }
                    Text {
                        anchors.top: marco.bottom
                        anchors.topMargin: 5
                        x: 4
                        width: parent.width - 8
                        text: celda.modelData.name
                        color: Theme.textoSecundario
                        font.pixelSize: Theme.tamTextoPequeno
                        elide: Text.ElideMiddle
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: panel.editor.mediaItems.length === 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: "Pulsa «Importar…» o arrastra videos aquí."
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTexto
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Arrastra un medio a la línea de tiempo · doble clic para abrirlo en Origen"
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
            }
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
                text: "Pronto podrás arrastrarlas entre dos clips de la línea de tiempo "
                      + "(fase 3, etapa 3). Mientras tanto, pruébalas en la pestaña Transición."
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: panel.transiciones
                    delegate: Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        implicitHeight: 50
                        radius: Theme.radio
                        color: Theme.panelElevado
                        border.color: Theme.borde
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
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
                            Text {
                                Layout.fillWidth: true
                                text: parent.parent.modelData
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
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
                model: ["Croma (pantalla verde)", "Zoom y movimiento (keyframes)",
                        "Subtítulos automáticos", "Corrección de color (LUT)"]
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
                        Chip { texto: "FASE 5"; colorTexto: Theme.textoDesactivado }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        // --- Propiedades del clip seleccionado ---
        ColumnLayout {
            visible: panel.pestana === 3
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            spacing: 12
            Text {
                text: "CLIP SELECCIONADO"
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
                font.letterSpacing: 0.6
            }
            Text {
                Layout.fillWidth: true
                text: panel.editor.selectionDescription.length > 0
                      ? panel.editor.selectionDescription : "Ningún clip seleccionado"
                color: panel.editor.selectionDescription.length > 0 ? Theme.texto
                                                                    : Theme.textoDesactivado
                font.pixelSize: Theme.tamTexto
                wrapMode: Text.WrapAnywhere
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borde }
            Repeater {
                model: ["Posición", "Escala", "Rotación", "Opacidad"]
                delegate: RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    opacity: 0.45
                    spacing: 10
                    Text {
                        Layout.preferredWidth: 80
                        text: parent.modelData
                        color: Theme.textoSecundario
                        font.pixelSize: Theme.tamTexto
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 4
                        radius: 2
                        color: Theme.panelElevado
                    }
                    Icono { nombre: "keyframe"; tam: 14; color: Theme.textoDesactivado }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Posición y escala llegan en la etapa 2 (para reencuadrar en vertical y "
                      + "colocar la facecam en V2); los keyframes, con los efectos."
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
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
                panel.editor.importFiles(soltado.urls)
                soltado.acceptProposedAction()
            }
        }
    }
}
