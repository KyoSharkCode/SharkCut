import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Línea de tiempo del editor: herramientas en vertical a la izquierda, regla y pistas
// (las de video de la más alta a V1, y debajo las de audio de A1 hacia abajo).
//
// Herramientas: Selección (mover clips, también a otra pista, y recortar sus bordes) y
// Cuchilla (dividir donde se hace clic). Mientras se arrastra solo se muestra una vista
// previa; la edición se aplica al soltar, como un único paso de deshacer.
//
// Cabecera de cada pista: ocultar (video) o silenciar (audio) y bloquear. Clic derecho:
// añadir o quitar pistas. Rueda: desplazar en el tiempo; sobre las cabeceras o con Alt,
// desplazar entre pistas; con Ctrl, zoom.
Rectangle {
    id: raiz

    required property Editor editor
    property string herramienta: "seleccion" // "seleccion" o "cuchilla"
    property bool iman: true                 // ajustar a bordes y cabezal al arrastrar

    // Vista previa del arrastre en curso:
    // {tipo: "mover" | "inicio" | "final", grupo: [ids], delta, valor, pistas, video}
    // (`pistas`: cambio de pista de los clips del tipo `video` al mover)
    property var arrastre: null
    property real marcaSoltar: -1 // fotograma donde caería lo que se arrastra encima
    property int pistaSoltar: -1  // pista donde caería
    property var corteSoltar: null // {track, cut} donde caería una transición

    property real escala: 0.5 // píxeles por fotograma
    readonly property real escalaMaxima: 24
    readonly property real escalaMinima: Math.min(escalaMaxima,
        Math.max(0.0005, (vista.width - 40) / Math.max(editor.frameCount, editor.fps * 10)))

    readonly property int alturaRegla: 26
    readonly property int alturaPista: 50
    readonly property int separacion: 4
    readonly property int separacionGrupos: 10 // entre las pistas de video y las de audio
    readonly property int filasVisibles: 6      // más pistas: desplazamiento vertical
    readonly property int anchoCabecera: 92

    // Filas en pantalla, de arriba abajo: {pista, y} (y relativa a la primera fila).
    readonly property var filas: {
        const pistas = editor.tracks
        const videos = pistas.filter((p) => p.video).reverse()
        const audios = pistas.filter((p) => !p.video)
        const lista = []
        let y = 0
        for (const p of videos) {
            lista.push({ pista: p, y: y })
            y += alturaPista + separacion
        }
        y += separacionGrupos - separacion
        for (const p of audios) {
            lista.push({ pista: p, y: y })
            y += alturaPista + separacion
        }
        return lista
    }
    readonly property real altoPistas: filas.length > 0
        ? filas[filas.length - 1].y + alturaPista : 0
    readonly property real yPrimeraFila: alturaRegla + separacion
    readonly property var yPorPista: {
        const mapa = {}
        for (const f of filas)
            mapa[f.pista.index] = f.y
        return mapa
    }

    color: Theme.panel
    radius: Theme.radio
    border.color: zonaSoltar.containsDrag ? Theme.acento : Theme.borde
    // Alta para ver hasta `filasVisibles` pistas (más la barra de desplazamiento), y como
    // mínimo tanto como la barra de herramientas vertical.
    implicitHeight: Math.max(yPrimeraFila
                             + Math.min(altoPistas, filasVisibles * (alturaPista + separacion)
                                                    + separacionGrupos) + 20,
                             herramientas.implicitHeight) + 2 * Theme.margen

    // --- Utilidades --------------------------------------------------------------

    function frameEn(xContenido) {
        return Math.max(0, Math.round(xContenido / escala))
    }

    function yDePista(indice) {
        return yPrimeraFila + (yPorPista[indice] !== undefined ? yPorPista[indice] : 0)
    }

    // Pista (objeto de editor.tracks) bajo una coordenada y del contenido, o null.
    function pistaEnY(yContenido) {
        const y = yContenido - yPrimeraFila
        for (const f of filas) {
            if (y >= f.y - separacion && y < f.y + alturaPista + separacion)
                return f.pista
        }
        return null
    }

    // Corte (fin de un clip = inicio del siguiente) de la pista más cercano a la x del
    // contenido, a menos de 60 píxeles: {track, cut}, o null.
    function corteCercano(pista, xContenido) {
        const finales = {}
        const lista = editor.clips
        for (let i = 0; i < lista.length; ++i) {
            if (lista[i].track === pista)
                finales[lista[i].start + lista[i].length] = true
        }
        let mejor = null
        let distancia = 60
        for (let i = 0; i < lista.length; ++i) {
            const c = lista[i]
            if (c.track !== pista || !finales[c.start])
                continue
            const d = Math.abs(c.start * escala - xContenido)
            if (d < distancia) {
                distancia = d
                mejor = { track: pista, cut: c.start }
            }
        }
        return mejor
    }

    // Índice de la pista `desplazamiento` posiciones más allá dentro de su tipo.
    function pistaDesplazada(indice, desplazamiento) {
        const pistas = editor.tracks
        const origen = pistas[indice]
        if (!origen)
            return indice
        const destino = pistas.find((p) => p.video === origen.video
                                    && p.number === origen.number + desplazamiento)
        return destino ? destino.index : indice
    }

    // Grupo de clips que se mueven juntos al arrastrar `clip`: la selección entera si
    // el clip está seleccionado, y siempre con sus enlazados.
    function grupoDe(clip) {
        let ids = []
        if (editor.selection.indexOf(clip.id) >= 0 || editor.selection.indexOf(clip.link) >= 0)
            ids = editor.selection.slice()
        else
            ids = [clip.id]
        const lista = editor.clips
        for (let i = 0; i < lista.length; ++i) {
            const c = lista[i]
            if (ids.indexOf(c.id) >= 0 && c.link !== 0 && ids.indexOf(c.link) < 0)
                ids.push(c.link)
        }
        return ids
    }

    // Imán: el borde o cabezal más cercano a `frame` (a menos de 8 píxeles).
    function ajustar(frame, excluir) {
        if (!iman)
            return frame
        let mejor = frame
        let distancia = 8 / escala
        const probar = (f) => {
            const d = Math.abs(f - frame)
            if (d <= distancia) {
                distancia = d
                mejor = f
            }
        }
        probar(0)
        probar(editor.currentFrame)
        const lista = editor.clips
        for (let i = 0; i < lista.length; ++i) {
            const c = lista[i]
            if (excluir.indexOf(c.id) >= 0)
                continue
            probar(c.start)
            probar(c.start + c.length)
        }
        return mejor
    }

    function zoomEn(factor, xVista) {
        const frame = (vista.contentX + xVista) / escala
        escala = Math.max(escalaMinima, Math.min(escalaMaxima, escala * factor))
        vista.contentX = Math.max(0, frame * escala - xVista)
    }

    function encajar() {
        escala = escalaMinima
        vista.contentX = 0
    }

    function acercar() { zoomEn(1.5, cabezalEnVista()) }
    function alejar() { zoomEn(1 / 1.5, cabezalEnVista()) }
    function cabezalEnVista() {
        const x = editor.currentFrame * escala - vista.contentX
        return x >= 0 && x <= vista.width ? x : vista.width / 2
    }

    function desplazarPistas(d) {
        vista.contentY = Math.max(0, Math.min(vista.contentHeight - vista.height,
                                              vista.contentY - d))
    }

    function textoVolumen(db) {
        if (db <= -60)
            return "−∞ dB"
        return (db > 0 ? "+" : db < 0 ? "−" : "") + Math.abs(db).toFixed(1) + " dB"
    }

    // Al añadir el primer clip, ver la línea de tiempo entera.
    property real duracionAnterior: 0
    Connections {
        target: raiz.editor
        function onFrameCountChanged() {
            if (raiz.duracionAnterior === 0 && raiz.editor.frameCount > 0)
                Qt.callLater(raiz.encajar)
            raiz.duracionAnterior = raiz.editor.frameCount
        }
        // Durante la reproducción, la vista sigue al cabezal.
        function onCurrentFrameChanged() {
            if (!raiz.editor.playing)
                return
            const x = raiz.editor.currentFrame * raiz.escala
            if (x > vista.contentX + vista.width - 30 || x < vista.contentX)
                vista.contentX = Math.max(0, x - 40)
        }
    }
    onAltoPistasChanged: Qt.callLater(() => raiz.desplazarPistas(0))

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.margen
        spacing: 6

        // --- Herramientas, en vertical ------------------------------------------

        ColumnLayout {
            id: herramientas
            Layout.fillHeight: true
            spacing: 4

            BotonIcono {
                icono: "seleccion"
                ayuda: "Selección (V): mover clips (también a otra pista) y recortarlos"
                activo: raiz.herramienta === "seleccion"
                onPulsado: raiz.herramienta = "seleccion"
            }
            BotonIcono {
                icono: "cuchilla"
                ayuda: "Cuchilla (C): dividir donde hagas clic"
                activo: raiz.herramienta === "cuchilla"
                onPulsado: raiz.herramienta = "cuchilla"
            }
            BotonIcono {
                icono: "iman"
                ayuda: "Imán (S): pegar a bordes y al cabezal"
                activo: raiz.iman
                onPulsado: raiz.iman = !raiz.iman
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 22
                implicitHeight: 1
                color: Theme.borde
            }
            BotonIcono {
                icono: "dividir"
                ayuda: "Dividir en el cabezal (Ctrl+K)"
                enabled: raiz.editor.frameCount > 0
                onPulsado: raiz.editor.splitAtPlayhead()
            }
            BotonIcono {
                icono: "quitar"
                ayuda: "Quitar la selección (Supr)"
                enabled: raiz.editor.selection.length > 0
                onPulsado: raiz.editor.removeSelection()
            }
            BotonIcono {
                icono: "deshacer"
                ayuda: raiz.editor.undoText + " (Ctrl+Z)"
                enabled: raiz.editor.canUndo
                onPulsado: raiz.editor.undo()
            }
            BotonIcono {
                icono: "rehacer"
                ayuda: raiz.editor.redoText + " (Ctrl+Mayús+Z)"
                enabled: raiz.editor.canRedo
                onPulsado: raiz.editor.redo()
            }
            Item { Layout.fillHeight: true }
            BotonIcono { icono: "acercar"; ayuda: "Acercar (+)"; onPulsado: raiz.acercar() }
            BotonIcono { icono: "alejar"; ayuda: "Alejar (-)"; onPulsado: raiz.alejar() }
            BotonIcono { icono: "ajustar"; ayuda: "Ver todo (\\)"; onPulsado: raiz.encajar() }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: Theme.borde
        }

        // --- Pistas -------------------------------------------------------------

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Cabeceras de pista (se desplazan en vertical con las pistas)
                Item {
                    id: cabeceras
                    Layout.preferredWidth: raiz.anchoCabecera
                    Layout.fillHeight: true
                    clip: true

                    Item {
                        y: -vista.contentY
                        width: parent.width
                        height: vista.contentHeight
                        Repeater {
                            model: raiz.filas
                            delegate: CabeceraPista {
                                required property var modelData
                                pista: modelData.pista
                                y: raiz.yPrimeraFila + modelData.y
                                height: raiz.alturaPista
                            }
                        }
                    }

                    // Añadir pistas (fijo arriba, a la altura de la regla)
                    Rectangle {
                        width: parent.width
                        height: raiz.alturaRegla
                        color: Theme.panel
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            BotonIcono {
                                texto: "+V"
                                ayuda: "Añadir una pista de video encima"
                                implicitWidth: 40
                                implicitHeight: 22
                                onPulsado: raiz.editor.addTrack("video")
                            }
                            BotonIcono {
                                texto: "+A"
                                ayuda: "Añadir una pista de audio debajo"
                                implicitWidth: 40
                                implicitHeight: 22
                                onPulsado: raiz.editor.addTrack("audio")
                            }
                        }
                    }

                    WheelHandler {
                        acceptedModifiers: Qt.NoModifier
                        onWheel: (e) => raiz.desplazarPistas(e.angleDelta.y)
                    }
                }

                Flickable {
                    id: vista
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    interactive: false
                    contentWidth: Math.max(width, (raiz.editor.frameCount + raiz.editor.fps * 30)
                                                  * raiz.escala)
                    contentHeight: Math.max(height, raiz.yPrimeraFila + raiz.altoPistas + 4)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOn }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Item {
                        id: contenido
                        width: vista.contentWidth
                        height: vista.contentHeight

                        // Fondo de cada pista: clic en vacío = quitar la selección.
                        Repeater {
                            model: raiz.filas
                            delegate: Rectangle {
                                required property var modelData
                                y: raiz.yPrimeraFila + modelData.y
                                width: contenido.width
                                height: raiz.alturaPista
                                color: raiz.pistaSoltar === modelData.pista.index
                                       ? Qt.tint(Theme.fondo, Qt.rgba(Theme.acento.r, Theme.acento.g,
                                                                      Theme.acento.b, 0.14))
                                       : Theme.fondo
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onPressed: (m) => {
                                        raiz.editor.clearSelection()
                                        if (m.button === Qt.RightButton)
                                            menuPista.abrir(parent.modelData.pista)
                                    }
                                }
                            }
                        }

                        // Clips
                        Repeater {
                            model: raiz.editor.clips
                            delegate: Rectangle {
                                id: elemento

                                required property var modelData
                                readonly property bool esVideo: modelData.video
                                readonly property var pista: raiz.editor.tracks[modelData.track]
                                readonly property bool bloqueado: pista !== undefined && pista.locked
                                readonly property bool apagado: pista !== undefined
                                    && (esVideo ? pista.hidden : pista.muted)
                                readonly property bool seleccionado:
                                    raiz.editor.selection.indexOf(modelData.id) >= 0
                                    || (modelData.link !== 0
                                        && raiz.editor.selection.indexOf(modelData.link) >= 0)
                                readonly property bool enArrastre: raiz.arrastre !== null
                                    && raiz.arrastre.grupo.indexOf(modelData.id) >= 0
                                readonly property real inicioVista: !enArrastre ? modelData.start
                                    : raiz.arrastre.tipo === "mover" ? modelData.start + raiz.arrastre.delta
                                    : raiz.arrastre.tipo === "inicio" ? raiz.arrastre.valor
                                    : modelData.start
                                readonly property real finVista: !enArrastre ? modelData.start + modelData.length
                                    : raiz.arrastre.tipo === "mover"
                                      ? modelData.start + modelData.length + raiz.arrastre.delta
                                    : raiz.arrastre.tipo === "final" ? raiz.arrastre.valor
                                    : modelData.start + modelData.length
                                // Fotograma del medio al principio de lo que se ve del clip.
                                readonly property real entradaVista: enArrastre
                                    && raiz.arrastre.tipo === "inicio"
                                    ? modelData.in + raiz.arrastre.valor - modelData.start
                                    : modelData.in
                                readonly property int pistaVista: enArrastre
                                    && raiz.arrastre.tipo === "mover" && raiz.arrastre.pistas !== 0
                                    && raiz.arrastre.video === esVideo
                                    ? raiz.pistaDesplazada(modelData.track, raiz.arrastre.pistas)
                                    : modelData.track

                                x: inicioVista * raiz.escala
                                y: raiz.yDePista(pistaVista)
                                z: enArrastre ? 2 : 1
                                width: Math.max(2, (finVista - inicioVista) * raiz.escala)
                                height: raiz.alturaPista
                                radius: 3
                                color: {
                                    const base = esVideo ? Theme.clipVideo : Theme.clipAudio
                                    return seleccionado ? Qt.lighter(base, 1.3) : base
                                }
                                opacity: enArrastre && raiz.arrastre.tipo === "mover" ? 0.85
                                         : apagado ? 0.45 : 1
                                border.color: seleccionado ? Theme.acento : Qt.lighter(color, 1.25)
                                border.width: seleccionado ? 2 : 1
                                clip: true // el nombre no se sale del clip

                                // Onda del audio: solo la parte visible del clip.
                                WaveformView {
                                    visible: !elemento.esVideo && width > 0
                                    readonly property real izquierda:
                                        Math.max(0, vista.contentX - elemento.x)
                                    readonly property real derecha:
                                        Math.min(elemento.width, vista.contentX + vista.width - elemento.x)
                                    x: izquierda
                                    y: 15
                                    width: Math.max(0, derecha - izquierda)
                                    height: elemento.height - 16
                                    editor: raiz.editor
                                    mediaId: elemento.modelData.media
                                    firstFrame: elemento.entradaVista + izquierda / raiz.escala
                                    framesPerPixel: 1 / raiz.escala
                                    fps: raiz.editor.fps
                                    gainDb: elemento.modelData.volumeDb
                                    color: Theme.clipAudioClaro

                                    // Mientras se calcula la onda (un VOD largo tarda unos segundos).
                                    Row {
                                        id: cargandoOnda
                                        visible: parent.loading && parent.width > 40
                                        x: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6
                                        Repeater {
                                            model: 3
                                            delegate: Rectangle {
                                                required property int index
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 3
                                                height: 12
                                                radius: 1
                                                color: Theme.clipAudioClaro
                                                SequentialAnimation on height {
                                                    running: cargandoOnda.visible
                                                    loops: Animation.Infinite
                                                    PauseAnimation { duration: index * 120 }
                                                    NumberAnimation { from: 4; to: 14; duration: 300; easing.type: Easing.InOutQuad }
                                                    NumberAnimation { from: 14; to: 4; duration: 300; easing.type: Easing.InOutQuad }
                                                    PauseAnimation { duration: (2 - index) * 120 }
                                                }
                                            }
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "Calculando la onda…"
                                            color: Theme.texto
                                            font.pixelSize: Theme.tamTextoPequeno
                                        }
                                    }
                                }

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.leftMargin: 5
                                    anchors.rightMargin: 5
                                    anchors.topMargin: 2
                                    spacing: 4
                                    visible: elemento.width > 30
                                    Text {
                                        Layout.fillWidth: true
                                        text: (elemento.esVideo ? "" : "♪ ") + elemento.modelData.name
                                        color: Theme.texto
                                        font.pixelSize: Theme.tamTextoPequeno
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: !elemento.esVideo && elemento.modelData.volumeDb !== 0
                                        text: raiz.textoVolumen(elemento.modelData.volumeDb)
                                        color: Theme.texto
                                        font.pixelSize: 10
                                    }
                                    Icono {
                                        visible: elemento.esVideo && elemento.modelData.transformed
                                        nombre: "rellenar"
                                        tam: 11
                                        color: Theme.texto
                                    }
                                }

                                // Pista bloqueada: rayado discreto, sin edición.
                                Rectangle {
                                    anchors.fill: parent
                                    visible: elemento.bloqueado
                                    color: Theme.fondo
                                    opacity: 0.35
                                }

                                // Cuerpo: seleccionar, mover o (con la cuchilla) dividir.
                                MouseArea {
                                    id: cuerpo
                                    anchors.fill: parent
                                    enabled: !elemento.bloqueado
                                    preventStealing: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    cursorShape: raiz.herramienta === "cuchilla" ? Qt.IBeamCursor
                                                                                 : Qt.ArrowCursor
                                    property real xInicial: 0
                                    property real yInicial: 0

                                    onPressed: (m) => {
                                        const p = mapToItem(contenido, m.x, m.y)
                                        if (m.button === Qt.RightButton) {
                                            if (!elemento.seleccionado)
                                                raiz.editor.select(elemento.modelData.id, false)
                                            menuClip.abrir(elemento.modelData)
                                            return
                                        }
                                        if (raiz.herramienta === "cuchilla") {
                                            const f = raiz.ajustar(raiz.frameEn(p.x), [])
                                            Qt.callLater(raiz.editor.splitClip, elemento.modelData.id, f)
                                            return
                                        }
                                        const sumar = (m.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                                        if (sumar || !elemento.seleccionado)
                                            raiz.editor.select(elemento.modelData.id, sumar)
                                        xInicial = p.x
                                        yInicial = p.y
                                        raiz.arrastre = null
                                    }
                                    onPositionChanged: (m) => {
                                        if (!pressed || raiz.herramienta !== "seleccion"
                                                || !(pressedButtons & Qt.LeftButton))
                                            return
                                        const p = mapToItem(contenido, m.x, m.y)
                                        const dx = p.x - xInicial
                                        const dy = p.y - yInicial
                                        if (raiz.arrastre === null && Math.abs(dx) < 4 && Math.abs(dy) < 10)
                                            return
                                        const c = elemento.modelData
                                        const grupo = raiz.grupoDe(c)
                                        let inicio = Number.MAX_VALUE
                                        let fin = 0
                                        const lista = raiz.editor.clips
                                        for (let i = 0; i < lista.length; ++i) {
                                            if (grupo.indexOf(lista[i].id) >= 0) {
                                                inicio = Math.min(inicio, lista[i].start)
                                                fin = Math.max(fin, lista[i].start + lista[i].length)
                                            }
                                        }
                                        let delta = Math.round(dx / raiz.escala)
                                        // Imán: el borde (inicial o final) más cercano manda.
                                        const porInicio = raiz.ajustar(inicio + delta, grupo) - inicio
                                        const porFin = raiz.ajustar(fin + delta, grupo) - fin
                                        if (porInicio !== delta && (porFin === delta
                                                || Math.abs(porInicio - delta) <= Math.abs(porFin - delta)))
                                            delta = porInicio
                                        else if (porFin !== delta)
                                            delta = porFin
                                        delta = raiz.editor.limitMove(c.id, delta)
                                        // Cambio de pista: la del mismo tipo bajo el ratón.
                                        let pistas = 0
                                        const destino = raiz.pistaEnY(p.y)
                                        const origen = raiz.editor.tracks[c.track]
                                        if (destino && origen && destino.video === origen.video)
                                            pistas = raiz.editor.limitTrackShift(c.id, destino.number - origen.number)
                                        raiz.arrastre = { tipo: "mover", grupo: grupo, delta: delta, valor: 0,
                                                          pistas: pistas, video: elemento.esVideo }
                                    }
                                    onReleased: {
                                        const a = raiz.arrastre
                                        raiz.arrastre = null
                                        if (a !== null && a.tipo === "mover" && (a.delta !== 0 || a.pistas !== 0))
                                            Qt.callLater(raiz.editor.moveClips, elemento.modelData.id, a.delta, a.pistas)
                                    }
                                    onCanceled: raiz.arrastre = null
                                }

                                // Bordes: recortar el inicio o el final.
                                Repeater {
                                    model: ["inicio", "final"]
                                    delegate: MouseArea {
                                        id: borde
                                        required property string modelData
                                        readonly property string lado: modelData
                                        width: Math.min(8, elemento.width / 3)
                                        height: elemento.height
                                        anchors.left: lado === "inicio" ? elemento.left : undefined
                                        anchors.right: lado === "final" ? elemento.right : undefined
                                        visible: raiz.herramienta === "seleccion" && !elemento.bloqueado
                                        preventStealing: true
                                        hoverEnabled: true
                                        cursorShape: Qt.SizeHorCursor
                                        property real xInicial: 0

                                        Rectangle {
                                            anchors.fill: parent
                                            color: Theme.acento
                                            opacity: borde.containsMouse || borde.pressed ? 0.5 : 0
                                        }
                                        onPressed: (m) => {
                                            raiz.editor.select(elemento.modelData.id, false)
                                            xInicial = mapToItem(contenido, m.x, 0).x
                                        }
                                        onPositionChanged: (m) => {
                                            if (!pressed)
                                                return
                                            const dx = mapToItem(contenido, m.x, 0).x - xInicial
                                            const c = elemento.modelData
                                            const grupo = c.link !== 0 ? [c.id, c.link] : [c.id]
                                            const base = lado === "inicio" ? c.start : c.start + c.length
                                            let f = raiz.ajustar(base + Math.round(dx / raiz.escala), grupo)
                                            f = lado === "inicio" ? raiz.editor.limitTrimStart(c.id, f)
                                                                  : raiz.editor.limitTrimEnd(c.id, f)
                                            raiz.arrastre = { tipo: lado, grupo: grupo, delta: 0, valor: f,
                                                              pistas: 0, video: elemento.esVideo }
                                        }
                                        onReleased: {
                                            const a = raiz.arrastre
                                            raiz.arrastre = null
                                            if (a === null)
                                                return
                                            if (lado === "inicio")
                                                Qt.callLater(raiz.editor.trimStart, elemento.modelData.id, a.valor)
                                            else
                                                Qt.callLater(raiz.editor.trimEnd, elemento.modelData.id, a.valor)
                                        }
                                        onCanceled: raiz.arrastre = null
                                    }
                                }
                            }
                        }

                        // Transiciones: sobre el corte, entre los dos clips. Clic para
                        // seleccionarla (Propiedades), bordes para cambiar su duración.
                        Repeater {
                            model: raiz.editor.transitions
                            delegate: Rectangle {
                                id: transicion
                                required property var modelData
                                readonly property bool seleccionada:
                                    raiz.editor.selectedTransition.id === modelData.id
                                readonly property bool enArrastre: raiz.arrastre !== null
                                    && raiz.arrastre.tipo === "transicion"
                                    && raiz.arrastre.id === modelData.id
                                readonly property real antes: enArrastre ? raiz.arrastre.antes
                                                                         : modelData.before
                                readonly property real despues: enArrastre ? raiz.arrastre.despues
                                                                           : modelData.after
                                readonly property real ancho: (antes + despues) * raiz.escala
                                // Con poco zoom, al menos 8 píxeles centrados en el corte.
                                x: ancho >= 8 ? (modelData.cut - antes) * raiz.escala
                                              : modelData.cut * raiz.escala - 4
                                width: Math.max(8, ancho)
                                y: raiz.yDePista(modelData.track) + 8
                                height: raiz.alturaPista - 16
                                z: 3
                                radius: 4
                                color: seleccionada ? Qt.lighter(Theme.clipTransicion, 1.25)
                                                    : Theme.clipTransicion
                                border.color: seleccionada ? Theme.acento : Qt.lighter(color, 1.3)
                                border.width: seleccionada ? 2 : 1
                                opacity: modelData.locked ? 0.6 : 0.95
                                clip: true

                                // Línea del corte
                                Rectangle {
                                    x: (transicion.modelData.cut * raiz.escala) - transicion.x - 0.5
                                    width: 1
                                    height: parent.height
                                    color: Theme.texto
                                    opacity: 0.35
                                }
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    visible: transicion.width > 44
                                    Icono {
                                        anchors.verticalCenter: parent.verticalCenter
                                        nombre: "transicion"
                                        tam: 12
                                        color: Theme.texto
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: (transicion.modelData.sound ? "♪ " : "")
                                              + transicion.modelData.name
                                        color: Theme.texto
                                        font.pixelSize: 10
                                        visible: transicion.width > 110
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !transicion.modelData.locked
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onPressed: (m) => {
                                        raiz.editor.selectTransition(transicion.modelData.id)
                                        if (m.button === Qt.RightButton)
                                            menuTransicion.abrir(transicion.modelData)
                                    }
                                }

                                // Bordes: cambiar lo que dura antes y después del corte.
                                Repeater {
                                    model: ["antes", "despues"]
                                    delegate: MouseArea {
                                        id: bordeTransicion
                                        required property string modelData
                                        readonly property string lado: modelData
                                        width: Math.min(7, transicion.width / 3)
                                        height: transicion.height
                                        anchors.left: lado === "antes" ? transicion.left : undefined
                                        anchors.right: lado === "despues" ? transicion.right : undefined
                                        visible: !transicion.modelData.locked
                                        preventStealing: true
                                        hoverEnabled: true
                                        cursorShape: Qt.SizeHorCursor
                                        Rectangle {
                                            anchors.fill: parent
                                            color: Theme.acento
                                            opacity: bordeTransicion.containsMouse || bordeTransicion.pressed ? 0.6 : 0
                                        }
                                        onPressed: raiz.editor.selectTransition(transicion.modelData.id)
                                        onPositionChanged: (m) => {
                                            if (!pressed)
                                                return
                                            const t = transicion.modelData
                                            const f = raiz.frameEn(mapToItem(contenido, m.x, 0).x)
                                            let antes = t.before
                                            let despues = t.after
                                            if (lado === "antes")
                                                antes = Math.max(0, Math.min(t.maxBefore, t.cut - f))
                                            else
                                                despues = Math.max(0, Math.min(t.maxAfter, f - t.cut))
                                            if (antes + despues < 2)
                                                return
                                            raiz.arrastre = { tipo: "transicion", id: t.id, grupo: [],
                                                              antes: antes, despues: despues,
                                                              delta: 0, valor: 0, pistas: 0 }
                                        }
                                        onReleased: {
                                            const a = raiz.arrastre
                                            raiz.arrastre = null
                                            if (a !== null && a.tipo === "transicion")
                                                Qt.callLater(raiz.editor.setTransitionLength, a.id,
                                                             a.antes, a.despues)
                                        }
                                        onCanceled: raiz.arrastre = null
                                    }
                                }
                            }
                        }

                        // Dónde caerá lo que se arrastra desde la biblioteca o el explorador.
                        Rectangle {
                            visible: raiz.marcaSoltar >= 0
                            x: raiz.marcaSoltar * raiz.escala - 1
                            y: raiz.yPrimeraFila
                            width: 2
                            height: raiz.altoPistas
                            color: Theme.acento
                            z: 4
                        }
                        // Dónde caerá una transición: el corte más cercano.
                        Rectangle {
                            visible: raiz.corteSoltar !== null
                            x: raiz.corteSoltar ? raiz.corteSoltar.cut * raiz.escala - 12 : 0
                            y: raiz.corteSoltar ? raiz.yDePista(raiz.corteSoltar.track) + 4 : 0
                            width: 24
                            height: raiz.alturaPista - 8
                            radius: 4
                            color: Theme.clipTransicion
                            border.color: Theme.acento
                            border.width: 2
                            z: 4
                        }

                        DropArea {
                            id: zonaSoltar
                            y: raiz.yPrimeraFila - raiz.separacion
                            width: parent.width
                            height: raiz.altoPistas + 2 * raiz.separacion
                            keys: ["sharkcut/medio", "sharkcut/transicion", "text/uri-list"]
                            function esTransicion(d) {
                                return d.keys.indexOf("sharkcut/transicion") >= 0
                            }
                            function actualizar(d) {
                                const p = raiz.pistaEnY(d.y + y)
                                if (esTransicion(d)) {
                                    raiz.corteSoltar = p && p.video ? raiz.corteCercano(p.index, d.x) : null
                                    return
                                }
                                raiz.marcaSoltar = raiz.ajustar(raiz.frameEn(d.x), [])
                                raiz.pistaSoltar = p ? p.index : -1
                            }
                            onPositionChanged: (d) => actualizar(d)
                            onEntered: (d) => actualizar(d)
                            onExited: {
                                raiz.marcaSoltar = -1
                                raiz.pistaSoltar = -1
                                raiz.corteSoltar = null
                            }
                            onDropped: (d) => {
                                const f = raiz.ajustar(raiz.frameEn(d.x), [])
                                const p = raiz.pistaEnY(d.y + y)
                                const pista = p ? p.index : -1
                                const corte = raiz.corteSoltar
                                raiz.marcaSoltar = -1
                                raiz.pistaSoltar = -1
                                raiz.corteSoltar = null
                                if (esTransicion(d)) {
                                    if (corte !== null && d.source)
                                        Qt.callLater(raiz.editor.addTransition, corte.track, corte.cut,
                                                     d.source.transicion)
                                    d.accept()
                                } else if (d.hasUrls && d.urls.length > 0) {
                                    raiz.editor.importFiles(d.urls, f, pista)
                                    d.acceptProposedAction()
                                } else if (d.source && d.source.mediaId !== undefined) {
                                    Qt.callLater(raiz.editor.addMedia, d.source.mediaId, f, pista)
                                    d.accept()
                                }
                            }
                        }

                        // Regla (fija arriba): clic o arrastre para mover el cabezal.
                        Rectangle {
                            id: regla
                            y: vista.contentY
                            z: 6
                            width: parent.width
                            height: raiz.alturaRegla
                            color: Theme.panel

                            // Distancia entre marcas: la primera que deja ~90 px entre etiquetas.
                            readonly property real paso: {
                                const fps = Math.max(1, Math.round(raiz.editor.fps))
                                const opciones = [1, 2, 5, 10, 15, 30, fps, 2 * fps, 5 * fps, 10 * fps,
                                                  15 * fps, 30 * fps, 60 * fps, 120 * fps, 300 * fps,
                                                  600 * fps, 900 * fps, 1800 * fps, 3600 * fps]
                                for (let i = 0; i < opciones.length; ++i) {
                                    if (opciones[i] * raiz.escala >= 90)
                                        return opciones[i]
                                }
                                return 7200 * fps
                            }
                            readonly property real primera: Math.floor(vista.contentX / raiz.escala / paso) * paso

                            Repeater {
                                model: Math.ceil(vista.width / (regla.paso * raiz.escala)) + 2
                                delegate: Item {
                                    required property int index
                                    readonly property real frame: regla.primera + index * regla.paso
                                    x: frame * raiz.escala
                                    height: regla.height
                                    Rectangle {
                                        width: 1
                                        height: 8
                                        anchors.bottom: parent.bottom
                                        color: Theme.textoDesactivado
                                    }
                                    Rectangle {
                                        x: regla.paso * raiz.escala / 2
                                        width: 1
                                        height: 4
                                        anchors.bottom: parent.bottom
                                        color: Theme.borde
                                    }
                                    Text {
                                        x: 3
                                        y: 2
                                        text: raiz.editor.timecodeFor(parent.frame)
                                        color: Theme.textoSecundario
                                        font.pixelSize: 10
                                        font.family: "Consolas, monospace"
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                onPressed: (m) => raiz.editor.seek(raiz.frameEn(m.x))
                                onPositionChanged: (m) => {
                                    if (pressed)
                                        raiz.editor.seek(raiz.frameEn(m.x))
                                }
                            }
                        }

                        // Cabezal de reproducción
                        Item {
                            x: raiz.editor.currentFrame * raiz.escala
                            y: vista.contentY
                            width: 1
                            height: vista.height
                            z: 7
                            Rectangle {
                                x: -1
                                width: 2
                                height: parent.height
                                color: Theme.cabezal
                            }
                            Rectangle {
                                x: -6
                                width: 12
                                height: 10
                                radius: 2
                                color: Theme.cabezal
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: raiz.editor.frameCount === 0
                horizontalAlignment: Text.AlignHCenter
                text: "Arrastra medios desde la biblioteca (o archivos desde el explorador) a una pista: "
                      + "el video a V1, la facecam a V2, la música a A2…"
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
                elide: Text.ElideRight
            }
        }
    }

    // Rueda: desplazar en el tiempo; con Alt, entre pistas; con Ctrl, zoom alrededor del ratón.
    WheelHandler {
        target: null
        onWheel: (e) => {
            const p = raiz.mapToItem(vista, e.x, e.y)
            if (e.modifiers & Qt.ControlModifier) {
                raiz.zoomEn(e.angleDelta.y > 0 ? 1.25 : 0.8, p.x)
            } else if (e.modifiers & Qt.AltModifier) {
                // Con Alt, algunos sistemas mandan el giro en el eje horizontal.
                raiz.desplazarPistas(e.angleDelta.y !== 0 ? e.angleDelta.y : e.angleDelta.x)
            } else {
                const d = e.angleDelta.x !== 0 ? e.angleDelta.x : e.angleDelta.y
                vista.contentX = Math.max(0, Math.min(vista.contentWidth - vista.width,
                                                      vista.contentX - d))
            }
        }
    }

    // --- Menús -------------------------------------------------------------------

    MenuTema {
        id: menuPista
        property var pista: null
        function abrir(p) {
            pista = p
            popup()
        }
        readonly property int delMismoTipo: pista === null ? 0
            : raiz.editor.tracks.filter((t) => t.video === pista.video).length
        Action {
            text: "Añadir pista de video"
            onTriggered: raiz.editor.addTrack("video")
        }
        Action {
            text: "Añadir pista de audio"
            onTriggered: raiz.editor.addTrack("audio")
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: menuPista.pista === null ? "Quitar pista"
                  : "Quitar la pista " + menuPista.pista.name
                    + (menuPista.pista.empty ? "" : " y sus clips")
            enabled: menuPista.delMismoTipo > 1
            onTriggered: raiz.editor.removeTrack(menuPista.pista.index)
        }
    }

    MenuTema {
        id: menuClip
        property var clipMenu: null
        function abrir(c) {
            clipMenu = c
            popup()
        }
        Action {
            text: "Desvincular audio y video"
            enabled: menuClip.clipMenu !== null && menuClip.clipMenu.link !== 0
            onTriggered: raiz.editor.unlinkSelection()
        }
        Action {
            text: "Ajustar al lienzo"
            enabled: menuClip.clipMenu !== null && menuClip.clipMenu.video
            onTriggered: raiz.editor.fitClip(menuClip.clipMenu.id, false)
        }
        Action {
            text: "Rellenar el lienzo"
            enabled: menuClip.clipMenu !== null && menuClip.clipMenu.video
            onTriggered: raiz.editor.fitClip(menuClip.clipMenu.id, true)
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Dividir en el cabezal"
            onTriggered: raiz.editor.splitAtPlayhead()
        }
        Action {
            text: "Quitar"
            onTriggered: raiz.editor.removeSelection()
        }
    }

    MenuTema {
        id: menuTransicion
        property var transicionMenu: null
        function abrir(t) {
            transicionMenu = t
            popup()
        }
        Action {
            text: "Quitar el sonido de la transición"
            enabled: menuTransicion.transicionMenu !== null && menuTransicion.transicionMenu.sound
            onTriggered: raiz.editor.setTransitionSound(menuTransicion.transicionMenu.id, 0)
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Quitar la transición"
            onTriggered: raiz.editor.removeTransition(menuTransicion.transicionMenu.id)
        }
    }

    component MenuTema: Menu {
        id: menu
        padding: 4
        background: Rectangle {
            implicitWidth: 230
            color: Theme.panelElevado
            border.color: Theme.borde
            radius: 4
        }
        delegate: MenuItem {
            id: opcion
            implicitHeight: 28
            contentItem: Text {
                leftPadding: 6
                text: opcion.text
                color: opcion.enabled ? Theme.texto : Theme.textoDesactivado
                font.pixelSize: Theme.tamTexto
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 3
                color: opcion.highlighted ? Theme.borde : "transparent"
            }
        }
    }

    // Cabecera de una pista: nombre, ocultar o silenciar, y bloquear.
    component CabeceraPista: Rectangle {
        id: cabecera
        property var pista
        width: raiz.anchoCabecera - 6
        radius: 3
        color: Theme.panelElevado
        border.color: Theme.borde

        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: 9
            text: cabecera.pista.name
            color: cabecera.pista.locked ? Theme.textoSecundario : Theme.texto
            font.pixelSize: Theme.tamTexto
            font.bold: true
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: menuPista.abrir(cabecera.pista)
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 5
            spacing: 2
            BotonPista {
                icono: cabecera.pista.video ? (cabecera.pista.hidden ? "ojoCerrado" : "ojo")
                                            : (cabecera.pista.muted ? "silencio" : "sonido")
                aviso: cabecera.pista.video ? cabecera.pista.hidden : cabecera.pista.muted
                ayuda: cabecera.pista.video
                       ? (cabecera.pista.hidden ? "Mostrar la pista" : "Ocultar la pista (no se ve ni se exporta)")
                       : (cabecera.pista.muted ? "Activar el sonido de la pista" : "Silenciar la pista (no suena ni se exporta)")
                onPulsado: cabecera.pista.video
                           ? raiz.editor.setTrackHidden(cabecera.pista.index, !cabecera.pista.hidden)
                           : raiz.editor.setTrackMuted(cabecera.pista.index, !cabecera.pista.muted)
            }
            BotonPista {
                icono: cabecera.pista.locked ? "candado" : "candadoAbierto"
                aviso: cabecera.pista.locked
                ayuda: cabecera.pista.locked ? "Desbloquear la pista"
                                             : "Bloquear la pista (sus clips no se pueden editar)"
                onPulsado: raiz.editor.setTrackLocked(cabecera.pista.index, !cabecera.pista.locked)
            }
        }
    }

    // Interruptor pequeño de la cabecera de pista. `aviso`: estado no habitual (oculta,
    // silenciada, bloqueada), en color de aviso para que se note.
    component BotonPista: Rectangle {
        id: boton
        property string icono
        property string ayuda
        property bool aviso: false
        signal pulsado()
        width: 24
        height: 24
        radius: 4
        color: zonaBoton.containsMouse ? Theme.borde : "transparent"
        Icono {
            anchors.centerIn: parent
            nombre: boton.icono
            tam: 14
            color: boton.aviso ? Theme.aviso : Theme.textoSecundario
        }
        MouseArea {
            id: zonaBoton
            anchors.fill: parent
            hoverEnabled: true
            onClicked: boton.pulsado()
        }
        ToolTip {
            id: ayudaBoton
            visible: zonaBoton.containsMouse
            delay: 500
            text: boton.ayuda
            contentItem: Text {
                text: ayudaBoton.text
                color: Theme.texto
                font.pixelSize: Theme.tamTextoPequeno
            }
            background: Rectangle { color: Theme.panelElevado; border.color: Theme.borde; radius: 4 }
        }
    }
}
