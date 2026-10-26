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
                model: ["Medios", "Transiciones", "Efectos", "Subtítulos", "Sonidos"]
                delegate: Item {
                    required property string modelData
                    required property int index
                    width: etiqueta.implicitWidth + 16
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
                text: "Arrastra una transición a la línea de tiempo: se pone como un clip, "
                      + "centrada en el corte más cercano, y afecta a todo lo que hay debajo. "
                      + "Se mueve, se corta y se recorta como cualquier clip, también al principio "
                      + "o al final de un clip. Lleva su efecto de sonido en una pista de audio."
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

        // --- Sonidos: biblioteca de efectos de sonido, por grupos ---
        Flickable {
            id: vistaSonidos
            visible: panel.pestana === 4
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: columnaSonidos.implicitHeight + 24
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: BarraDesplazamiento {}
            ColumnLayout {
                id: columnaSonidos
                x: 12
                y: 12
                width: vistaSonidos.width - 24
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Pulsa ▶ para escucharlo. Arrastra un sonido a una pista de audio "
                          + "(empieza donde lo sueltas) o pulsa + para ponerlo con su golpe en "
                          + "el cabezal."
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                }
                Repeater {
                    model: panel.editor.soundLibrary
                    delegate: ColumnLayout {
                        id: filaSonido
                        required property var modelData
                        required property int index
                        readonly property bool primeroDelGrupo: index === 0
                            || panel.editor.soundLibrary[index - 1].category !== modelData.category
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            visible: filaSonido.primeroDelGrupo
                            Layout.topMargin: filaSonido.index === 0 ? 4 : 10
                            text: filaSonido.modelData.category.toUpperCase()
                            color: Theme.textoDesactivado
                            font.pixelSize: Theme.tamTextoPequeno
                            font.letterSpacing: 1
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 36
                            radius: Theme.radio
                            color: arrastreSonido.containsMouse ? Theme.panelElevado : "transparent"
                            border.color: arrastreSonido.containsMouse ? Theme.acento : Theme.borde
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 8
                                // Escuchar: un clic lo hace sonar y otro lo para.
                                BotonIcono {
                                    readonly property bool sonando:
                                        panel.editor.previewSound === filaSonido.modelData.id
                                    icono: sonando ? "detener" : "reproducir"
                                    activo: sonando
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    ayuda: sonando ? "Parar" : "Escuchar"
                                    onPulsado: panel.editor.toggleSoundPreview(filaSonido.modelData.id)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: filaSonido.modelData.name
                                    color: Theme.texto
                                    font.pixelSize: Theme.tamTexto
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: filaSonido.modelData.seconds.toFixed(2).replace(".", ",") + " s"
                                    color: Theme.textoDesactivado
                                    font.pixelSize: Theme.tamTextoPequeno
                                }
                                BotonIcono {
                                    icono: "mas"
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    enabled: panel.editor.hasSequence
                                    ayuda: "En el cabezal (con su golpe ahí)"
                                    onPulsado: panel.editor.addSoundAtPlayhead(filaSonido.modelData.id)
                                }
                            }
                            MouseArea {
                                id: arrastreSonido
                                anchors.fill: parent
                                anchors.leftMargin: 40 // el botón de escuchar
                                anchors.rightMargin: 36
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
                                    f.efecto = ""
                                    f.sonido = filaSonido.modelData.id
                                    f.nombre = filaSonido.modelData.name
                                    f.Drag.keys = ["sharkcut/sonido"]
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
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    wrapMode: Text.WordWrap
                    text: "Sonidos propios de SharkCut (sintetizados, CC0). Entran a −6 dB; el "
                          + "volumen se cambia en Propiedades."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }
            }
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

        // --- Efectos (con los ajustes guardados debajo; se desplaza si no cabe) ---
        Flickable {
            id: vistaEfectos
            visible: panel.pestana === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: columnaEfectos.implicitHeight + 24
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: BarraDesplazamiento {}
            ColumnLayout {
                id: columnaEfectos
                x: 12
                y: 12
                width: vistaEfectos.width - 24
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
                    delegate: FilaEfecto {
                        required property var modelData
                        clave: modelData.clave
                        nombre: modelData.nombre
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Arrastra un efecto sobre un clip de video de la línea de tiempo (o "
                          + "selecciona el clip y pulsa Aplicar). Con varios clips seleccionados "
                          + "(arrastra en una zona vacía de la línea de tiempo para marcarlos), "
                          + "va a todos. Después se ajusta o se quita en "
                          + "Propiedades. Croma busca solo el color del fondo; la sombra y el "
                          + "resplandor siguen la forma del clip (también su transparencia)."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }

                // Ajustes guardados (presets): se arrastran igual que los efectos.
                Text {
                    Layout.topMargin: 6
                    text: "AJUSTES GUARDADOS"
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                    font.letterSpacing: 1
                }
                Text {
                    visible: panel.editor.clipPresets.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Todavía no hay ninguno. Selecciona un clip de video y pulsa «Guardar como "
                          + "ajuste…» en Propiedades: su colocación, efectos y animación quedan aquí "
                          + "para arrastrarlos a otros clips."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }
                Repeater {
                    model: panel.editor.clipPresets
                    delegate: FilaEfecto {
                        id: filaAjuste
                        required property var modelData
                        clave: "ajuste:" + modelData.name
                        nombre: modelData.name
                        detalle: modelData.summary
                        // Borrar pide confirmación: el primer clic arma el botón.
                        property bool confirmando: false
                        BotonIcono {
                            icono: "lapiz"
                            implicitWidth: 26
                            implicitHeight: 26
                            ayuda: "Cambiar el nombre"
                            onPulsado: dialogoNombreAjuste.pedir("Cambiar el nombre del ajuste",
                                filaAjuste.modelData.name, "Cambiar",
                                (nuevo) => panel.editor.renameClipPreset(filaAjuste.modelData.name, nuevo))
                        }
                        BotonIcono {
                            icono: filaAjuste.confirmando ? "" : "quitar"
                            texto: filaAjuste.confirmando ? "¿Borrar?" : ""
                            acentuado: filaAjuste.confirmando
                            implicitWidth: filaAjuste.confirmando ? 74 : 26
                            implicitHeight: 26
                            ayuda: "Borrar el ajuste"
                            onPulsado: {
                                if (filaAjuste.confirmando)
                                    panel.editor.removeClipPreset(filaAjuste.modelData.name)
                                else {
                                    filaAjuste.confirmando = true
                                    desarmar.restart()
                                }
                            }
                            Timer {
                                id: desarmar
                                interval: 3000
                                onTriggered: filaAjuste.confirmando = false
                            }
                        }
                    }
                }
            }
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

    DialogoNombre { id: dialogoNombreAjuste }

    // Un efecto (o un ajuste guardado) de la pestaña Efectos: se arrastra sobre un clip de
    // video o se aplica al seleccionado. Los hijos van como botones a la derecha.
    component FilaEfecto: Rectangle {
        id: fila
        property string clave   // lo que recibe applyEffect
        property string nombre
        property string detalle // segunda línea (opcional)
        default property alias botones: extra.data
        Layout.fillWidth: true
        implicitHeight: detalle.length > 0 ? 48 : 40
        radius: Theme.radio
        color: arrastre.containsMouse ? Theme.panelElevado : "transparent"
        border.color: arrastre.containsMouse ? Theme.acento : Theme.borde
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 6
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: fila.nombre
                    elide: Text.ElideRight
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                }
                Text {
                    visible: fila.detalle.length > 0
                    Layout.fillWidth: true
                    text: fila.detalle
                    elide: Text.ElideRight
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }
            }
            RowLayout {
                id: botonesFila
                spacing: 4
                RowLayout {
                    id: extra
                    spacing: 4
                }
                BotonIcono {
                    // El clip de video seleccionado; con varios, va a todos los de video.
                    readonly property var sel: panel.editor.selectedClip
                    readonly property var objetivo: sel.count === 1 ? sel.video : (sel.firstVideo || 0)
                    texto: "Aplicar"
                    implicitHeight: 26
                    enabled: objetivo !== undefined && objetivo !== 0
                    ayuda: sel.count > 1 ? "A los " + sel.videoCount + " clips de video seleccionados"
                                         : "Al clip de video seleccionado"
                    onPulsado: panel.editor.applyEffect(objetivo, fila.clave)
                }
            }
        }
        MouseArea {
            id: arrastre
            // Deja libres los botones.
            anchors.fill: parent
            anchors.rightMargin: botonesFila.width + 12
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
                f.efecto = fila.clave
                f.nombre = fila.nombre
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
