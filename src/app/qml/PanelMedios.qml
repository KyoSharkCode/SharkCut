import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Panel izquierdo del editor, con pestañas: Medios (biblioteca), Transiciones, Efectos y
// Subtítulos.
// Las propiedades de lo seleccionado tienen su propio panel (PanelPropiedades).
Rectangle {
    id: panel

    required property Editor editor
    required property Item fantasma          // lo que se ve al arrastrar un medio
    property int pestana: 0
    signal importar()
    signal crearSubtitulos()
    signal importarSrt()
    signal exportarSrt(int pista)
    signal dividirSubtitulos(int pista)
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
                model: ["Medios", "Transiciones", "Efectos", "Subtítulos"]
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

        // --- Subtítulos ---
        ColumnLayout {
            visible: panel.pestana === 3
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 8

            BotonIcono {
                Layout.fillWidth: true
                implicitHeight: 36
                acentuado: true
                texto: "Crear subtítulos a partir de la transcripción…"
                enabled: panel.editor.hasSequence && !panel.editor.transcribing
                ayuda: "Escucha el audio de la secuencia y crea una pista de subtítulos"
                onPulsado: panel.crearSubtitulos()
            }
            // Transcripción en curso
            ColumnLayout {
                visible: panel.editor.transcribing
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: panel.editor.transcribeMessage
                    color: Theme.texto
                    font.pixelSize: Theme.tamTextoPequeno
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 6
                        radius: 3
                        color: Theme.fondo
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, panel.editor.transcribeProgress))
                            height: parent.height
                            radius: 3
                            color: Theme.acento
                        }
                    }
                    BotonIcono {
                        texto: "Cancelar"
                        implicitHeight: 24
                        onPulsado: panel.editor.cancelTranscription()
                    }
                }
            }
            BotonIcono {
                Layout.fillWidth: true
                icono: "mas"
                texto: "Añadir un subtítulo o texto en el cabezal"
                enabled: panel.editor.hasSequence
                ayuda: "También: doble clic en una pista de subtítulos"
                onPulsado: panel.editor.addCaptionAtPlayhead(-1)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                BotonIcono {
                    Layout.fillWidth: true
                    texto: "Nueva pista"
                    enabled: panel.editor.hasSequence
                    onPulsado: panel.editor.addCaptionTrack()
                }
                BotonIcono {
                    Layout.fillWidth: true
                    icono: "importar"
                    texto: "Importar .srt…"
                    enabled: panel.editor.hasSequence
                    onPulsado: panel.importarSrt()
                }
            }

            Text {
                Layout.topMargin: 6
                visible: panel.editor.captionTracks.length > 0
                text: "PISTAS DE SUBTÍTULOS"
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
                font.letterSpacing: 0.6
            }
            Repeater {
                model: panel.editor.captionTracks
                delegate: Rectangle {
                    id: filaSub
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: Theme.radio
                    color: "transparent"
                    border.color: Theme.borde
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 6
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            text: filaSub.modelData.name + " · " + filaSub.modelData.count
                                  + (filaSub.modelData.count === 1 ? " subtítulo" : " subtítulos")
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            elide: Text.ElideRight
                        }
                        BotonIcono {
                            texto: "Dividir…"
                            implicitHeight: 26
                            enabled: filaSub.modelData.count > 0 && !filaSub.modelData.locked
                            ayuda: "Volver a dividir con otros caracteres por línea, líneas…"
                            onPulsado: panel.dividirSubtitulos(filaSub.modelData.index)
                        }
                        BotonIcono {
                            icono: "exportar"
                            implicitHeight: 26
                            enabled: filaSub.modelData.count > 0
                            ayuda: "Exportar como .srt"
                            onPulsado: panel.exportarSrt(filaSub.modelData.index)
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Selecciona un subtítulo en la línea de tiempo para cambiar su texto y su "
                      + "estilo en Propiedades (a él solo o a toda la pista), o aplicar un ajuste "
                      + "guardado."
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
            // Cada efecto se arrastra a un clip de video o se aplica al seleccionado.
            Repeater {
                model: [
                    { clave: "croma", nombre: "Croma (pantalla verde)" },
                    { clave: "byn", nombre: "Blanco y negro" },
                    { clave: "desenfoque", nombre: "Desenfoque" },
                    { clave: "esquinas", nombre: "Esquinas redondeadas" },
                    { clave: "sombra", nombre: "Sombra" },
                    { clave: "resplandor", nombre: "Resplandor" }
                ]
                delegate: Rectangle {
                    id: efecto
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: Theme.radio
                    color: arrastreEfecto.containsMouse ? Theme.panelElevado : "transparent"
                    border.color: arrastreEfecto.containsMouse ? Theme.acento : Theme.borde
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        Text {
                            Layout.fillWidth: true
                            text: efecto.modelData.nombre
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                        }
                        BotonIcono {
                            texto: "Aplicar"
                            implicitHeight: 26
                            enabled: panel.editor.selectedClip.count === 1
                                     && panel.editor.selectedClip.video !== 0
                            ayuda: "Al clip de video seleccionado"
                            onPulsado: panel.editor.applyEffect(panel.editor.selectedClip.video,
                                                                efecto.modelData.clave)
                        }
                    }
                    MouseArea {
                        id: arrastreEfecto
                        // Deja libre el botón Aplicar.
                        anchors.fill: parent
                        anchors.rightMargin: 80
                        hoverEnabled: true
                        drag.target: panel.fantasma
                        drag.threshold: 6
                        cursorShape: Qt.OpenHandCursor
                        onPressed: (m) => {
                            const f = panel.fantasma
                            const p = mapToItem(f.parent, m.x, m.y)
                            f.x = p.x - 10
                            f.y = p.y - 14
                            f.mediaId = undefined
                            f.transicion = -1
                            f.efecto = efecto.modelData.clave
                            f.nombre = efecto.modelData.nombre
                            f.Drag.keys = ["sharkcut/efecto"]
                        }
                        onPositionChanged: {
                            if (drag.active && !panel.fantasma.Drag.active)
                                panel.fantasma.Drag.active = true
                        }
                        onReleased: {
                            if (panel.fantasma.Drag.active) {
                                panel.fantasma.Drag.drop()
                                panel.fantasma.Drag.active = false
                            }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Arrastra un efecto sobre un clip de video de la línea de tiempo (o "
                      + "selecciona el clip y pulsa Aplicar). Después se ajusta o se quita en "
                      + "Propiedades. Croma busca solo el color del fondo; la sombra y el "
                      + "resplandor siguen la forma del clip (también su transparencia)."
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 40
                radius: Theme.radio
                color: "transparent"
                border.color: Theme.borde
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    Text {
                        Layout.fillWidth: true
                        text: "Ajustes guardados (presets)"
                        color: Theme.textoSecundario
                        font.pixelSize: Theme.tamTexto
                    }
                    Chip { texto: "PRÓXIMO"; colorTexto: Theme.textoDesactivado }
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
