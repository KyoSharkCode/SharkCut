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
    component Separador: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 2
        implicitHeight: 1
        color: Theme.borde
    }
}
