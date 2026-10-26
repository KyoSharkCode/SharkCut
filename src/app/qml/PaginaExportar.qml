import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

// Pestaña Exportar: la configuración completa (secuencia, nombre y carpeta, tramo, códec,
// resolución, fps, calidad y audio) con la vista previa de la secuencia al lado.
// Lo elegido aquí queda como lo que usa «Exportar rápido».
Item {
    id: pagina

    required property Editor editor

    // Secuencia que se exporta: la activa (elegir otra la abre en el editor).
    readonly property var secuencia: {
        const lista = editor.sequences
        for (const s of lista) {
            if (s.active)
                return s
        }
        return null
    }
    readonly property var idSecuencia: secuencia ? secuencia.id : 0

    // Ajustes (se cargan de los últimos usados al entrar en la pestaña)
    property string codec: "h264"
    property real escala: 1
    property int fps: 0
    property int calidad: 1
    property real mbps: 16
    property bool audio: true
    property int audioKbps: 192
    property url carpeta
    property int tramo: 0 // 0 secuencia completa, 1 personalizado

    function cargar() {
        const a = editor.exportSettings
        codec = a.codec
        escala = a.scale
        fps = a.fps
        calidad = a.quality
        mbps = a.mbps
        audio = a.audio
        audioKbps = a.audioKbps
        carpeta = a.folder && a.folder.toString().length > 0 ? a.folder : editor.defaultExportFolder
        reiniciarSecuencia()
    }
    property var ultimaSecuencia: 0
    function reiniciarSecuencia() {
        ultimaSecuencia = idSecuencia
        nombre.text = secuencia ? secuencia.name : editor.projectName
        tramo = 0
        desde.text = editor.sequenceTimecode(idSecuencia, 0)
        hasta.text = editor.sequenceTimecode(idSecuencia, secuencia ? secuencia.frames : 0)
        // Una escala que esta secuencia no ofrece (p. ej. 200 % de un lienzo 4K): la suya.
        if (!tamanos.some((t) => Math.abs(t.scale - escala) < 1e-6))
            escala = 1
    }
    onVisibleChanged: if (visible) cargar()
    onIdSecuenciaChanged: if (visible && idSecuencia !== ultimaSecuencia) reiniciarSecuencia()

    readonly property var tamanos: editor.exportSizes(idSecuencia)
    readonly property var tamano: {
        for (const t of tamanos) {
            if (Math.abs(t.scale - escala) < 1e-6)
                return t
        }
        return tamanos.length > 0 ? tamanos[0] : { width: 0, height: 0, scale: 1, label: "" }
    }
    readonly property int fpsSecuencia: secuencia ? Math.round(secuencia.fps) : 60
    readonly property var opcionesFps: {
        const lista = [0]
        for (const f of [60, 50, 30, 25, 24]) {
            if (f !== fpsSecuencia)
                lista.push(f)
        }
        return lista
    }
    readonly property real fpsSalida: fps > 0 ? fps : (secuencia ? secuencia.fps : 60)

    // Tramo en fotogramas de la secuencia
    readonly property int totalFotogramas: secuencia ? secuencia.frames : 0
    readonly property var inicio: tramo === 0 ? 0 : editor.parseTimecode(idSecuencia, desde.text)
    readonly property var fin: tramo === 0 ? totalFotogramas : editor.parseTimecode(idSecuencia, hasta.text)
    readonly property bool tramoValido: inicio >= 0 && fin > inicio && fin <= totalFotogramas
    readonly property real segundos: tramoValido && secuencia ? (fin - inicio) / secuencia.fps : 0

    function mbpsDe(q) {
        return editor.recommendedMbps(idSecuencia, escala, fps, q, codec)
    }
    readonly property real mbpsVideo: calidad < 3 ? mbpsDe(calidad) : mbps
    readonly property real megas: segundos * (mbpsVideo + (audio ? audioKbps / 1000 : 0)) / 8

    function textoTamano(mb) {
        return mb >= 1024 ? (mb / 1024).toFixed(1) + " GB" : Math.max(1, Math.round(mb)) + " MB"
    }
    function textoDuracion(s) {
        const total = Math.round(s)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const ss = total % 60
        return (h > 0 ? h + " h " : "") + (h > 0 || m > 0 ? m + " min " : "") + ss + " s"
    }
    // "file:///C:/Videos" → "C:/Videos" (y "file:///home/…" → "/home/…").
    function rutaLegible(url) {
        let texto = url.toString()
        if (texto.startsWith("file:///") && texto.charAt(9) === ":")
            texto = texto.substring(8)
        else if (texto.startsWith("file://"))
            texto = texto.substring(7)
        return decodeURIComponent(texto)
    }

    readonly property bool listo: secuencia !== null && totalFotogramas > 0 && tramoValido
                                  && nombre.text.trim().length > 0 && !editor.exporting

    function exportar() {
        if (!listo)
            return
        editor.exportSequence({
            sequence: idSecuencia,
            name: nombre.text.trim(),
            folder: carpeta,
            codec: codec,
            scale: escala,
            fps: fps,
            quality: calidad,
            mbps: mbps,
            audio: audio,
            audioKbps: audioKbps,
            rangeStart: tramo === 0 ? -1 : inicio,
            rangeEnd: tramo === 0 ? -1 : fin
        })
    }

    FolderDialog {
        id: elegirCarpeta
        title: "Carpeta de destino"
        currentFolder: pagina.carpeta
        onAccepted: pagina.carpeta = selectedFolder
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.margen

        // --- Ajustes ---
        Rectangle {
            Layout.preferredWidth: 480
            Layout.fillHeight: true
            color: Theme.panel
            radius: Theme.radio
            border.color: Theme.borde

            Flickable {
                id: ajustes
                anchors.fill: parent
                contentHeight: columna.implicitHeight + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: BarraDesplazamiento {}

                ColumnLayout {
                    id: columna
                    x: 22
                    y: 20
                    width: ajustes.width - 44
                    spacing: 14

                    Text {
                        text: "Exportar"
                        color: Theme.texto
                        font.pixelSize: 17
                        font.bold: true
                    }

                    Selector {
                        etiqueta: "Secuencia"
                        model: pagina.editor.sequences.map((s) => s.name + "  ·  " + s.details)
                        indice: pagina.editor.sequences.findIndex((s) => s.active)
                        onElegido: (i) => pagina.editor.openSequence(pagina.editor.sequences[i].id)
                    }

                    // Nombre y carpeta
                    Etiqueta { texto: "Nombre del archivo" }
                    Campo {
                        Layout.topMargin: -8
                        TextField {
                            id: nombre
                            Layout.fillWidth: true
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            background: null
                            padding: 0
                            selectByMouse: true
                            onAccepted: pagina.exportar()
                        }
                        Text { text: ".mp4"; color: Theme.textoDesactivado; font.pixelSize: Theme.tamTexto }
                    }
                    Etiqueta { texto: "Carpeta" }
                    RowLayout {
                        Layout.topMargin: -8
                        Layout.fillWidth: true
                        spacing: 8
                        Campo {
                            Text {
                                Layout.fillWidth: true
                                text: pagina.rutaLegible(pagina.carpeta)
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                elide: Text.ElideMiddle
                            }
                        }
                        BotonIcono {
                            icono: "abrir"
                            texto: "Elegir…"
                            implicitHeight: 34
                            onPulsado: elegirCarpeta.open()
                        }
                    }

                    // Tramo
                    Etiqueta { texto: "Tramo" }
                    Opciones {
                        Layout.topMargin: -8
                        opciones: ["Secuencia completa", "Personalizado"]
                        elegida: pagina.tramo
                        onElegir: (i) => pagina.tramo = i
                    }
                    GridLayout {
                        visible: pagina.tramo === 1
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 8
                        rowSpacing: 6
                        Text { text: "Desde"; color: Theme.textoSecundario; font.pixelSize: Theme.tamTexto }
                        Campo {
                            valido: pagina.inicio >= 0
                            TextField {
                                id: desde
                                Layout.fillWidth: true
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                font.family: "Consolas, monospace"
                                background: null
                                padding: 0
                                selectByMouse: true
                            }
                        }
                        BotonIcono {
                            texto: "Cabezal"
                            ayuda: "Desde la posición del cabezal"
                            implicitHeight: 34
                            onPulsado: desde.text = pagina.editor.sequenceTimecode(
                                           pagina.idSecuencia, pagina.editor.currentFrame)
                        }
                        Text { text: "Hasta"; color: Theme.textoSecundario; font.pixelSize: Theme.tamTexto }
                        Campo {
                            valido: pagina.fin >= 0
                            TextField {
                                id: hasta
                                Layout.fillWidth: true
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                font.family: "Consolas, monospace"
                                background: null
                                padding: 0
                                selectByMouse: true
                            }
                        }
                        BotonIcono {
                            texto: "Cabezal"
                            ayuda: "Hasta la posición del cabezal"
                            implicitHeight: 34
                            onPulsado: hasta.text = pagina.editor.sequenceTimecode(
                                           pagina.idSecuencia, pagina.editor.currentFrame)
                        }
                    }
                    Text {
                        visible: pagina.tramo === 1
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: pagina.tramoValido
                              ? "Duración del tramo: " + pagina.textoDuracion(pagina.segundos)
                              : "Escribe tiempos como 00:01:30:00 (horas:minutos:segundos:fotogramas), "
                                + "con «Desde» antes que «Hasta» y dentro de la secuencia ("
                                + (pagina.secuencia ? pagina.secuencia.duration : "") + ")."
                        color: pagina.tramoValido ? Theme.textoSecundario : Theme.aviso
                        font.pixelSize: Theme.tamTextoPequeno
                    }

                    // Video
                    Separador {}
                    Etiqueta { texto: "Códec de video" }
                    Opciones {
                        Layout.topMargin: -8
                        opciones: ["H.264 · compatible con todo", "H.265 · archivos más ligeros"]
                        elegida: pagina.codec === "h265" ? 1 : 0
                        onElegir: (i) => pagina.codec = i === 1 ? "h265" : "h264"
                    }
                    Selector {
                        etiqueta: "Resolución"
                        model: pagina.tamanos.map((t) => t.label)
                        indice: pagina.tamanos.findIndex((t) => Math.abs(t.scale - pagina.escala) < 1e-6)
                        onElegido: (i) => pagina.escala = pagina.tamanos[i].scale
                    }
                    Selector {
                        etiqueta: "Fotogramas por segundo"
                        model: pagina.opcionesFps.map((f) => f === 0
                                                       ? "Los de la secuencia (" + (pagina.secuencia ? pagina.secuencia.fps.toFixed(2).replace(/\.?0+$/, "") : "") + " fps)"
                                                       : f + " fps")
                        indice: Math.max(0, pagina.opcionesFps.indexOf(pagina.fps))
                        onElegido: (i) => pagina.fps = pagina.opcionesFps[i]
                    }

                    Etiqueta { texto: "Calidad" }
                    GridLayout {
                        Layout.topMargin: -8
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 6
                        Repeater {
                            model: [{ nombre: "Ligera", nota: "más pequeño" },
                                    { nombre: "Alta", nota: "recomendada" },
                                    { nombre: "Máxima", nota: "para reeditar" },
                                    { nombre: "A medida", nota: "tú eliges" }]
                            delegate: Rectangle {
                                id: opcion
                                required property var modelData
                                required property int index
                                readonly property bool elegida: pagina.calidad === index
                                Layout.fillWidth: true
                                implicitHeight: 58
                                radius: Theme.radio
                                color: elegida ? Theme.panelElevado : Theme.fondo
                                border.color: elegida ? Theme.acento : Theme.borde
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: opcion.modelData.nombre
                                        color: Theme.texto
                                        font.pixelSize: Theme.tamTexto
                                        font.bold: true
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: opcion.index < 3
                                              ? pagina.mbpsDe(opcion.index).toFixed(1).replace(".0", "") + " Mbps"
                                              : opcion.modelData.nota
                                        color: Theme.textoSecundario
                                        font.pixelSize: Theme.tamTextoPequeno
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (opcion.index === 3 && pagina.calidad < 3)
                                            pagina.mbps = pagina.mbpsDe(pagina.calidad)
                                        pagina.calidad = opcion.index
                                    }
                                }
                            }
                        }
                    }
                    CampoNumero {
                        visible: pagina.calidad === 3
                        etiqueta: "Tasa de video"
                        valor: pagina.mbps
                        desde: 1
                        hasta: 200
                        paso: 0.5
                        decimales: 1
                        unidad: "Mbps"
                        onCambiando: (v) => pagina.mbps = v
                        onCambiado: (v) => pagina.mbps = v
                    }

                    // Audio
                    Separador {}
                    Etiqueta { texto: "Audio" }
                    Opciones {
                        Layout.topMargin: -8
                        opciones: ["AAC 128 kbps", "AAC 192 kbps", "AAC 320 kbps", "Sin audio"]
                        elegida: !pagina.audio ? 3 : pagina.audioKbps <= 128 ? 0
                                                    : pagina.audioKbps >= 320 ? 2 : 1
                        onElegir: (i) => {
                            pagina.audio = i !== 3
                            if (i !== 3)
                                pagina.audioKbps = [128, 192, 320][i]
                        }
                    }
                }
            }
        }

        // --- Vista previa y resumen ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.margen

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.margen
                    spacing: Theme.margen

                    RowLayout {
                        Layout.leftMargin: 4
                        Text {
                            text: pagina.secuencia ? pagina.secuencia.name : "Sin secuencia"
                            color: Theme.texto
                            font.pixelSize: Theme.tamTexto
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Chip { texto: "LIENZO " + pagina.editor.canvasLabel; colorTexto: Theme.texto }
                    }
                    VideoViewer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: pagina.editor
                        backgroundColor: Theme.visorFondo
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        enabled: pagina.editor.frameCount > 0
                        opacity: enabled ? 1 : 0.4
                        BotonIcono {
                            icono: pagina.editor.playing ? "pausa" : "reproducir"
                            ayuda: "Reproducir / pausa (Espacio)"
                            acentuado: true
                            implicitWidth: 52
                            onPulsado: pagina.editor.togglePlay()
                        }
                        Slider {
                            id: posicion
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1, pagina.editor.frameCount)
                            value: pagina.editor.currentFrame
                            onMoved: pagina.editor.seek(Math.round(value))
                            background: Rectangle {
                                x: posicion.leftPadding
                                y: posicion.topPadding + posicion.availableHeight / 2 - height / 2
                                width: posicion.availableWidth
                                height: 4
                                radius: 2
                                color: Theme.borde
                                Rectangle {
                                    width: posicion.visualPosition * parent.width
                                    height: parent.height
                                    radius: 2
                                    color: Theme.acento
                                }
                            }
                            handle: Rectangle {
                                x: posicion.leftPadding + posicion.visualPosition * (posicion.availableWidth - width)
                                y: posicion.topPadding + posicion.availableHeight / 2 - height / 2
                                width: 12
                                height: 12
                                radius: 6
                                color: Theme.cabezal
                            }
                        }
                        Text {
                            text: pagina.editor.timecode + " / " + pagina.editor.durationTimecode
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTexto
                            font.family: "Consolas, monospace"
                        }
                    }
                }
            }

            // Resumen y botón
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: resumen.implicitHeight + 32
                color: Theme.panel
                radius: Theme.radio
                border.color: Theme.borde

                ColumnLayout {
                    id: resumen
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 24
                        Resumen {
                            titulo: "Archivo"
                            valor: "MP4 · " + pagina.tamano.width + "×" + pagina.tamano.height
                                   + " · " + pagina.fpsSalida.toFixed(2).replace(/\.?0+$/, "") + " fps"
                        }
                        Resumen {
                            titulo: "Video"
                            valor: (pagina.codec === "h265" ? "H.265" : "H.264") + " · "
                                   + pagina.mbpsVideo.toFixed(1).replace(".0", "") + " Mbps"
                        }
                        Resumen {
                            titulo: "Audio"
                            valor: pagina.audio ? "AAC · " + pagina.audioKbps + " kbps" : "Sin audio"
                        }
                        Resumen {
                            titulo: "Duración"
                            valor: pagina.tramoValido ? pagina.textoDuracion(pagina.segundos) : "—"
                        }
                        Resumen {
                            titulo: "Tamaño aprox."
                            valor: pagina.tramoValido ? pagina.textoTamano(pagina.megas) : "—"
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // Progreso o resultado
                    Rectangle {
                        visible: pagina.editor.exporting
                        Layout.fillWidth: true
                        implicitHeight: 6
                        radius: 3
                        color: Theme.panelElevado
                        Rectangle {
                            width: parent.width * pagina.editor.exportProgress
                            height: parent.height
                            radius: 3
                            color: Theme.acento
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: pagina.editor.exportMessage.length > 0 ? pagina.editor.exportMessage
                                  : "Se usa la GPU para codificar si está disponible (NVENC en una "
                                    + "tarjeta NVIDIA). Si ya existe un archivo con ese nombre, se "
                                    + "guarda como «nombre (2)»."
                            color: pagina.editor.exportMessage.startsWith("No se") ? Theme.aviso
                                                                                   : Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            wrapMode: Text.WordWrap
                        }
                        BotonIcono {
                            visible: !pagina.editor.exporting && pagina.editor.lastExportPath.length > 0
                            icono: "abrir"
                            texto: "Abrir carpeta"
                            implicitHeight: 38
                            onPulsado: pagina.editor.showLastExport()
                        }
                        BotonIcono {
                            visible: pagina.editor.exporting
                            icono: "cerrar"
                            texto: "Cancelar"
                            implicitHeight: 38
                            onPulsado: pagina.editor.cancelExport()
                        }
                        BotonIcono {
                            icono: "exportar"
                            texto: "Exportar"
                            acentuado: true
                            implicitHeight: 38
                            implicitWidth: 140
                            enabled: pagina.listo
                            onPulsado: pagina.exportar()
                        }
                    }
                }
            }
        }
    }

    component Etiqueta: Text {
        property string texto
        text: texto.toUpperCase()
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
        font.letterSpacing: 0.6
    }

    component Separador: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: 1
        color: Theme.borde
    }

    // Caja de texto con el estilo del tema (lo de dentro va en una fila).
    component Campo: Rectangle {
        default property alias contenido: fila.data
        property bool valido: true
        Layout.fillWidth: true
        implicitHeight: 34
        radius: Theme.radio
        color: Theme.fondo
        border.color: !valido ? Theme.aviso : Theme.borde
        RowLayout {
            id: fila
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
        }
    }

    // Botones de opción excluyentes, en una fila.
    component Opciones: RowLayout {
        id: opciones
        property var opciones: []
        property int elegida: 0
        signal elegir(int indice)
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: opciones.opciones
            delegate: BotonIcono {
                required property string modelData
                required property int index
                Layout.fillWidth: true
                texto: modelData
                implicitHeight: 32
                activo: opciones.elegida === index
                onPulsado: opciones.elegir(index)
            }
        }
    }

    component Resumen: ColumnLayout {
        property string titulo
        property string valor
        spacing: 2
        Text {
            text: parent.titulo.toUpperCase()
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.letterSpacing: 0.6
        }
        Text {
            text: parent.valor
            color: Theme.texto
            font.pixelSize: Theme.tamTexto
            font.bold: true
        }
    }
}
