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
        Flickable {
            id: propiedades
            visible: panel.pestana === 3
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: columnaPropiedades.implicitHeight + 28
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            readonly property var tr: panel.editor.selectedTransition
            readonly property bool hayTransicion: tr.id !== undefined
            readonly property var sel: panel.editor.selectedClip
            readonly property bool uno: sel.count === 1
            readonly property bool hayVideo: uno && sel.video !== 0
            readonly property bool hayAudio: uno && sel.audio !== 0
            readonly property var t: hayVideo ? sel.transform : ({})
            readonly property bool videoEditable: hayVideo && !sel.videoLocked

            function vista(valores) { panel.editor.previewTransform(sel.video, valores) }
            function aplicar(valores, etiqueta) { panel.editor.setClipTransform(sel.video, valores, etiqueta) }

            ColumnLayout {
                id: columnaPropiedades
                x: 14
                y: 14
                width: propiedades.width - 28
                spacing: 10

                // --- Transición seleccionada ---
                ColumnLayout {
                    id: propiedadesTransicion
                    visible: propiedades.hayTransicion
                    enabled: propiedades.hayTransicion && !propiedades.tr.locked
                    Layout.fillWidth: true
                    spacing: 10

                    readonly property real fps: Math.max(1, panel.editor.fps)
                    function segundos(fotogramas) {
                        return (fotogramas / fps).toFixed(2).replace(".", ",") + " s"
                    }

                    Text {
                        text: "TRANSICIÓN SELECCIONADA"
                        color: Theme.textoDesactivado
                        font.pixelSize: Theme.tamTextoPequeno
                        font.letterSpacing: 0.6
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            implicitWidth: 22
                            implicitHeight: 22
                            radius: 4
                            color: Theme.clipTransicion
                            Icono { anchors.centerIn: parent; nombre: "transicion"; tam: 14; color: Theme.texto }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (propiedades.tr.name || "") + " · " + (propiedades.tr.track || "")
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Chip {
                            visible: propiedades.hayTransicion && propiedades.tr.locked
                            texto: "BLOQUEADA"
                            colorTexto: Theme.aviso
                        }
                    }
                    Selector {
                        etiqueta: "Tipo"
                        model: panel.editor.transitionNames
                        indice: propiedades.tr.typeIndex !== undefined ? propiedades.tr.typeIndex : 0
                        onElegido: (i) => panel.editor.setTransitionType(propiedades.tr.id, i)
                    }
                    CampoNumero {
                        etiqueta: "Duración"
                        unidad: "fot."
                        paso: 0.25
                        desde: 2
                        hasta: (propiedades.tr.maxBefore || 0) + (propiedades.tr.maxAfter || 0)
                        valor: propiedades.tr.length || 0
                        onCambiado: (v) => {
                            // Repartida como estaba antes y después del corte.
                            const t = propiedades.tr
                            const total = Math.round(v)
                            const parteAntes = t.length > 0 ? t.before / t.length : 0.5
                            let antes = Math.min(t.maxBefore, Math.round(total * parteAntes))
                            let despues = Math.min(t.maxAfter, total - antes)
                            antes = Math.min(t.maxBefore, total - despues)
                            panel.editor.setTransitionLength(t.id, antes, despues)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: propiedadesTransicion.segundos(propiedades.tr.length || 0) + " · "
                              + (propiedades.tr.before || 0) + " antes del corte y "
                              + (propiedades.tr.after || 0) + " después (arrastra sus bordes en "
                              + "la línea de tiempo)"
                        wrapMode: Text.WordWrap
                        color: Theme.textoSecundario
                        font.pixelSize: Theme.tamTextoPequeno
                    }
                    Deslizador {
                        etiqueta: "Intensidad"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        valor: propiedades.tr.intensity !== undefined ? propiedades.tr.intensity : 0.6
                        textoValor: Math.round(valorActual * 100) + " %"
                        onSoltado: (v) => panel.editor.setTransitionValue(propiedades.tr.id, "intensity", v)
                    }
                    Deslizador {
                        etiqueta: "Desenfoque de movimiento"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        valor: propiedades.tr.blur !== undefined ? propiedades.tr.blur : 0.6
                        textoValor: Math.round(valorActual * 100) + " %"
                        onSoltado: (v) => panel.editor.setTransitionValue(propiedades.tr.id, "blur", v)
                    }
                    Selector {
                        etiqueta: "Curva de movimiento"
                        model: panel.editor.easingNames
                        indice: propiedades.tr.easingIndex !== undefined ? propiedades.tr.easingIndex : 0
                        onElegido: (i) => panel.editor.setTransitionValue(propiedades.tr.id, "easing", i)
                    }

                    Separador {}
                    Selector {
                        etiqueta: "Efecto de sonido"
                        model: panel.editor.soundNames
                        indice: propiedades.tr.soundIndex !== undefined ? propiedades.tr.soundIndex : 0
                        onElegido: (i) => panel.editor.setTransitionSound(propiedades.tr.id, i)
                    }
                    RowLayout {
                        visible: propiedades.tr.hasSound === true
                        Layout.fillWidth: true
                        spacing: 8
                        Deslizador {
                            etiqueta: "Volumen del sonido"
                            from: -60
                            to: 15
                            stepSize: 0.5
                            valor: propiedades.tr.soundVolumeDb !== undefined ? propiedades.tr.soundVolumeDb : 0
                            textoValor: valorActual <= -60 ? "Silencio"
                                        : (valorActual > 0 ? "+" : "") + valorActual.toFixed(1) + " dB"
                            onSoltado: (v) => panel.editor.setClipVolume(propiedades.tr.soundClip, v)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "El sonido es un clip de audio normal: puedes moverlo o recortarlo en la "
                              + "línea de tiempo. Se coloca con su golpe en el corte, en la primera pista "
                              + "de audio libre."
                        color: Theme.textoDesactivado
                        font.pixelSize: Theme.tamTextoPequeno
                    }
                    BotonIcono {
                        Layout.fillWidth: true
                        icono: "quitar"
                        texto: "Quitar la transición"
                        ayuda: "Quita la transición y su sonido (Supr)"
                        onPulsado: panel.editor.removeTransition(propiedades.tr.id)
                    }
                }

                Text {
                    visible: !propiedades.hayTransicion
                    text: "CLIP SELECCIONADO"
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                    font.letterSpacing: 0.6
                }
                Text {
                    visible: !propiedades.hayTransicion
                    Layout.fillWidth: true
                    text: panel.editor.selectionDescription.length > 0
                          ? panel.editor.selectionDescription : "Ningún clip seleccionado"
                    color: panel.editor.selectionDescription.length > 0 ? Theme.texto
                                                                        : Theme.textoDesactivado
                    font.pixelSize: Theme.tamTexto
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    visible: !propiedades.uno && !propiedades.hayTransicion
                    wrapMode: Text.WordWrap
                    text: propiedades.sel.count > 1
                          ? "Selecciona un solo clip para ver y cambiar sus propiedades."
                          : "Selecciona un clip en la línea de tiempo (o haz clic sobre él en el visor)."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }

                // --- Video: posición, escala, opacidad y recorte ---
                Separador { visible: propiedades.hayVideo }
                RowLayout {
                    visible: propiedades.hayVideo
                    Layout.fillWidth: true
                    Icono { nombre: "video"; tam: 15; color: Theme.clipVideoClaro }
                    Text {
                        Layout.fillWidth: true
                        text: "Video · " + (propiedades.sel.videoTrack || "")
                        color: Theme.texto
                        font.pixelSize: Theme.tamTexto
                        font.bold: true
                    }
                    Chip {
                        visible: propiedades.hayVideo && propiedades.sel.videoLocked
                        texto: "BLOQUEADA"
                        colorTexto: Theme.aviso
                    }
                }
                ColumnLayout {
                    visible: propiedades.hayVideo
                    enabled: propiedades.videoEditable
                    Layout.fillWidth: true
                    spacing: 6

                    CampoNumero {
                        etiqueta: "Posición X"
                        unidad: "px"
                        valor: propiedades.t.x || 0
                        activo: propiedades.videoEditable
                        onCambiando: (v) => propiedades.vista({ x: v })
                        onCambiado: (v) => propiedades.aplicar({ x: v }, "Posición")
                    }
                    CampoNumero {
                        etiqueta: "Posición Y"
                        unidad: "px"
                        valor: propiedades.t.y || 0
                        activo: propiedades.videoEditable
                        onCambiando: (v) => propiedades.vista({ y: v })
                        onCambiado: (v) => propiedades.aplicar({ y: v }, "Posición")
                    }
                    CampoNumero {
                        etiqueta: "Escala"
                        unidad: "%"
                        decimales: 1
                        paso: 0.5
                        desde: 1
                        hasta: 2000
                        valor: (propiedades.t.scale || 1) * 100
                        activo: propiedades.videoEditable
                        onCambiando: (v) => propiedades.vista({ scale: v / 100 })
                        onCambiado: (v) => propiedades.aplicar({ scale: v / 100 }, "Escala")
                    }
                    CampoNumero {
                        etiqueta: "Opacidad"
                        unidad: "%"
                        paso: 0.5
                        desde: 0
                        hasta: 100
                        valor: (propiedades.t.opacity !== undefined ? propiedades.t.opacity : 1) * 100
                        activo: propiedades.videoEditable
                        onCambiando: (v) => propiedades.vista({ opacity: v / 100 })
                        onCambiado: (v) => propiedades.aplicar({ opacity: v / 100 }, "Opacidad")
                    }
                    Text {
                        Layout.topMargin: 4
                        text: "RECORTE"
                        color: Theme.textoDesactivado
                        font.pixelSize: Theme.tamTextoPequeno
                        font.letterSpacing: 0.6
                    }
                    Repeater {
                        model: [
                            { clave: "cropLeft", nombre: "Izquierda" },
                            { clave: "cropRight", nombre: "Derecha" },
                            { clave: "cropTop", nombre: "Arriba" },
                            { clave: "cropBottom", nombre: "Abajo" }
                        ]
                        delegate: CampoNumero {
                            required property var modelData
                            etiqueta: modelData.nombre
                            unidad: "%"
                            decimales: 1
                            paso: 0.2
                            desde: 0
                            hasta: 95
                            valor: (propiedades.t[modelData.clave] || 0) * 100
                            activo: propiedades.videoEditable
                            onCambiando: (v) => {
                                const valores = {}
                                valores[modelData.clave] = v / 100
                                propiedades.vista(valores)
                            }
                            onCambiado: (v) => {
                                const valores = {}
                                valores[modelData.clave] = v / 100
                                propiedades.aplicar(valores, "Recorte")
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 6
                        BotonIcono {
                            Layout.fillWidth: true
                            icono: "ajustar"
                            texto: "Ajustar"
                            ayuda: "Ajustar al lienzo: se ve entero (con bandas si no encaja)"
                            onPulsado: panel.editor.fitClip(propiedades.sel.video, false)
                        }
                        BotonIcono {
                            Layout.fillWidth: true
                            icono: "rellenar"
                            texto: "Rellenar"
                            ayuda: "Rellenar el lienzo: sin bandas (p. ej. un video 16:9 en vertical)"
                            onPulsado: panel.editor.fitClip(propiedades.sel.video, true)
                        }
                        BotonIcono {
                            icono: "restablecer"
                            ayuda: "Restablecer posición, escala, opacidad y recorte"
                            onPulsado: panel.editor.resetTransform(propiedades.sel.video)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Arrastra el número para cambiarlo (Mayús: más rápido, Ctrl: más fino) "
                              + "o haz clic para escribirlo. En el visor: arrastra el clip para "
                              + "moverlo y sus esquinas para escalarlo."
                        color: Theme.textoDesactivado
                        font.pixelSize: Theme.tamTextoPequeno
                    }
                }

                // --- Audio: volumen ---
                Separador { visible: propiedades.hayAudio }
                RowLayout {
                    visible: propiedades.hayAudio
                    Layout.fillWidth: true
                    Icono { nombre: "audio"; tam: 15; color: Theme.clipAudioClaro }
                    Text {
                        Layout.fillWidth: true
                        text: "Audio · " + (propiedades.sel.audioTrack || "")
                        color: Theme.texto
                        font.pixelSize: Theme.tamTexto
                        font.bold: true
                    }
                    Chip {
                        visible: propiedades.hayAudio && propiedades.sel.audioLocked
                        texto: "BLOQUEADA"
                        colorTexto: Theme.aviso
                    }
                }
                RowLayout {
                    visible: propiedades.hayAudio
                    enabled: propiedades.hayAudio && !propiedades.sel.audioLocked
                    opacity: enabled ? 1 : 0.45
                    Layout.fillWidth: true
                    spacing: 8
                    Deslizador {
                        id: volumen
                        etiqueta: "Volumen"
                        from: -60
                        to: 15
                        stepSize: 0.5
                        valor: propiedades.sel.volumeDb !== undefined ? propiedades.sel.volumeDb : 0
                        textoValor: valorActual <= -60 ? "Silencio"
                                    : (valorActual > 0 ? "+" : "") + valorActual.toFixed(1) + " dB"
                        onSoltado: (v) => panel.editor.setClipVolume(propiedades.sel.audio, v)
                    }
                    BotonIcono {
                        Layout.alignment: Qt.AlignBottom
                        texto: "0 dB"
                        ayuda: "Volumen original"
                        implicitHeight: 26
                        onPulsado: panel.editor.setClipVolume(propiedades.sel.audio, 0)
                    }
                }

                // --- Enlace entre video y audio ---
                Separador { visible: propiedades.uno && propiedades.sel.linked }
                BotonIcono {
                    visible: propiedades.uno && propiedades.sel.linked
                    Layout.fillWidth: true
                    icono: "desvincular"
                    texto: "Desvincular audio y video"
                    ayuda: "Para mover, recortar o quitar el video y su audio por separado"
                    onPulsado: panel.editor.unlinkSelection()
                }
            }
        }
    }

    component Separador: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 2
        implicitHeight: 1
        color: Theme.borde
    }

    // Al poner (o elegir) una transición desde su pestaña, se pasa a sus propiedades.
    Connections {
        target: panel.editor
        function onSelectedClipChanged() {
            if (panel.pestana === 1 && panel.editor.selectedTransition.id !== undefined)
                panel.pestana = 3
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
