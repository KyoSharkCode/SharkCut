import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Panel Propiedades del editor (entre la biblioteca y el visor): lo que se ha
// seleccionado en la línea de tiempo, un clip (posición, escala, opacidad, recorte,
// volumen) o una transición (tipo, duración, curva, sonido).
Rectangle {
    id: panel

    required property Editor editor

    color: Theme.panel
    radius: Theme.radio
    border.color: Theme.borde

    Text {
        x: 14
        y: 10
        text: "Propiedades"
        color: Theme.texto
        font.pixelSize: Theme.tamTexto
        font.bold: true
    }
    Rectangle { y: 36; width: parent.width; height: 1; color: Theme.borde }

    // --- Propiedades del clip seleccionado ---
    Flickable {
        id: propiedades
        anchors.fill: parent
        anchors.topMargin: 37
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
        readonly property var croma: t.chromaKey || ({})
        readonly property var fx: t.effects || ({})
        readonly property var anim: hayVideo && sel.animation ? sel.animation : ({})
        readonly property var sub: panel.editor.selectedCaption
        readonly property bool haySubtitulo: sub.count > 0
        readonly property var estilo: haySubtitulo
            ? (estiloPista ? sub.trackStyle : sub.style) : ({})
        property bool estiloPista: false // el estilo se aplica a la pista entera
        function vistaEstilo(valores) { panel.editor.previewCaptionStyle(valores, estiloPista) }
        function aplicarEstilo(valores, etiqueta) {
            panel.editor.setCaptionStyle(valores, estiloPista, etiqueta)
        }
        readonly property bool hayCroma: hayVideo && croma.added === true
        readonly property bool videoEditable: hayVideo && !sel.videoLocked

        // Al aplicar o detectar la pantalla verde, se lleva a la vista su sección.
        // Lo mismo con los demás efectos al aplicarlos desde la pestaña Efectos.
        property Item seccionPendiente: null
        Connections {
            target: panel.editor
            function onKeyColorDetected(found) {
                propiedades.seccionPendiente = cabeceraCroma
                desplazarCroma.restart()
            }
            function onEffectApplied(efecto) {
                if (efecto.startsWith("ajuste:"))
                    return // un ajuste guardado: cambia varias secciones a la vez
                propiedades.seccionPendiente = efecto === "byn" ? seccionByn
                    : efecto === "desenfoque" ? seccionDesenfoque
                    : efecto === "esquinas" ? seccionEsquinas : seccionSombra
                desplazarCroma.restart()
            }
        }
        // Espera a que la columna coloque la sección recién añadida.
        Timer {
            id: desplazarCroma
            interval: 80
            onTriggered: {
                if (propiedades.seccionPendiente)
                    propiedades.contentY = Math.max(0, Math.min(propiedades.seccionPendiente.y,
                        propiedades.contentHeight - propiedades.height))
            }
        }

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

            // --- Subtítulo seleccionado: texto y estilo ---
            ColumnLayout {
                id: propiedadesSubtitulo
                visible: propiedades.haySubtitulo
                Layout.fillWidth: true
                spacing: 6
                enabled: !propiedades.sub.locked

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: propiedades.sub.count > 1
                              ? propiedades.sub.count + " subtítulos seleccionados"
                              : "Subtítulo · " + (propiedades.sub.trackName || "")
                        color: Theme.texto
                        font.pixelSize: Theme.tamTexto
                        font.bold: true
                    }
                    Chip {
                        visible: propiedades.sub.locked === true
                        texto: "BLOQUEADA"
                        colorTexto: Theme.aviso
                    }
                }
                Text {
                    visible: propiedades.sub.count === 1
                    text: panel.editor.timecodeFor(propiedades.sub.start || 0) + " → "
                          + panel.editor.timecodeFor(propiedades.sub.end || 0)
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                }
                // Texto (Ctrl+Intro o al salir del campo se aplica).
                Rectangle {
                    visible: propiedades.sub.count === 1
                    Layout.fillWidth: true
                    implicitHeight: Math.max(56, campoTexto.implicitHeight + 12)
                    radius: Theme.radio
                    color: Theme.fondo
                    border.color: campoTexto.activeFocus ? Theme.acento : Theme.borde
                    TextEdit {
                        id: campoTexto
                        anchors.fill: parent
                        anchors.margins: 6
                        text: propiedades.sub.text || ""
                        color: Theme.texto
                        font.pixelSize: Theme.tamTexto
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        function aplicar() {
                            if (text !== propiedades.sub.text)
                                panel.editor.setCaptionText(propiedades.sub.id, text)
                            text = Qt.binding(() => propiedades.sub.text || "")
                        }
                        onEditingFinished: aplicar()
                        Keys.onPressed: (e) => {
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter)
                                    && (e.modifiers & Qt.ControlModifier)) {
                                aplicar()
                                campoTexto.focus = false
                                e.accepted = true
                            } else if (e.key === Qt.Key_Escape) {
                                text = Qt.binding(() => propiedades.sub.text || "")
                                campoTexto.focus = false
                                e.accepted = true
                            }
                        }
                    }
                }
                Text {
                    visible: propiedades.sub.count === 1
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Intro: nueva línea · Ctrl+Intro: aplicar · doble clic en la pista: "
                          + "subtítulo nuevo."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }

                Separador {}
                Text {
                    text: "ESTILO"
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                    font.letterSpacing: 0.6
                }
                // A qué se aplica: a lo seleccionado o a la pista entera.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    BotonIcono {
                        Layout.fillWidth: true
                        texto: propiedades.sub.count > 1 ? "Seleccionados" : "Este subtítulo"
                        activo: !propiedades.estiloPista
                        implicitHeight: 26
                        onPulsado: propiedades.estiloPista = false
                    }
                    BotonIcono {
                        Layout.fillWidth: true
                        texto: "Toda la pista"
                        activo: propiedades.estiloPista
                        implicitHeight: 26
                        ayuda: "El estilo de la pista: lo usan todos sus subtítulos"
                        onPulsado: propiedades.estiloPista = true
                    }
                }
                Selector {
                    etiqueta: "Ajuste guardado"
                    model: ["Elegir…"].concat(panel.editor.captionPresets.map((p) => p.name))
                    indice: 0
                    onElegido: (i) => {
                        if (i > 0)
                            panel.editor.applyCaptionPreset(panel.editor.captionPresets[i - 1].name,
                                                            propiedades.estiloPista)
                    }
                }
                SelectorFuente {
                    fuente: propiedades.estilo.font || ""
                    onElegida: (f) => propiedades.aplicarEstilo({ font: f }, "Fuente")
                }
                CampoNumero {
                    etiqueta: "Tamaño"
                    unidad: "px"
                    paso: 0.5
                    desde: 8
                    hasta: 400
                    valor: propiedades.estilo.size || 64
                    onCambiando: (v) => propiedades.vistaEstilo({ size: v })
                    onCambiado: (v) => propiedades.aplicarEstilo({ size: v }, "Tamaño")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [{ clave: "bold", texto: "Negrita" }, { clave: "italic", texto: "Cursiva" },
                                { clave: "uppercase", texto: "MAYÚSCULAS" }]
                        delegate: BotonIcono {
                            required property var modelData
                            Layout.fillWidth: true
                            texto: modelData.texto
                            implicitHeight: 26
                            activo: propiedades.estilo[modelData.clave] === true
                            onPulsado: {
                                const valores = {}
                                valores[modelData.clave] = !propiedades.estilo[modelData.clave]
                                propiedades.aplicarEstilo(valores, modelData.texto)
                            }
                        }
                    }
                }
                CampoColor {
                    etiqueta: "Color"
                    color: propiedades.estilo.color || "#FFFFFF"
                    onElegido: (c) => propiedades.aplicarEstilo({ color: c }, "Color")
                }
                CampoColor {
                    etiqueta: "Contorno"
                    color: propiedades.estilo.outlineColor || "#000000"
                    onElegido: (c) => propiedades.aplicarEstilo({ outlineColor: c }, "Contorno")
                }
                CampoNumero {
                    etiqueta: "Grosor del contorno"
                    anchoEtiqueta: 140
                    unidad: "px"
                    paso: 0.1
                    decimales: 1
                    desde: 0
                    hasta: 40
                    valor: propiedades.estilo.outline !== undefined ? propiedades.estilo.outline : 6
                    onCambiando: (v) => propiedades.vistaEstilo({ outline: v })
                    onCambiado: (v) => propiedades.aplicarEstilo({ outline: v }, "Contorno")
                }
                CampoColor {
                    etiqueta: "Sombra"
                    color: propiedades.estilo.shadowColor || "#000000"
                    onElegido: (c) => propiedades.aplicarEstilo({ shadowColor: c }, "Sombra")
                }
                CampoNumero {
                    etiqueta: "Opacidad de la sombra"
                    anchoEtiqueta: 140
                    unidad: "%"
                    paso: 0.5
                    desde: 0
                    hasta: 100
                    valor: (propiedades.estilo.shadow || 0) * 100
                    onCambiando: (v) => propiedades.vistaEstilo({ shadow: v / 100 })
                    onCambiado: (v) => propiedades.aplicarEstilo({ shadow: v / 100 }, "Sombra")
                }
                CampoColor {
                    etiqueta: "Caja de fondo"
                    color: propiedades.estilo.boxColor || "#000000"
                    onElegido: (c) => propiedades.aplicarEstilo({ boxColor: c }, "Caja de fondo")
                }
                CampoNumero {
                    etiqueta: "Opacidad de la caja"
                    anchoEtiqueta: 140
                    unidad: "%"
                    paso: 0.5
                    desde: 0
                    hasta: 100
                    valor: (propiedades.estilo.box || 0) * 100
                    onCambiando: (v) => propiedades.vistaEstilo({ box: v / 100 })
                    onCambiado: (v) => propiedades.aplicarEstilo({ box: v / 100 }, "Caja de fondo")
                }
                Text {
                    Layout.topMargin: 4
                    text: "POSICIÓN"
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                    font.letterSpacing: 0.6
                }
                Repeater {
                    model: [{ clave: "x", nombre: "Horizontal" }, { clave: "y", nombre: "Vertical" },
                            { clave: "maxWidth", nombre: "Ancho máximo" }]
                    delegate: CampoNumero {
                        required property var modelData
                        etiqueta: modelData.nombre
                        unidad: "%"
                        decimales: 1
                        paso: 0.2
                        desde: modelData.clave === "maxWidth" ? 10 : 0
                        hasta: 100
                        valor: (propiedades.estilo[modelData.clave] || 0) * 100
                        onCambiando: (v) => {
                            const valores = {}
                            valores[modelData.clave] = v / 100
                            propiedades.vistaEstilo(valores)
                        }
                        onCambiado: (v) => {
                            const valores = {}
                            valores[modelData.clave] = v / 100
                            propiedades.aplicarEstilo(valores, "Posición")
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 6
                    BotonIcono {
                        Layout.fillWidth: true
                        icono: "guardar"
                        texto: "Guardar como ajuste…"
                        ayuda: "Guarda este estilo con un nombre para aplicarlo después"
                        onPulsado: dialogoAjuste.pedir("Nombre del ajuste", "", "Guardar",
                                                       (nombre) => panel.editor.saveCaptionPreset(nombre))
                    }
                    BotonIcono {
                        icono: "restablecer"
                        visible: propiedades.sub.ownStyle === true && !propiedades.estiloPista
                        ayuda: "Quitar su estilo propio: vuelve al de la pista"
                        onPulsado: panel.editor.resetCaptionStyle()
                    }
                }
                // Ajustes guardados por ti: se pueden borrar.
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: panel.editor.captionPresets.filter((p) => !p.builtin)
                        delegate: Chip {
                            required property var modelData
                            texto: modelData.name + "  ✕"
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.editor.removeCaptionPreset(parent.modelData.name)
                            }
                        }
                    }
                }
                BotonIcono {
                    Layout.fillWidth: true
                    icono: "quitar"
                    texto: propiedades.sub.count > 1 ? "Quitar los subtítulos" : "Quitar el subtítulo"
                    ayuda: "Supr"
                    onPulsado: panel.editor.removeSelection()
                }
            }

            Text {
                visible: !propiedades.hayTransicion && !propiedades.haySubtitulo
                text: "CLIP SELECCIONADO"
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
                font.letterSpacing: 0.6
            }
            Text {
                visible: !propiedades.hayTransicion && !propiedades.haySubtitulo
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
                visible: !propiedades.uno && !propiedades.hayTransicion && !propiedades.haySubtitulo
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

                CampoAnimable {
                    clave: "x"
                    etiqueta: "Posición X"
                    unidad: "px"
                    valor: propiedades.t.x || 0
                    activo: propiedades.videoEditable
                    onCambiando: (v) => propiedades.vista({ x: v })
                    onCambiado: (v) => propiedades.aplicar({ x: v }, "Posición")
                }
                CampoAnimable {
                    clave: "y"
                    etiqueta: "Posición Y"
                    unidad: "px"
                    valor: propiedades.t.y || 0
                    activo: propiedades.videoEditable
                    onCambiando: (v) => propiedades.vista({ y: v })
                    onCambiado: (v) => propiedades.aplicar({ y: v }, "Posición")
                }
                CampoAnimable {
                    clave: "scale"
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
                CampoAnimable {
                    clave: "rotation"
                    etiqueta: "Rotación"
                    unidad: "°"
                    decimales: 1
                    paso: 0.5
                    desde: -3600
                    hasta: 3600
                    valor: propiedades.t.rotation || 0
                    activo: propiedades.videoEditable
                    onCambiando: (v) => propiedades.vista({ rotation: v })
                    onCambiado: (v) => propiedades.aplicar({ rotation: v }, "Rotación")
                }
                CampoAnimable {
                    clave: "opacity"
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
                    delegate: CampoAnimable {
                        required property var modelData
                        clave: modelData.clave
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
                // Curva de los keyframes del cabezal (hasta el siguiente).
                Selector {
                    visible: propiedades.anim.keyEasing !== undefined && propiedades.anim.keyEasing >= 0
                    etiqueta: "Curva del keyframe"
                    model: panel.editor.keyframeEasingNames
                    indice: Math.max(0, propiedades.anim.keyEasing || 0)
                    onElegido: (i) => panel.editor.setKeyframeEasing(propiedades.sel.video, i)
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
                BotonIcono {
                    Layout.fillWidth: true
                    icono: "estrella"
                    texto: "Guardar como ajuste…"
                    ayuda: "Guarda el aspecto del clip (y su animación) para aplicarlo a otros "
                           + "desde la pestaña Efectos"
                    onPulsado: dialogoAjusteClip.guardar(propiedades.sel.video,
                                                         propiedades.anim.any === true)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Arrastra el número para cambiarlo (Mayús: más rápido, Ctrl: más fino) "
                          + "o haz clic para escribirlo. En el visor: arrastra el clip para "
                          + "moverlo, sus esquinas para escalarlo y el círculo de arriba para "
                          + "girarlo (Mayús: de 15° en 15°). ⏱ anima la propiedad: "
                          + "cada cambio en otro momento crea un keyframe ahí (◆)."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }
            }

            // --- Pantalla verde (croma): solo si se le añadió el efecto (desde Efectos) ---
            Separador { visible: propiedades.hayCroma }
            RowLayout {
                id: cabeceraCroma
                visible: propiedades.hayCroma
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: "Pantalla verde"
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                    font.bold: true
                }
                BotonIcono {
                    texto: propiedades.croma.enabled ? "Activada" : "Desactivada"
                    activo: propiedades.croma.enabled === true
                    enabled: propiedades.videoEditable
                    implicitHeight: 26
                    ayuda: "Vuelve transparente el color de fondo del clip"
                    onPulsado: propiedades.aplicar({ keyEnabled: !propiedades.croma.enabled },
                                                   "Pantalla verde")
                }
                BotonIcono {
                    icono: "quitar"
                    enabled: propiedades.videoEditable
                    implicitHeight: 26
                    ayuda: "Quitar el efecto de pantalla verde del clip"
                    onPulsado: propiedades.aplicar({ keyRemove: true }, "Quitar pantalla verde")
                }
            }
            ColumnLayout {
                visible: propiedades.hayCroma
                enabled: propiedades.videoEditable
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    // Muestra del color que se quita (es un color del video, no del tema).
                    Rectangle {
                        implicitWidth: 26
                        implicitHeight: 26
                        radius: Theme.radio
                        color: propiedades.croma.color || "transparent"
                        border.color: Theme.borde
                    }
                    TextField {
                        id: campoColor
                        Layout.preferredWidth: 84
                        implicitHeight: 26
                        text: propiedades.croma.color || ""
                        color: Theme.texto
                        font.pixelSize: Theme.tamTextoPequeno
                        font.family: "monospace"
                        selectByMouse: true
                        maximumLength: 7
                        validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
                        background: Rectangle {
                            radius: Theme.radio
                            color: Theme.fondo
                            border.color: campoColor.activeFocus ? Theme.acento : Theme.borde
                        }
                        onEditingFinished: {
                            const hex = text.startsWith("#") ? text : "#" + text
                            if (/^#[0-9a-fA-F]{6}$/.test(hex))
                                propiedades.aplicar({ keyEnabled: true, keyColor: hex }, "Pantalla verde")
                            // Al escribir se pierde el enlace: que siga al clip seleccionado.
                            text = Qt.binding(() => propiedades.croma.color || "")
                        }
                    }
                    BotonIcono {
                        texto: "Verde"
                        implicitHeight: 26
                        ayuda: "Verde puro (#00FF00), el de la mayoría de los efectos con fondo verde"
                        onPulsado: propiedades.aplicar({ keyEnabled: true, keyColor: "#00FF00" },
                                                       "Pantalla verde")
                    }
                    BotonIcono {
                        texto: "Azul"
                        implicitHeight: 26
                        ayuda: "Pantalla azul (#0047BB)"
                        onPulsado: propiedades.aplicar({ keyEnabled: true, keyColor: "#0047BB" },
                                                       "Pantalla verde")
                    }
                    Item { Layout.fillWidth: true }
                }
                BotonIcono {
                    Layout.fillWidth: true
                    icono: "gotero"
                    texto: panel.editor.detectingKey ? "Buscando el color…" : "Detectar el color del fondo"
                    enabled: !panel.editor.detectingKey
                    ayuda: "Busca el color de fondo en los bordes del fotograma que se ve y activa "
                           + "la pantalla verde con él"
                    onPulsado: {
                        avisoCroma.visible = false
                        panel.editor.detectKeyColor(propiedades.sel.video)
                    }
                }
                Text {
                    id: avisoCroma
                    Layout.fillWidth: true
                    visible: false
                    wrapMode: Text.WordWrap
                    text: "No se encontró un color de fondo en los bordes de este fotograma. "
                          + "Mueve el cabezal a un momento en que se vea el fondo o escribe el color."
                    color: Theme.aviso
                    font.pixelSize: Theme.tamTextoPequeno
                    Connections {
                        target: panel.editor
                        function onKeyColorDetected(found) { avisoCroma.visible = !found }
                    }
                }
                CampoNumero {
                    etiqueta: "Similitud"
                    unidad: "%"
                    paso: 0.2
                    desde: 0
                    hasta: 100
                    valor: (propiedades.croma.similarity || 0) * 100
                    activo: propiedades.videoEditable
                    onCambiando: (v) => propiedades.vista({ keySimilarity: v / 100 })
                    onCambiado: (v) => propiedades.aplicar({ keySimilarity: v / 100 }, "Pantalla verde")
                }
                CampoNumero {
                    etiqueta: "Suavidad"
                    unidad: "%"
                    paso: 0.2
                    desde: 0
                    hasta: 100
                    valor: (propiedades.croma.smoothness || 0) * 100
                    activo: propiedades.videoEditable
                    onCambiando: (v) => propiedades.vista({ keySmoothness: v / 100 })
                    onCambiado: (v) => propiedades.aplicar({ keySmoothness: v / 100 }, "Pantalla verde")
                }
                CampoNumero {
                    etiqueta: "Quitar reflejo"
                    unidad: "%"
                    paso: 0.2
                    desde: 0
                    hasta: 100
                    valor: (propiedades.croma.spill || 0) * 100
                    activo: propiedades.videoEditable
                    onCambiando: (v) => propiedades.vista({ keySpill: v / 100 })
                    onCambiado: (v) => propiedades.aplicar({ keySpill: v / 100 }, "Pantalla verde")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Similitud: cuánto se parece un color al del fondo para quitarlo (súbela "
                          + "si quedan restos del fondo). Suavidad: borde gradual. Quitar reflejo: "
                          + "elimina el tono del fondo que se cuela en los bordes."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }
            }

            // --- Efectos de imagen (solo los que tiene el clip) ---
            CabeceraEfecto {
                id: seccionByn
                visible: propiedades.hayVideo && propiedades.fx.grayscaleOn === true
                titulo: "Blanco y negro"
                onQuitar: propiedades.aplicar({ grayscaleOn: false }, "Quitar blanco y negro")
            }
            CampoAnimable {
                clave: "grayscale"
                visible: seccionByn.visible
                enabled: propiedades.videoEditable
                etiqueta: "Intensidad"
                unidad: "%"
                paso: 0.5
                desde: 0
                hasta: 100
                valor: (propiedades.fx.grayscale !== undefined ? propiedades.fx.grayscale : 1) * 100
                onCambiando: (v) => propiedades.vista({ grayscale: v / 100 })
                onCambiado: (v) => propiedades.aplicar({ grayscale: v / 100 }, "Blanco y negro")
            }

            CabeceraEfecto {
                id: seccionDesenfoque
                visible: propiedades.hayVideo && propiedades.fx.blurOn === true
                titulo: "Desenfoque"
                onQuitar: propiedades.aplicar({ blurOn: false }, "Quitar desenfoque")
            }
            CampoAnimable {
                clave: "blur"
                visible: seccionDesenfoque.visible
                enabled: propiedades.videoEditable
                etiqueta: "Cantidad"
                unidad: "px"
                decimales: 1
                paso: 0.2
                desde: 0
                hasta: 200
                valor: propiedades.fx.blur || 0
                onCambiando: (v) => propiedades.vista({ blur: v })
                onCambiado: (v) => propiedades.aplicar({ blur: v }, "Desenfoque")
            }

            CabeceraEfecto {
                id: seccionEsquinas
                visible: propiedades.hayVideo && propiedades.fx.cornersOn === true
                titulo: "Esquinas redondeadas"
                onQuitar: propiedades.aplicar({ cornersOn: false }, "Quitar esquinas redondeadas")
            }
            CampoAnimable {
                clave: "corners"
                visible: seccionEsquinas.visible
                enabled: propiedades.videoEditable
                etiqueta: "Radio"
                unidad: "px"
                paso: 0.5
                desde: 0
                hasta: 2000
                valor: propiedades.fx.corners || 0
                onCambiando: (v) => propiedades.vista({ corners: v })
                onCambiado: (v) => propiedades.aplicar({ corners: v }, "Esquinas redondeadas")
            }

            CabeceraEfecto {
                id: seccionSombra
                visible: propiedades.hayVideo && propiedades.fx.shadowOn === true
                titulo: "Sombra o resplandor"
                onQuitar: propiedades.aplicar({ shadowOn: false }, "Quitar sombra")
            }
            ColumnLayout {
                visible: seccionSombra.visible
                enabled: propiedades.videoEditable
                Layout.fillWidth: true
                spacing: 6
                CampoColor {
                    etiqueta: "Color"
                    color: propiedades.fx.shadowColor || "#000000"
                    onElegido: (c) => propiedades.aplicar({ shadowColor: c }, "Color de la sombra")
                }
                Repeater {
                    model: [
                        { clave: "shadowOpacity", nombre: "Opacidad", unidad: "%", factor: 100, desde: 0, hasta: 100 },
                        { clave: "shadowSoftness", nombre: "Difuminado", unidad: "px", factor: 1, desde: 0, hasta: 200 },
                        { clave: "shadowSpread", nombre: "Tamaño", unidad: "px", factor: 1, desde: 0, hasta: 200 },
                        { clave: "shadowX", nombre: "Distancia X", unidad: "px", factor: 1, desde: -2000, hasta: 2000 },
                        { clave: "shadowY", nombre: "Distancia Y", unidad: "px", factor: 1, desde: -2000, hasta: 2000 }
                    ]
                    delegate: CampoAnimable {
                        required property var modelData
                        clave: modelData.clave
                        etiqueta: modelData.nombre
                        unidad: modelData.unidad
                        paso: 0.5
                        desde: modelData.desde
                        hasta: modelData.hasta
                        valor: (propiedades.fx[modelData.clave] || 0) * modelData.factor
                        onCambiando: (v) => {
                            const valores = {}
                            valores[modelData.clave] = v / modelData.factor
                            propiedades.vista(valores)
                        }
                        onCambiado: (v) => {
                            const valores = {}
                            valores[modelData.clave] = v / modelData.factor
                            propiedades.aplicar(valores, "Sombra")
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Sigue la forma del clip, también su transparencia y la pantalla verde. "
                          + "Para un resplandor: color vivo, distancia 0 y algo de tamaño."
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
    DialogoNombre { id: dialogoAjuste }
    DialogoAjusteClip { id: dialogoAjusteClip; editor: panel.editor }

    // Cabecera de un efecto del clip: nombre y botón para quitarlo.
    component CabeceraEfecto: ColumnLayout {
        id: cabeceraEfecto
        property string titulo
        signal quitar()
        Layout.fillWidth: true
        spacing: 6
        Separador {}
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: cabeceraEfecto.titulo
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                font.bold: true
            }
            BotonIcono {
                icono: "quitar"
                implicitHeight: 26
                enabled: propiedades.videoEditable
                ayuda: "Quitar el efecto «" + cabeceraEfecto.titulo + "»"
                onPulsado: cabeceraEfecto.quitar()
            }
        }
    }

    // Valor de una propiedad animable del clip: el número y, a su derecha, ⏱ (animarla)
    // y, si está animada, ◀ ◆ ▶ (keyframe anterior, poner o quitar el del cabezal,
    // siguiente).
    component CampoAnimable: RowLayout {
        id: campoA
        property string clave
        property alias etiqueta: numero.etiqueta
        property alias unidad: numero.unidad
        property alias valor: numero.valor
        property alias desde: numero.desde
        property alias hasta: numero.hasta
        property alias paso: numero.paso
        property alias decimales: numero.decimales
        property alias activo: numero.activo
        signal cambiando(real valor)
        signal cambiado(real valor)
        readonly property var info: propiedades.anim[clave] || ({})
        readonly property bool animada: info.animated === true

        Layout.fillWidth: true
        spacing: 2
        CampoNumero {
            id: numero
            Layout.fillWidth: true
            onCambiando: (v) => campoA.cambiando(v)
            onCambiado: (v) => campoA.cambiado(v)
        }
        BotonKey {
            icono: "reloj"
            encendido: campoA.animada
            ayuda: campoA.animada ? "Dejar de animar (se queda con el valor de ahora)"
                                  : "Animar: los cambios en distintos momentos crean keyframes"
            onPulsado: panel.editor.setPropertyAnimated(propiedades.sel.video, campoA.clave,
                                                        !campoA.animada)
        }
        BotonKey {
            visible: campoA.animada
            icono: "anterior"
            enabled: campoA.info.prev === true
            ayuda: "Keyframe anterior"
            onPulsado: panel.editor.goToKeyframe(propiedades.sel.video, campoA.clave, -1)
        }
        BotonKey {
            visible: campoA.animada
            icono: campoA.info.key ? "keyframeLleno" : "keyframe"
            encendido: campoA.info.key === true
            ayuda: campoA.info.key ? "Quitar el keyframe del cabezal" : "Poner un keyframe en el cabezal"
            onPulsado: panel.editor.toggleKeyframe(propiedades.sel.video, campoA.clave)
        }
        BotonKey {
            visible: campoA.animada
            icono: "siguiente"
            enabled: campoA.info.next === true
            ayuda: "Keyframe siguiente"
            onPulsado: panel.editor.goToKeyframe(propiedades.sel.video, campoA.clave, 1)
        }
    }

    component BotonKey: Rectangle {
        id: botonKey
        property string icono
        property string ayuda
        property bool encendido: false
        signal pulsado()
        implicitWidth: 20
        implicitHeight: 22
        radius: 4
        opacity: enabled ? 1 : 0.35
        color: zonaKey.containsMouse ? Theme.panelElevado : "transparent"
        Icono {
            anchors.centerIn: parent
            nombre: botonKey.icono
            tam: 13
            color: botonKey.encendido ? Theme.acento : Theme.textoSecundario
        }
        MouseArea {
            id: zonaKey
            anchors.fill: parent
            hoverEnabled: true
            onClicked: botonKey.pulsado()
        }
        ToolTip.visible: zonaKey.containsMouse && ayuda.length > 0
        ToolTip.delay: 600
        ToolTip.text: ayuda
    }

    // Color con su muestra y su código (#RRGGBB) editable, y unos colores rápidos.
    component CampoColor: RowLayout {
        id: campo
        property string etiqueta
        property string color
        signal elegido(string color)
        Layout.fillWidth: true
        spacing: 5
        Text {
            Layout.preferredWidth: 72
            text: campo.etiqueta
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            elide: Text.ElideRight
        }
        // Muestra del color elegido (es un color del video, no del tema).
        Rectangle {
            implicitWidth: 22
            implicitHeight: 22
            radius: 4
            color: campo.color
            border.color: Theme.borde
        }
        TextField {
            id: codigo
            Layout.preferredWidth: 70
            implicitHeight: 24
            text: campo.color
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
            onEditingFinished: {
                const hex = text.startsWith("#") ? text : "#" + text
                if (/^#[0-9a-fA-F]{6}$/.test(hex) && hex.toUpperCase() !== campo.color.toUpperCase())
                    campo.elegido(hex.toUpperCase())
                text = Qt.binding(() => campo.color)
            }
        }
        Repeater {
            // Colores rápidos (del video, no del tema).
            model: ["#FFFFFF", "#000000", "#FFD800"]
            delegate: Rectangle {
                required property string modelData
                implicitWidth: 14
                implicitHeight: 14
                radius: 7
                color: modelData
                border.color: Theme.borde
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: campo.elegido(parent.modelData)
                }
            }
        }
        Item { Layout.fillWidth: true }
    }

    component Separador: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 2
        implicitHeight: 1
        color: Theme.borde
    }
}
