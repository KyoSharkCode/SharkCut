import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Línea de tiempo del editor: regla, pista de video (V1) y de audio (A1).
//
// Herramientas: Selección (mover clips y recortar sus bordes) y Cuchilla (dividir
// donde se hace clic). Mientras se arrastra solo se muestra una vista previa; la
// edición se aplica al soltar, como un único paso de deshacer.
Rectangle {
    id: raiz

    required property Editor editor
    property string herramienta: "seleccion" // "seleccion" o "cuchilla"
    property bool iman: true                 // ajustar a bordes y cabezal al arrastrar

    // Vista previa del arrastre en curso:
    // {tipo: "mover" | "inicio" | "final", grupo: [ids], delta, valor}
    property var arrastre: null
    property real marcaSoltar: -1 // fotograma donde caería lo que se arrastra encima

    property real escala: 0.5 // píxeles por fotograma
    readonly property real escalaMaxima: 24
    readonly property real escalaMinima: Math.min(escalaMaxima,
        Math.max(0.0005, (vista.width - 40) / Math.max(editor.frameCount, editor.fps * 10)))

    readonly property int alturaRegla: 26
    readonly property int alturaVideo: 58
    readonly property int alturaAudio: 46
    readonly property int separacion: 4
    readonly property int yVideo: alturaRegla + separacion
    readonly property int yAudio: yVideo + alturaVideo + separacion

    color: Theme.panel
    radius: Theme.radio
    border.color: zonaSoltar.containsDrag ? Theme.acento : Theme.borde
    implicitHeight: barra.implicitHeight + alturaRegla + alturaVideo + alturaAudio + 3 * separacion + 44

    // --- Utilidades --------------------------------------------------------------

    function frameEn(xContenido) {
        return Math.max(0, Math.round(xContenido / escala))
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

    // --- Barra de herramientas --------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.margen
        spacing: 6

        RowLayout {
            id: barra
            Layout.fillWidth: true
            spacing: 6

            Pestana {
                texto: "Selección (V)"
                activa: raiz.herramienta === "seleccion"
                onPulsada: raiz.herramienta = "seleccion"
            }
            Pestana {
                texto: "Cuchilla (C)"
                activa: raiz.herramienta === "cuchilla"
                onPulsada: raiz.herramienta = "cuchilla"
            }
            Pestana {
                texto: "Imán (S)"
                activa: raiz.iman
                onPulsada: raiz.iman = !raiz.iman
            }
            Item { implicitWidth: 6 }
            Boton {
                texto: "Dividir (Ctrl+K)"
                enabled: raiz.editor.frameCount > 0
                onPulsado: raiz.editor.splitAtPlayhead()
            }
            Boton {
                texto: "Quitar (Supr)"
                enabled: raiz.editor.selection.length > 0
                onPulsado: raiz.editor.removeSelection()
            }
            Boton {
                texto: "Deshacer"
                enabled: raiz.editor.canUndo
                onPulsado: raiz.editor.undo()
            }
            Boton {
                texto: "Rehacer"
                enabled: raiz.editor.canRedo
                onPulsado: raiz.editor.redo()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: raiz.editor.projectDescription
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
            Item { implicitWidth: 6 }
            Boton { texto: "−"; onPulsado: raiz.alejar() }
            Boton { texto: "+"; onPulsado: raiz.acercar() }
            Boton { texto: "Ajustar"; onPulsado: raiz.encajar() }
        }

        // --- Pistas -------------------------------------------------------------

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Cabeceras de pista
            Item {
                Layout.preferredWidth: 40
                Layout.fillHeight: true
                CabeceraPista { y: raiz.yVideo; height: raiz.alturaVideo; texto: "V1" }
                CabeceraPista { y: raiz.yAudio; height: raiz.alturaAudio; texto: "A1" }
            }

            Flickable {
                id: vista
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                interactive: false
                contentWidth: Math.max(width, (raiz.editor.frameCount + raiz.editor.fps * 30)
                                              * raiz.escala)
                contentHeight: height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOn }

                Item {
                    id: contenido
                    width: vista.contentWidth
                    height: vista.height

                    // Fondo de las pistas: clic en vacío = quitar la selección.
                    Rectangle {
                        y: raiz.yVideo
                        width: parent.width
                        height: raiz.alturaVideo
                        color: Theme.fondo
                    }
                    Rectangle {
                        y: raiz.yAudio
                        width: parent.width
                        height: raiz.alturaAudio
                        color: Theme.fondo
                    }
                    MouseArea {
                        y: raiz.yVideo
                        width: parent.width
                        height: raiz.yAudio + raiz.alturaAudio - raiz.yVideo
                        onPressed: raiz.editor.clearSelection()
                    }

                    // Regla: clic o arrastre para mover el cabezal.
                    Item {
                        id: regla
                        width: parent.width
                        height: raiz.alturaRegla

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

                    // Clips
                    Repeater {
                        model: raiz.editor.clips
                        delegate: Rectangle {
                            id: elemento

                            required property var modelData
                            readonly property bool esVideo: modelData.track === 0
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

                            x: inicioVista * raiz.escala
                            y: esVideo ? raiz.yVideo : raiz.yAudio
                            z: enArrastre ? 2 : 1
                            width: Math.max(2, (finVista - inicioVista) * raiz.escala)
                            height: esVideo ? raiz.alturaVideo : raiz.alturaAudio
                            radius: 3
                            color: {
                                const base = esVideo ? Theme.clipVideo : Theme.clipAudio
                                return seleccionado ? Qt.lighter(base, 1.3) : base
                            }
                            opacity: enArrastre && raiz.arrastre.tipo === "mover" ? 0.85 : 1
                            border.color: seleccionado ? Theme.acento : Qt.lighter(color, 1.25)
                            border.width: seleccionado ? 2 : 1
                            clip: true // el nombre no se sale del clip

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 5
                                text: (elemento.esVideo ? "" : "♪ ") + elemento.modelData.name
                                color: Theme.texto
                                font.pixelSize: Theme.tamTextoPequeno
                                elide: Text.ElideRight
                                visible: elemento.width > 30
                            }

                            // Cuerpo: seleccionar, mover o (con la cuchilla) dividir.
                            MouseArea {
                                id: cuerpo
                                anchors.fill: parent
                                preventStealing: true
                                cursorShape: raiz.herramienta === "cuchilla" ? Qt.IBeamCursor
                                                                             : Qt.ArrowCursor
                                property real xInicial: 0

                                onPressed: (m) => {
                                    const x = mapToItem(contenido, m.x, 0).x
                                    if (raiz.herramienta === "cuchilla") {
                                        const f = raiz.ajustar(raiz.frameEn(x), [])
                                        Qt.callLater(raiz.editor.splitClip, elemento.modelData.id, f)
                                        return
                                    }
                                    const sumar = (m.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                                    if (sumar || !elemento.seleccionado)
                                        raiz.editor.select(elemento.modelData.id, sumar)
                                    xInicial = x
                                    raiz.arrastre = null
                                }
                                onPositionChanged: (m) => {
                                    if (!pressed || raiz.herramienta !== "seleccion")
                                        return
                                    const dx = mapToItem(contenido, m.x, 0).x - xInicial
                                    if (raiz.arrastre === null && Math.abs(dx) < 4)
                                        return
                                    const grupo = raiz.grupoDe(elemento.modelData)
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
                                    delta = raiz.editor.limitMove(elemento.modelData.id, delta)
                                    raiz.arrastre = { tipo: "mover", grupo: grupo, delta: delta, valor: 0 }
                                }
                                onReleased: {
                                    const a = raiz.arrastre
                                    raiz.arrastre = null
                                    if (a !== null && a.tipo === "mover" && a.delta !== 0)
                                        Qt.callLater(raiz.editor.moveClips, elemento.modelData.id, a.delta)
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
                                    visible: raiz.herramienta === "seleccion"
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
                                        raiz.arrastre = { tipo: lado, grupo: grupo, delta: 0, valor: f }
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

                    // Dónde caerá lo que se arrastra desde la biblioteca o el explorador.
                    Rectangle {
                        visible: raiz.marcaSoltar >= 0
                        x: raiz.marcaSoltar * raiz.escala - 1
                        y: raiz.yVideo
                        width: 2
                        height: raiz.yAudio + raiz.alturaAudio - raiz.yVideo
                        color: Theme.acento
                        z: 4
                    }

                    DropArea {
                        id: zonaSoltar
                        y: raiz.yVideo
                        width: parent.width
                        height: raiz.yAudio + raiz.alturaAudio - raiz.yVideo
                        keys: ["sharkcut/medio", "text/uri-list"]
                        onPositionChanged: (d) => raiz.marcaSoltar = raiz.ajustar(raiz.frameEn(d.x), [])
                        onEntered: (d) => raiz.marcaSoltar = raiz.ajustar(raiz.frameEn(d.x), [])
                        onExited: raiz.marcaSoltar = -1
                        onDropped: (d) => {
                            const f = raiz.ajustar(raiz.frameEn(d.x), [])
                            raiz.marcaSoltar = -1
                            if (d.hasUrls && d.urls.length > 0) {
                                raiz.editor.importFiles(d.urls, f)
                                d.acceptProposedAction()
                            } else if (d.source && d.source.mediaId !== undefined) {
                                Qt.callLater(raiz.editor.addMedia, d.source.mediaId, f)
                                d.accept()
                            }
                        }
                    }

                    // Cabezal de reproducción
                    Item {
                        x: raiz.editor.currentFrame * raiz.escala
                        width: 1
                        height: parent.height
                        z: 5
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
            text: "Arrastra medios desde la biblioteca (o archivos desde el explorador) a las pistas."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }
    }

    // Rueda: desplazar; con Ctrl, zoom alrededor del ratón.
    WheelHandler {
        target: null
        onWheel: (e) => {
            const p = raiz.mapToItem(vista, e.x, e.y)
            if (e.modifiers & Qt.ControlModifier) {
                raiz.zoomEn(e.angleDelta.y > 0 ? 1.25 : 0.8, p.x)
            } else {
                const d = e.angleDelta.x !== 0 ? e.angleDelta.x : e.angleDelta.y
                vista.contentX = Math.max(0, Math.min(vista.contentWidth - vista.width,
                                                      vista.contentX - d))
            }
        }
    }

    component CabeceraPista: Rectangle {
        property string texto
        width: 34
        radius: 3
        color: Theme.panelElevado
        border.color: Theme.borde
        Text {
            anchors.centerIn: parent
            text: parent.texto
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.bold: true
        }
    }
}
