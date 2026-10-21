import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import SharkCut

// Línea de tiempo del editor: herramientas en vertical a la izquierda, regla y pistas
// (las de video de la más alta a V1, y debajo las de audio de A1 hacia abajo).
//
// Herramientas: Selección (mover clips, también a otra pista, y recortar sus bordes) y
// Cuchilla (dividir donde se hace clic). Mientras se arrastra solo se muestra una vista
// previa; la edición se aplica al soltar, como un único paso de deshacer.
//
// Cabecera de cada pista: ocultar (video) o silenciar (audio) y bloquear. Clic derecho:
// añadir o quitar pistas (y, sobre un hueco, cerrarlo). Rueda: desplazar en el tiempo;
// sobre las cabeceras o con Alt, desplazar entre pistas; con Ctrl, zoom.
//
// Regla: marcadores (M). Encima de la última pista de video y debajo de la última de
// audio hay una zona vacía: soltar ahí un medio crea pistas nuevas para él.
Rectangle {
    id: raiz

    required property Editor editor
    property string herramienta: "seleccion" // "seleccion" o "cuchilla"
    property bool iman: true                 // ajustar a bordes y cabezal al arrastrar
    signal nuevaSecuencia()
    signal exportarSrt(int pista)          // pide el archivo y exporta la pista S<n>
    signal dividirSubtitulos(int pista)    // abre las opciones para volver a dividirla

    // Vista previa del arrastre en curso:
    // {tipo: "mover" | "inicio" | "final", grupo: [ids], delta, valor, pistas, video}
    // (`pistas`: cambio de pista de los clips del tipo `video` al mover)
    property var arrastre: null
    property real marcaSoltar: -1 // fotograma donde caería lo que se arrastra encima
    property int pistaSoltar: -1  // pista donde caería
    // Zona vacía donde se suelta un medio para crear pistas nuevas: "", "video" (encima de
    // la última de video) o "audio" (debajo de la última de audio).
    property string zonaNueva: ""
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
    // Zona vacía encima y debajo de las pistas (soltar ahí crea pistas nuevas; debajo
    // vale también todo el espacio libre hasta el final de la vista).
    readonly property int bandaNueva: 30
    readonly property int alturaPestanas: 30
    readonly property int alturaSubtitulo: 34

    // Pistas de subtítulos, arriba del todo (la más alta primero): {pista, y}.
    readonly property var filasSubtitulos: {
        const lista = []
        let y = 0
        for (const p of editor.captionTracks.slice().reverse()) {
            lista.push({ pista: p, y: y })
            y += alturaSubtitulo + separacion
        }
        return lista
    }
    readonly property real altoSubtitulos: filasSubtitulos.length > 0
        ? filasSubtitulos.length * (alturaSubtitulo + separacion) - separacion + separacionGrupos : 0
    readonly property var yPorSubtitulo: {
        const mapa = {}
        for (const f of filasSubtitulos)
            mapa[f.pista.index] = f.y
        return mapa
    }
    function yDeSubtitulo(indice) {
        return yPrimeraFila + (yPorSubtitulo[indice] !== undefined ? yPorSubtitulo[indice] : 0)
    }
    // Arrastre de subtítulos: {tipo: "mover" | "inicio" | "final", id, grupo, delta, valor}
    property var arrastreSub: null

    // Filas en pantalla, de arriba abajo: {pista, y} (y relativa a la primera fila).
    readonly property var filas: {
        const pistas = editor.tracks
        const videos = pistas.filter((p) => p.video).reverse()
        const audios = pistas.filter((p) => !p.video)
        const lista = []
        let y = altoSubtitulos + bandaNueva
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
        ? filas[filas.length - 1].y + alturaPista + bandaNueva : 0
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
    implicitHeight: Math.max(alturaPestanas + 4 + yPrimeraFila
                             + Math.min(altoPistas, filasVisibles * (alturaPista + separacion)
                                                    + separacionGrupos) + 20,
                             herramientas.implicitHeight) + 2 * Theme.margen

    // Cuadro de selección (arrastrar desde una zona vacía de las pistas), en coordenadas
    // del contenido: {x0, y0, x1, y1}; null si no se está dibujando.
    property var marcoSeleccion: null
    property point inicioMarco
    function empezarMarco(p) {
        inicioMarco = p
        marcoSeleccion = null
    }
    // Devuelve si ya es un cuadro (se movió lo bastante) para no mover el cabezal.
    function moverMarco(p) {
        if (marcoSeleccion === null && Math.hypot(p.x - inicioMarco.x, p.y - inicioMarco.y) < 5)
            return false
        marcoSeleccion = { x0: Math.min(inicioMarco.x, p.x), y0: Math.min(inicioMarco.y, p.y),
                           x1: Math.max(inicioMarco.x, p.x), y1: Math.max(inicioMarco.y, p.y) }
        return true
    }
    // Selecciona lo que toca el cuadro (sumándolo con Mayús o Ctrl).
    function soltarMarco(modificadores) {
        const m = marcoSeleccion
        marcoSeleccion = null
        if (m === null)
            return false
        const pistas = []
        for (const f of filas) {
            const y = yPrimeraFila + f.y
            if (y < m.y1 && y + alturaPista > m.y0)
                pistas.push(f.pista.index)
        }
        const subtitulos = []
        for (const f of filasSubtitulos) {
            const y = yPrimeraFila + f.y
            if (y < m.y1 && y + alturaSubtitulo > m.y0)
                subtitulos.push(f.pista.index)
        }
        const sumar = (modificadores & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
        editor.selectArea(pistas, subtitulos, Math.floor(m.x0 / escala), Math.ceil(m.x1 / escala),
                          sumar)
        return true
    }

    // --- Utilidades --------------------------------------------------------------

    // Clic para llevar el cabezal: la reproducción se pausa (petición de Kyo).
    function llevarCabezal(frame) {
        editor.pause()
        editor.seek(frame)
    }

    function frameEn(xContenido) {
        return Math.max(0, Math.round(xContenido / escala))
    }

    function yDePista(indice) {
        return yPrimeraFila + (yPorPista[indice] !== undefined ? yPorPista[indice] : 0)
    }

    // Zona de pistas nuevas bajo una coordenada y del contenido: "video", "audio" o "".
    function zonaNuevaEn(yContenido) {
        if (filas.length === 0)
            return ""
        const y = yContenido - yPrimeraFila
        if (y < altoSubtitulos)
            return ""
        if (y < filas[0].y - separacion)
            return "video"
        if (y > filas[filas.length - 1].y + alturaPista + separacion)
            return "audio"
        return ""
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
        const corte = editor.nearestCut(pista, Math.round(xContenido / escala), 60 / escala)
        return corte.cut === undefined ? null : corte
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
        return editor.clipGroup(clip.id)
    }

    // Imán: el borde, marcador o cabezal más cercano a `frame` (a menos de 8 píxeles).
    function ajustar(frame, excluir) {
        if (!iman)
            return frame
        return editor.snapFrame(frame, excluir, 8 / escala)
    }

    function zoomEn(factor, xVista) {
        const frame = (vista.contentX + xVista) / escala
        escala = Math.max(escalaMinima, Math.min(escalaMaxima, escala * factor))
        vista.contentX = frame * escala - xVista
        limitarVista()
    }

    // La vista nunca se queda más allá del contenido (al alejar estando al final, la
    // barra desaparece y no habría forma de volver).
    function limitarVista() {
        const maximo = Math.max(0, vista.contentWidth - vista.width)
        if (vista.contentX > maximo || vista.contentX < 0)
            vista.contentX = Math.max(0, Math.min(maximo, vista.contentX))
    }

    // Fotograma para el cabezal en la x del contenido: con el imán, se pega a cortes y
    // marcadores (a menos de 8 píxeles).
    function cabezalEn(xContenido) {
        const frame = frameEn(xContenido)
        return iman ? editor.snapFrame(frame, [], 8 / escala, false) : frame
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

    property bool seguirCabezal: true
    Connections {
        target: raiz.editor
        function onPlayingChanged() {
            if (raiz.editor.playing)
                raiz.seguirCabezal = true
        }
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
        // Durante la reproducción, la vista sigue al cabezal (salvo si se movió la vista
        // con la rueda: entonces se deja donde está hasta volver a reproducir).
        function onCurrentFrameChanged() {
            if (!raiz.editor.playing || !raiz.seguirCabezal)
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

            // Pestañas de las secuencias abiertas (de cualquier proyecto abierto)
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.preferredHeight: raiz.alturaPestanas
                Layout.maximumHeight: raiz.alturaPestanas
                spacing: 4
                ListView {
                    id: pestanas
                    Layout.fillWidth: true
                    Layout.maximumWidth: contentWidth
                    Layout.preferredHeight: raiz.alturaPestanas
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: raiz.editor.tabs
                    readonly property bool variosProyectos: raiz.editor.projects.length > 1
                    delegate: PestanaSecuencia {
                        required property var modelData
                        pestana: modelData
                        conProyecto: pestanas.variosProyectos
                    }
                }
                BotonIcono {
                    icono: "mas"
                    ayuda: "Nueva secuencia"
                    implicitWidth: 28
                    implicitHeight: 26
                    onPulsado: raiz.nuevaSecuencia()
                }
                Item { Layout.fillWidth: true }
            }

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
                        Repeater {
                            model: raiz.filasSubtitulos
                            delegate: CabeceraSubtitulos {
                                required property var modelData
                                pista: modelData.pista
                                y: raiz.yPrimeraFila + modelData.y
                                height: raiz.alturaSubtitulo
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
                                implicitWidth: 27
                                implicitHeight: 22
                                onPulsado: raiz.editor.addTrack("video")
                            }
                            BotonIcono {
                                texto: "+A"
                                ayuda: "Añadir una pista de audio debajo"
                                implicitWidth: 27
                                implicitHeight: 22
                                onPulsado: raiz.editor.addTrack("audio")
                            }
                            BotonIcono {
                                texto: "+S"
                                ayuda: "Añadir una pista de subtítulos (arriba)"
                                implicitWidth: 27
                                implicitHeight: 22
                                onPulsado: raiz.editor.addCaptionTrack()
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
                    ScrollBar.horizontal: BarraDesplazamiento {}
                    ScrollBar.vertical: BarraDesplazamiento {}
                    onContentWidthChanged: Qt.callLater(raiz.limitarVista)
                    onWidthChanged: Qt.callLater(raiz.limitarVista)

                    Item {
                        id: contenido
                        width: vista.contentWidth
                        height: vista.contentHeight

                        // Fondo de cada pista: clic en vacío = quitar la selección y llevar
                        // el cabezal ahí; arrastrar = cuadro de selección.
                        Repeater {
                            model: raiz.filas
                            delegate: Rectangle {
                                required property var modelData
                                y: raiz.yPrimeraFila + modelData.y
                                width: contenido.width
                                height: raiz.alturaPista
                                readonly property color base: modelData.pista.video
                                                              ? Theme.pistaVideo : Theme.pistaAudio
                                color: raiz.pistaSoltar === modelData.pista.index
                                       ? Qt.tint(base, Qt.rgba(Theme.acento.r, Theme.acento.g,
                                                               Theme.acento.b, 0.14))
                                       : base
                                // Clic: quitar la selección y el cabezal ahí. Arrastrar:
                                // cuadro de selección (con Mayús o Ctrl, se suma).
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    preventStealing: true
                                    onPressed: (m) => {
                                        if (m.button === Qt.RightButton) {
                                            raiz.editor.clearSelection()
                                            menuPista.abrir(parent.modelData.pista, raiz.frameEn(m.x))
                                            return
                                        }
                                        raiz.empezarMarco(mapToItem(contenido, m.x, m.y))
                                    }
                                    onPositionChanged: (m) => {
                                        if (pressedButtons & Qt.LeftButton)
                                            raiz.moverMarco(mapToItem(contenido, m.x, m.y))
                                    }
                                    onReleased: (m) => {
                                        if (m.button !== Qt.LeftButton || raiz.soltarMarco(m.modifiers))
                                            return
                                        raiz.editor.clearSelection()
                                        raiz.llevarCabezal(raiz.cabezalEn(m.x))
                                    }
                                    onCanceled: raiz.marcoSeleccion = null
                                }
                            }
                        }

                        // Fondo de las pistas de subtítulos: clic = cabezal ahí; arrastrar =
                        // cuadro de selección; doble clic = subtítulo nuevo ahí.
                        Repeater {
                            model: raiz.filasSubtitulos
                            delegate: Rectangle {
                                required property var modelData
                                y: raiz.yPrimeraFila + modelData.y
                                width: contenido.width
                                height: raiz.alturaSubtitulo
                                color: Theme.pistaVideo
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    preventStealing: true
                                    onPressed: (m) => {
                                        if (m.button === Qt.RightButton) {
                                            raiz.editor.clearSelection()
                                            menuSubtitulos.abrir(parent.modelData.pista)
                                            return
                                        }
                                        raiz.empezarMarco(mapToItem(contenido, m.x, m.y))
                                    }
                                    onPositionChanged: (m) => {
                                        if (pressedButtons & Qt.LeftButton)
                                            raiz.moverMarco(mapToItem(contenido, m.x, m.y))
                                    }
                                    onReleased: (m) => {
                                        if (m.button !== Qt.LeftButton || raiz.soltarMarco(m.modifiers))
                                            return
                                        raiz.editor.clearSelection()
                                        raiz.llevarCabezal(raiz.cabezalEn(m.x))
                                    }
                                    onCanceled: raiz.marcoSeleccion = null
                                    onDoubleClicked: raiz.editor.addCaptionAtPlayhead(parent.modelData.pista.index)
                                }
                            }
                        }

                        // Subtítulos (solo los de la parte visible, como los clips).
                        Repeater {
                            model: TimelineClipsModel {
                                editor: raiz.editor
                                captions: true
                                firstFrame: vista.contentX / raiz.escala
                                lastFrame: (vista.contentX + vista.width) / raiz.escala
                                pixelsPerFrame: raiz.escala
                            }
                            delegate: Rectangle {
                                id: subtitulo
                                required property var datosClip
                                readonly property var dato: datosClip
                                readonly property bool bloque: dato.block === true
                                readonly property var pista: raiz.editor.captionTracks[dato.track]
                                readonly property bool bloqueado: pista !== undefined && pista.locked
                                readonly property bool seleccionado: !bloque
                                    && raiz.editor.captionSelection.indexOf(dato.id) >= 0
                                readonly property bool enArrastre: raiz.arrastreSub !== null
                                    && raiz.arrastreSub.grupo.indexOf(dato.id) >= 0
                                readonly property real inicioVista: !enArrastre ? dato.start
                                    : raiz.arrastreSub.tipo === "mover" ? dato.start + raiz.arrastreSub.delta
                                    : raiz.arrastreSub.tipo === "inicio" ? raiz.arrastreSub.valor
                                    : dato.start
                                readonly property real finVista: !enArrastre ? dato.start + dato.length
                                    : raiz.arrastreSub.tipo === "mover" ? dato.start + dato.length + raiz.arrastreSub.delta
                                    : raiz.arrastreSub.tipo === "final" ? raiz.arrastreSub.valor
                                    : dato.start + dato.length

                                x: inicioVista * raiz.escala
                                y: raiz.yDeSubtitulo(dato.track) + 3
                                z: enArrastre ? 2 : 1
                                width: Math.max(2, (finVista - inicioVista) * raiz.escala)
                                height: raiz.alturaSubtitulo - 6
                                radius: 3
                                color: seleccionado ? Qt.lighter(Theme.clipTexto, 1.3) : Theme.clipTexto
                                opacity: pista !== undefined && pista.hidden ? 0.45 : 1
                                border.color: seleccionado ? Theme.acento : Qt.lighter(color, 1.25)
                                border.width: seleccionado ? 2 : 1
                                clip: true

                                Text {
                                    visible: !subtitulo.bloque && subtitulo.width > 24
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 5
                                    width: parent.width - 10
                                    text: subtitulo.dato.text.replace(/\n/g, " ")
                                    color: Theme.texto
                                    font.pixelSize: Theme.tamTextoPequeno
                                    elide: Text.ElideRight
                                }
                                Loader {
                                    active: subtitulo.bloque && subtitulo.width > 160
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: Math.max(6, vista.contentX - subtitulo.x + 6)
                                    sourceComponent: Text {
                                        text: subtitulo.dato.count + " subtítulos · acércate para editarlos"
                                        color: Theme.texto
                                        opacity: 0.7
                                        font.pixelSize: Theme.tamTextoPequeno
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    visible: subtitulo.bloqueado
                                    color: Theme.fondo
                                    opacity: 0.35
                                }

                                // Cuerpo: seleccionar y mover.
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !subtitulo.bloqueado && !subtitulo.bloque
                                    preventStealing: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    property real xInicial: 0
                                    onPressed: (m) => {
                                        const sumar = (m.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                                        if (sumar || !subtitulo.seleccionado)
                                            raiz.editor.selectCaption(subtitulo.dato.id, sumar)
                                        if (m.button === Qt.RightButton) {
                                            menuSubtitulos.abrir(subtitulo.pista)
                                            return
                                        }
                                        xInicial = mapToItem(contenido, m.x, 0).x
                                        raiz.arrastreSub = null
                                    }
                                    onPositionChanged: (m) => {
                                        if (!pressed || !(pressedButtons & Qt.LeftButton))
                                            return
                                        const dx = mapToItem(contenido, m.x, 0).x - xInicial
                                        if (raiz.arrastreSub === null && Math.abs(dx) < 4)
                                            return
                                        const d = subtitulo.dato
                                        const grupo = raiz.editor.captionSelection.indexOf(d.id) >= 0
                                                      ? raiz.editor.captionSelection : [d.id]
                                        let delta = Math.round(dx / raiz.escala)
                                        // Imán: con el borde inicial o el final de este subtítulo.
                                        const porInicio = raiz.ajustar(d.start + delta, []) - d.start
                                        const porFin = raiz.ajustar(d.start + d.length + delta, []) - d.start - d.length
                                        if (porInicio !== delta && (porFin === delta
                                                || Math.abs(porInicio - delta) <= Math.abs(porFin - delta)))
                                            delta = porInicio
                                        else if (porFin !== delta)
                                            delta = porFin
                                        delta = raiz.editor.limitCaptionMove(d.id, delta)
                                        raiz.arrastreSub = { tipo: "mover", id: d.id, grupo: grupo,
                                                             delta: delta, valor: 0 }
                                    }
                                    onReleased: {
                                        const a = raiz.arrastreSub
                                        raiz.arrastreSub = null
                                        if (a !== null && a.delta !== 0)
                                            Qt.callLater(raiz.editor.moveCaptions, a.id, a.delta)
                                    }
                                    onCanceled: raiz.arrastreSub = null
                                }

                                // Bordes: cambiar cuándo empieza o acaba.
                                Repeater {
                                    model: subtitulo.bloque || subtitulo.width < 6 || subtitulo.bloqueado
                                           ? [] : ["inicio", "final"]
                                    delegate: MouseArea {
                                        id: bordeSub
                                        required property string modelData
                                        readonly property string lado: modelData
                                        width: Math.min(8, subtitulo.width / 3)
                                        height: subtitulo.height
                                        anchors.left: lado === "inicio" ? subtitulo.left : undefined
                                        anchors.right: lado === "final" ? subtitulo.right : undefined
                                        preventStealing: true
                                        hoverEnabled: true
                                        cursorShape: Qt.SizeHorCursor
                                        property real xInicial: 0
                                        Rectangle {
                                            anchors.fill: parent
                                            color: Theme.acento
                                            opacity: bordeSub.containsMouse || bordeSub.pressed ? 0.5 : 0
                                        }
                                        onPressed: (m) => {
                                            raiz.editor.selectCaption(subtitulo.dato.id, false)
                                            xInicial = mapToItem(contenido, m.x, 0).x
                                        }
                                        onPositionChanged: (m) => {
                                            if (!pressed)
                                                return
                                            const d = subtitulo.dato
                                            const dx = mapToItem(contenido, m.x, 0).x - xInicial
                                            const hueco = raiz.editor.captionRoom(d.id)
                                            const base = lado === "inicio" ? d.start : d.start + d.length
                                            let f = raiz.ajustar(base + Math.round(dx / raiz.escala), [])
                                            if (lado === "inicio")
                                                f = Math.max(hueco.low, Math.min(f, d.start + d.length - 1))
                                            else
                                                f = Math.max(d.start + 1, hueco.high >= 0 ? Math.min(f, hueco.high) : f)
                                            raiz.arrastreSub = { tipo: lado, id: d.id, grupo: [d.id], delta: 0, valor: f }
                                        }
                                        onReleased: {
                                            const a = raiz.arrastreSub
                                            raiz.arrastreSub = null
                                            if (a === null)
                                                return
                                            const d = subtitulo.dato
                                            if (lado === "inicio")
                                                Qt.callLater(raiz.editor.trimCaption, d.id, a.valor, d.start + d.length)
                                            else
                                                Qt.callLater(raiz.editor.trimCaption, d.id, d.start, a.valor)
                                        }
                                        onCanceled: raiz.arrastreSub = null
                                    }
                                }
                            }
                        }

                        // Zonas vacías de arriba y de abajo: al arrastrar un medio encima se
                        // ve que ahí se crea una pista nueva.
                        Repeater {
                            model: ["video", "audio"]
                            delegate: Rectangle {
                                required property string modelData
                                visible: raiz.zonaNueva === modelData
                                x: vista.contentX + 4
                                y: modelData === "video"
                                   ? raiz.yPrimeraFila + raiz.altoSubtitulos + 2
                                   : raiz.yPrimeraFila + raiz.altoPistas - raiz.bandaNueva + raiz.separacion
                                width: vista.width - 8
                                height: modelData === "video"
                                        ? raiz.bandaNueva - raiz.separacion - 2
                                        : Math.max(raiz.bandaNueva - raiz.separacion - 2,
                                                   vista.contentY + vista.height - y - 4)
                                radius: 3
                                color: Qt.rgba(Theme.acento.r, Theme.acento.g, Theme.acento.b, 0.12)
                                border.color: Theme.acento
                                Text {
                                    anchors.centerIn: parent
                                    text: parent.modelData === "video"
                                          ? "Suelta aquí para crear una pista de video nueva"
                                          : "Suelta aquí para crear una pista de audio nueva"
                                    color: Theme.acento
                                    font.pixelSize: Theme.tamTextoPequeno
                                }
                            }
                        }

                        // Clips
                        Repeater {
                            // Solo los clips de la parte visible (más media pantalla a cada lado).
                            model: TimelineClipsModel {
                                editor: raiz.editor
                                firstFrame: vista.contentX / raiz.escala
                                lastFrame: (vista.contentX + vista.width) / raiz.escala
                                pixelsPerFrame: raiz.escala
                            }
                            delegate: Rectangle {
                                id: elemento

                                required property var datosClip
                                readonly property var modelData: datosClip
                                // Varios clips demasiado estrechos para verse (vista alejada):
                                // una franja sin edición; al acercarse aparecen los clips.
                                readonly property bool bloque: modelData.block === true
                                readonly property bool esVideo: modelData.video
                                readonly property var pista: raiz.editor.tracks[modelData.track]
                                readonly property bool bloqueado: pista !== undefined && pista.locked
                                readonly property bool apagado: pista !== undefined
                                    && (esVideo ? pista.hidden : pista.muted)
                                readonly property bool seleccionado: !bloque && (
                                    raiz.editor.selection.indexOf(modelData.id) >= 0
                                    || (modelData.link !== 0
                                        && raiz.editor.selection.indexOf(modelData.link) >= 0))
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
                                    const base = elemento.modelData.effect ? Theme.clipTransicion
                                               : esVideo ? Theme.clipVideo : Theme.clipAudio
                                    return seleccionado ? Qt.lighter(base, 1.3) : base
                                }
                                opacity: enArrastre && raiz.arrastre.tipo === "mover" ? 0.85
                                         : apagado ? 0.45 : 1
                                border.color: seleccionado ? Theme.acento : Qt.lighter(color, 1.25)
                                border.width: seleccionado ? 2 : 1
                                clip: true // el nombre no se sale del clip

                                // Onda del audio: solo la parte visible del clip.
                                // (Se crea solo si se ve: con miles de clips, cada elemento cuenta.)
                                Loader {
                                    anchors.fill: parent
                                    active: !elemento.esVideo && !elemento.bloque && elemento.width > 6
                                    sourceComponent: Item {
                                        WaveformView {
                                            visible: width > 0
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
                                            Loader {
                                                id: avisoOnda
                                                active: parent.loading && parent.width > 40
                                                anchors.fill: parent
                                                sourceComponent: Item {
                                                    Row {
                                                        id: cargandoOnda
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
                                                                    running: true
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
                                            }
                                        }
                                    }
                                }

                                // Miniatura del video: solo el primer fotograma del clip, al
                                // principio (como pidió Kyo). Cada clip pide siempre la misma
                                // imagen: al hacer zoom o desplazarse no se decodifica nada.
                                Image {
                                    visible: elemento.esVideo && !elemento.bloque && elemento.width > 24
                                             && (elemento.modelData.thumbSource || "") !== ""
                                    x: 0
                                    y: 1
                                    height: elemento.height - 2
                                    width: Math.min(Math.round(height * 16 / 9), elemento.width)
                                    fillMode: Image.PreserveAspectCrop
                                    horizontalAlignment: Image.AlignLeft
                                    asynchronous: true
                                    sourceSize.width: 160
                                    opacity: 0.6
                                    source: visible
                                            ? "image://fotogramas/"
                                              + encodeURIComponent(elemento.modelData.thumbSource) + "@"
                                              + Math.round(elemento.modelData["in"] * 1e6
                                                           / Math.max(1, raiz.editor.fps))
                                            : ""
                                }

                                Loader {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 18
                                    active: elemento.width > 30 && !elemento.bloque
                                    sourceComponent: RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 5
                                        anchors.rightMargin: 5
                                        anchors.topMargin: 2
                                        spacing: 4
                                        Text {
                                            Layout.fillWidth: true
                                            text: (elemento.esVideo ? "" : "♪ ") + elemento.modelData.name
                                            color: Theme.texto
                                            // Contorno oscuro: legible sobre las miniaturas
                                            style: elemento.esVideo ? Text.Outline : Text.Normal
                                            styleColor: Theme.visorFondo
                                            font.pixelSize: Theme.tamTextoPequeno
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: !elemento.esVideo && elemento.modelData.volumeDb !== 0
                                            text: raiz.textoVolumen(elemento.modelData.volumeDb)
                                            color: Theme.texto
                                            font.pixelSize: 10
                                        }
                                        Loader {
                                            active: elemento.esVideo && elemento.modelData.transformed
                                            visible: active
                                            sourceComponent: Icono {
                                                nombre: "rellenar"
                                                tam: 11
                                                color: Theme.texto
                                            }
                                        }
                                    }
                                }

                                // Keyframes del clip (◆ abajo): clic = ir ahí; arrastrar = moverlo.
                                Repeater {
                                    model: elemento.esVideo && !elemento.bloque && elemento.width > 16
                                           && !elemento.enArrastre ? (elemento.modelData.keyframes || []) : []
                                    delegate: Item {
                                        id: rombo
                                        required property var modelData
                                        readonly property real fotograma: modelData
                                        property real desplazado: 0 // fotogramas, mientras se arrastra
                                        x: (fotograma + desplazado - elemento.modelData.start) * raiz.escala - 6
                                        y: elemento.height - 13
                                        width: 12
                                        height: 12
                                        z: 5
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 7
                                            height: 7
                                            rotation: 45
                                            color: zonaRombo.pressed || zonaRombo.containsMouse
                                                   ? Theme.cabezal : Theme.texto
                                            border.color: Theme.fondo
                                        }
                                        MouseArea {
                                            id: zonaRombo
                                            anchors.fill: parent
                                            anchors.margins: -2
                                            hoverEnabled: true
                                            preventStealing: true
                                            enabled: !elemento.bloqueado
                                            cursorShape: Qt.SizeHorCursor
                                            property real xInicial: 0
                                            onPressed: (m) => {
                                                xInicial = mapToItem(contenido, m.x, 0).x
                                                raiz.llevarCabezal(rombo.fotograma)
                                            }
                                            onPositionChanged: (m) => {
                                                if (!pressed)
                                                    return
                                                const c = elemento.modelData
                                                const f = Math.round(rombo.fotograma
                                                    + (mapToItem(contenido, m.x, 0).x - xInicial) / raiz.escala)
                                                rombo.desplazado = Math.max(c.start, Math.min(c.start + c.length - 1, f))
                                                                   - rombo.fotograma
                                            }
                                            onReleased: {
                                                const d = rombo.desplazado
                                                rombo.desplazado = 0
                                                if (d !== 0) {
                                                    Qt.callLater(raiz.editor.moveKeyframes, elemento.modelData.id,
                                                                 rombo.fotograma, rombo.fotograma + d)
                                                    raiz.editor.seek(rombo.fotograma + d)
                                                }
                                            }
                                        }
                                        ToolTip.visible: zonaRombo.containsMouse && !zonaRombo.pressed
                                        ToolTip.delay: 600
                                        ToolTip.text: "Keyframe: clic para ir, arrastra para moverlo"
                                    }
                                }

                                // Bloque de clips diminutos: cuántos son.
                                Loader {
                                    active: elemento.bloque && elemento.width > 160
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: Math.max(6, vista.contentX - elemento.x + 6)
                                    sourceComponent: Text {
                                        text: elemento.modelData.count + " clips · acércate para editarlos"
                                        color: Theme.texto
                                        opacity: 0.7
                                        font.pixelSize: Theme.tamTextoPequeno
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
                                    enabled: !elemento.bloqueado && !elemento.bloque
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
                                        const tramo = raiz.editor.groupRange(grupo)
                                        const inicio = tramo.start
                                        const fin = tramo.end
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
                                    model: elemento.bloque || elemento.width < 6 ? [] : ["inicio", "final"]
                                    delegate: MouseArea {
                                        id: borde
                                        required property string modelData
                                        readonly property string lado: modelData
                                        width: Math.min(8, elemento.width / 3)
                                        height: elemento.height
                                        anchors.left: lado === "inicio" ? elemento.left : undefined
                                        anchors.right: lado === "final" ? elemento.right : undefined
                                        visible: raiz.herramienta === "seleccion" && !elemento.bloqueado
                                                 && !elemento.bloque
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

                        // Cuadro de selección.
                        Rectangle {
                            visible: raiz.marcoSeleccion !== null
                            z: 5
                            x: visible ? raiz.marcoSeleccion.x0 : 0
                            y: visible ? raiz.marcoSeleccion.y0 : 0
                            width: visible ? raiz.marcoSeleccion.x1 - raiz.marcoSeleccion.x0 : 0
                            height: visible ? raiz.marcoSeleccion.y1 - raiz.marcoSeleccion.y0 : 0
                            color: "transparent"
                            border.color: Theme.acento
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.acento
                                opacity: 0.12
                            }
                        }

                        DropArea {
                            id: zonaSoltar
                            y: raiz.yPrimeraFila - raiz.separacion
                            width: parent.width
                            height: Math.max(raiz.altoPistas + 2 * raiz.separacion, contenido.height - y)
                            keys: ["sharkcut/medio", "sharkcut/transicion", "sharkcut/efecto",
                                   "text/uri-list"]
                            function esTransicion(d) {
                                return d.keys.indexOf("sharkcut/transicion") >= 0
                            }
                            function esEfecto(d) {
                                return d.keys.indexOf("sharkcut/efecto") >= 0
                            }
                            function actualizar(d) {
                                const p = raiz.pistaEnY(d.y + y)
                                if (esEfecto(d)) {
                                    raiz.pistaSoltar = p && p.video ? p.index : -1
                                    return
                                }
                                if (esTransicion(d)) {
                                    // Transición como clip: dónde se suelta (se centra en el
                                    // corte más cercano al soltarla).
                                    raiz.marcaSoltar = raiz.frameEn(d.x)
                                    raiz.pistaSoltar = p && p.video ? p.index : -1
                                    return
                                }
                                raiz.marcaSoltar = raiz.ajustar(raiz.frameEn(d.x), [])
                                raiz.pistaSoltar = p ? p.index : -1
                                raiz.zonaNueva = p ? "" : raiz.zonaNuevaEn(d.y + y)
                            }
                            onPositionChanged: (d) => actualizar(d)
                            onEntered: (d) => actualizar(d)
                            onExited: {
                                raiz.marcaSoltar = -1
                                raiz.pistaSoltar = -1
                                raiz.corteSoltar = null
                                raiz.zonaNueva = ""
                            }
                            onDropped: (d) => {
                                const f = raiz.ajustar(raiz.frameEn(d.x), [])
                                const p = raiz.pistaEnY(d.y + y)
                                // En la zona vacía de arriba o de abajo: pistas nuevas.
                                const pista = p ? p.index : raiz.zonaNuevaEn(d.y + y) !== "" ? -2 : -1
                                const corte = raiz.corteSoltar
                                raiz.marcaSoltar = -1
                                raiz.pistaSoltar = -1
                                raiz.corteSoltar = null
                                raiz.zonaNueva = ""
                                if (esEfecto(d)) {
                                    // Sobre un clip de video: se le aplica el efecto.
                                    const clip = p && p.video ? raiz.editor.videoClipAt(p.index, raiz.frameEn(d.x)) : 0
                                    if (clip !== 0 && d.source)
                                        Qt.callLater(raiz.editor.applyEffect, clip, d.source.efecto)
                                    d.accept()
                                } else if (esTransicion(d)) {
                                    // Una transición-clip: afecta a todo lo que hay debajo.
                                    if (d.source)
                                        Qt.callLater(raiz.editor.addEffectClip, p && p.video ? p.index : -1,
                                                     raiz.frameEn(d.x), d.source.transicion)
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
                            // Franja amarilla a lo largo de la regla: ahí se lleva el cabezal
                            // (clic o arrastre).
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 3
                                color: Theme.reglaCabezal
                                opacity: zonaRegla.containsMouse || zonaRegla.pressed ? 1 : 0.7
                            }
                            MouseArea {
                                id: zonaRegla
                                anchors.fill: parent
                                preventStealing: true
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onPressed: (m) => raiz.llevarCabezal(raiz.cabezalEn(m.x))
                                onPositionChanged: (m) => {
                                    if (pressed)
                                        raiz.editor.seek(raiz.cabezalEn(m.x))
                                }
                            }

                            // Marcadores (M): clic para ir, arrastrar para moverlo, doble clic
                            // para ponerle nombre y clic derecho para su menú.
                            Repeater {
                                model: raiz.editor.markers
                                delegate: Item {
                                    id: marcador
                                    required property var modelData
                                    property real arrastre: -1 // fotograma mientras se arrastra
                                    readonly property real fotograma: arrastre >= 0 ? arrastre
                                                                                    : modelData.frame
                                    x: fotograma * raiz.escala - 6
                                    y: regla.height - 14
                                    width: 12
                                    height: 14
                                    z: 2
                                    Shape {
                                        anchors.fill: parent
                                        preferredRendererType: Shape.CurveRenderer
                                        ShapePath {
                                            strokeColor: Theme.visorFondo
                                            strokeWidth: 1
                                            fillColor: Theme.marcador
                                            startX: 0; startY: 0
                                            PathLine { x: 12; y: 0 }
                                            PathLine { x: 12; y: 8 }
                                            PathLine { x: 6; y: 14 }
                                            PathLine { x: 0; y: 8 }
                                            PathLine { x: 0; y: 0 }
                                        }
                                    }
                                    Text {
                                        visible: marcador.modelData.name.length > 0
                                        x: 14
                                        y: -1
                                        text: marcador.modelData.name
                                        color: Theme.marcador
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                    MouseArea {
                                        id: zonaMarcador
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        hoverEnabled: true
                                        preventStealing: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.PointingHandCursor
                                        property real inicioX: 0
                                        onPressed: (m) => {
                                            if (m.button === Qt.RightButton) {
                                                menuMarcador.abrir(marcador.modelData)
                                                return
                                            }
                                            inicioX = mapToItem(regla, m.x, m.y).x
                                        }
                                        onPositionChanged: (m) => {
                                            if (!(pressedButtons & Qt.LeftButton))
                                                return
                                            const xr = mapToItem(regla, m.x, m.y).x
                                            if (marcador.arrastre < 0 && Math.abs(xr - inicioX) < 4)
                                                return
                                            marcador.arrastre = raiz.ajustar(raiz.frameEn(xr), [])
                                        }
                                        onReleased: (m) => {
                                            if (m.button !== Qt.LeftButton)
                                                return
                                            if (marcador.arrastre >= 0) {
                                                const destino = marcador.arrastre
                                                marcador.arrastre = -1
                                                raiz.editor.moveMarker(marcador.modelData.id, destino)
                                            } else {
                                                raiz.editor.pause()
                                                raiz.llevarCabezal(marcador.modelData.frame)
                                            }
                                        }
                                        onDoubleClicked: (m) => {
                                            if (m.button === Qt.LeftButton)
                                                menuMarcador.renombrar(marcador.modelData)
                                        }
                                    }
                                    ToolTip {
                                        id: ayudaMarcador
                                        visible: zonaMarcador.containsMouse && marcador.arrastre < 0
                                        delay: 400
                                        text: (marcador.modelData.name.length > 0 ? marcador.modelData.name
                                                                                  : "Marcador")
                                              + " · " + raiz.editor.timecodeFor(marcador.modelData.frame)
                                        contentItem: Text {
                                            text: ayudaMarcador.text
                                            color: Theme.texto
                                            font.pixelSize: Theme.tamTextoPequeno
                                        }
                                        background: Rectangle {
                                            color: Theme.panelElevado
                                            border.color: Theme.borde
                                            radius: 4
                                        }
                                    }
                                }
                            }
                        }

                        // Dónde caería el cabezal: línea tenue y su tiempo bajo el ratón.
                        HoverHandler {
                            id: sobreLinea
                        }
                        Item {
                            visible: sobreLinea.hovered && raiz.arrastre === null
                            x: sobreLinea.point.position.x
                            y: vista.contentY
                            width: 1
                            height: vista.height
                            z: 7
                            Rectangle {
                                width: 1
                                height: parent.height
                                color: Theme.textoSecundario
                                opacity: 0.45
                            }
                            Rectangle {
                                x: 4
                                y: 2
                                width: tiempoRaton.implicitWidth + 8
                                height: tiempoRaton.implicitHeight + 2
                                radius: 3
                                color: Theme.panelElevado
                                border.color: Theme.borde
                                Text {
                                    id: tiempoRaton
                                    anchors.centerIn: parent
                                    text: raiz.editor.timecodeFor(raiz.frameEn(sobreLinea.point.position.x))
                                    color: Theme.textoSecundario
                                    font.pixelSize: 10
                                    font.family: "Consolas, monospace"
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
                text: !raiz.editor.hasSequence
                      ? "No hay ninguna secuencia abierta: abre una desde Medios (doble clic) o crea "
                        + "una nueva con «+»."
                      : "Arrastra medios desde la biblioteca (o archivos desde el explorador) a una pista: "
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
            if (raiz.editor.playing && !(e.modifiers & (Qt.AltModifier | Qt.ControlModifier)))
                raiz.seguirCabezal = false
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
        property real fotograma: -1 // dónde se hizo clic (-1: en la cabecera)
        property bool hayHueco: false
        function abrir(p, frame) {
            pista = p
            fotograma = frame === undefined ? -1 : frame
            hayHueco = fotograma >= 0 && raiz.editor.hasGapAt(p.index, fotograma)
            popup()
        }
        readonly property int delMismoTipo: pista === null ? 0
            : raiz.editor.tracks.filter((t) => t.video === pista.video).length
        Action {
            text: "Cerrar este hueco"
            enabled: menuPista.hayHueco
            onTriggered: raiz.editor.closeGap(menuPista.pista.index, menuPista.fotograma)
        }
        Action {
            text: "Pegar en el cabezal (Ctrl+V)"
            enabled: raiz.editor.canPaste
            onTriggered: raiz.editor.pasteAtPlayhead()
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
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
        id: menuSubtitulos
        property var pista: null
        function abrir(p) {
            pista = p
            popup()
        }
        Action {
            text: "Añadir subtítulo en el cabezal"
            enabled: menuSubtitulos.pista !== null && !menuSubtitulos.pista.locked
            onTriggered: raiz.editor.addCaptionAtPlayhead(menuSubtitulos.pista.index)
        }
        Action {
            text: "Volver a dividir…"
            enabled: menuSubtitulos.pista !== null && !menuSubtitulos.pista.locked
                     && menuSubtitulos.pista.count > 0
            onTriggered: raiz.dividirSubtitulos(menuSubtitulos.pista.index)
        }
        Action {
            text: "Exportar como .srt…"
            enabled: menuSubtitulos.pista !== null && menuSubtitulos.pista.count > 0
            onTriggered: raiz.exportarSrt(menuSubtitulos.pista.index)
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Añadir pista de subtítulos"
            onTriggered: raiz.editor.addCaptionTrack()
        }
        Action {
            text: menuSubtitulos.pista === null ? "Quitar pista"
                  : "Quitar la pista " + menuSubtitulos.pista.name
                    + (menuSubtitulos.pista.count > 0 ? " y sus subtítulos" : "")
            enabled: menuSubtitulos.pista !== null && !menuSubtitulos.pista.locked
            onTriggered: raiz.editor.removeCaptionTrack(menuSubtitulos.pista.index)
        }
    }

    MenuTema {
        id: menuMarcador
        property var marcador: null
        function abrir(m) {
            marcador = m
            popup()
        }
        function renombrar(m) {
            const id = m.id
            dialogoNombre.pedir("Nombre del marcador", m.name, "Aceptar",
                                (nombre) => raiz.editor.renameMarker(id, nombre))
        }
        Action {
            text: "Ir al marcador"
            onTriggered: {
                raiz.editor.pause()
                raiz.editor.seek(menuMarcador.marcador.frame)
            }
        }
        Action {
            text: "Ponerle nombre…"
            onTriggered: menuMarcador.renombrar(menuMarcador.marcador)
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Quitar el marcador"
            onTriggered: raiz.editor.removeMarker(menuMarcador.marcador.id)
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
            text: "Copiar (Ctrl+C)"
            onTriggered: raiz.editor.copySelection()
        }
        Action {
            text: "Cortar (Ctrl+X)"
            onTriggered: raiz.editor.cutSelection()
        }
        Action {
            text: "Pegar en el cabezal (Ctrl+V)"
            enabled: raiz.editor.canPaste
            onTriggered: raiz.editor.pasteAtPlayhead()
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Dividir en el cabezal"
            onTriggered: raiz.editor.splitAtPlayhead()
        }
        Action {
            text: "Quitar (deja el hueco)"
            onTriggered: raiz.editor.removeSelection()
        }
        Action {
            text: "Eliminar con rizo (Mayús+Supr)"
            onTriggered: raiz.editor.rippleDeleteSelection()
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

    // Pestaña de una secuencia abierta: clic para verla, doble clic para renombrarla.
    component PestanaSecuencia: Rectangle {
        id: tab
        property var pestana
        property bool conProyecto: false
        readonly property bool activa: pestana.active
        width: filaTab.implicitWidth + 18
        height: pestanas.height
        radius: Theme.radio
        color: activa ? Theme.panelElevado : zonaTab.containsMouse ? Theme.fondo : "transparent"
        border.color: activa ? Theme.acento : Theme.borde
        MouseArea {
            id: zonaTab
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            onClicked: (m) => {
                if (m.button === Qt.MiddleButton)
                    raiz.editor.closeTab(tab.pestana.index)
                else
                    raiz.editor.activateTab(tab.pestana.index)
            }
            onDoubleClicked: (m) => {
                if (m.button !== Qt.LeftButton)
                    return
                raiz.editor.activateTab(tab.pestana.index)
                const id = tab.pestana.sequence
                dialogoNombre.pedir("Renombrar secuencia", tab.pestana.name, "Renombrar",
                                    (nombre) => raiz.editor.renameSequence(id, nombre))
            }
        }
        Row {
            id: filaTab
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Icono {
                anchors.verticalCenter: parent.verticalCenter
                nombre: "secuencia"
                tam: 13
                color: tab.activa ? Theme.acento : Theme.textoSecundario
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: tab.conProyecto
                text: tab.pestana.projectName + " ›"
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: tab.pestana.name
                color: tab.activa ? Theme.texto : Theme.textoSecundario
                font.pixelSize: Theme.tamTexto
                font.bold: tab.activa
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: tab.pestana.canvas
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 3
                color: zonaCerrarTab.containsMouse ? Theme.borde : "transparent"
                Icono {
                    anchors.centerIn: parent
                    nombre: "cerrar"
                    tam: 11
                    color: zonaCerrarTab.containsMouse ? Theme.texto : Theme.textoDesactivado
                }
                MouseArea {
                    id: zonaCerrarTab
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: raiz.editor.closeTab(tab.pestana.index)
                }
            }
        }
    }

    DialogoNombre { id: dialogoNombre }

    // Cabecera de una pista: nombre, ocultar o silenciar, y bloquear.
    component CabeceraPista: Rectangle {
        id: cabecera
        property var pista
        width: raiz.anchoCabecera - 6
        radius: 3
        color: pista.video ? Theme.cabeceraVideo : Theme.cabeceraAudio
        border.color: Theme.borde

        // Franja del color del tipo de pista
        Rectangle {
            x: 1
            y: 4
            width: 3
            height: parent.height - 8
            radius: 1.5
            color: cabecera.pista.video ? Theme.clipVideoClaro : Theme.clipAudioClaro
            opacity: 0.8
        }

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

    // Cabecera de una pista de subtítulos: ocultar y bloquear; clic derecho, su menú.
    component CabeceraSubtitulos: Rectangle {
        id: cabeceraSub
        property var pista
        width: raiz.anchoCabecera - 6
        radius: 3
        color: Theme.cabeceraVideo
        border.color: Theme.borde
        Rectangle {
            x: 1
            y: 4
            width: 3
            height: parent.height - 8
            radius: 1.5
            color: Theme.clipTexto
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: 9
            text: cabeceraSub.pista.name
            color: cabeceraSub.pista.locked ? Theme.textoSecundario : Theme.texto
            font.pixelSize: Theme.tamTexto
            font.bold: true
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: menuSubtitulos.abrir(cabeceraSub.pista)
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 5
            spacing: 2
            BotonPista {
                icono: cabeceraSub.pista.hidden ? "ojoCerrado" : "ojo"
                aviso: cabeceraSub.pista.hidden
                ayuda: cabeceraSub.pista.hidden ? "Mostrar los subtítulos"
                                                : "Ocultar los subtítulos (no se ven ni se exportan)"
                onPulsado: raiz.editor.setCaptionTrackHidden(cabeceraSub.pista.index, !cabeceraSub.pista.hidden)
            }
            BotonPista {
                icono: cabeceraSub.pista.locked ? "candado" : "candadoAbierto"
                aviso: cabeceraSub.pista.locked
                ayuda: cabeceraSub.pista.locked ? "Desbloquear la pista"
                                                : "Bloquear la pista (sus subtítulos no se pueden editar)"
                onPulsado: raiz.editor.setCaptionTrackLocked(cabeceraSub.pista.index, !cabeceraSub.pista.locked)
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
