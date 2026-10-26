import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import SharkCut

// Opciones de los subtítulos: diseño (una línea, dos o una sola palabra), caracteres por
// línea, duración mínima, separación y puntuación.
// Modo "crear": también idioma, audio y ajuste de estilo, para transcribir. Modo
// "dividir": vuelve a dividir una pista que ya existe. Las medidas se recuerdan.
Popup {
    id: dialogo

    required property Editor editor
    property string modo: "crear" // "crear" | "dividir"
    property int pista: -1       // pista a dividir
    // {lines, maxChars, minSeconds, gapFrames, removePunctuation} y, al crear, también
    // {language, audioTrack (−1 = toda la mezcla), preset, quality}
    signal aceptado(var opciones)

    function crear() {
        modo = "crear"
        dialogo.editor.checkSpeechModels() // ¿qué modelos están ya descargados?
        open()
    }

    // Texto de cada precisión: «descargado» o lo que se descargaría la primera vez.
    function textoModelo(indice, nombre) {
        const m = dialogo.editor.speechModels[indice]
        if (!m)
            return nombre
        return nombre + (m.downloaded ? "; descargado" : "; descarga de ~" + m.megabytes + " MB")
    }
    function dividir(indice) {
        modo = "dividir"
        pista = indice
        open()
    }

    Settings {
        id: recuerdo
        category: "subtitulos"
        property string lineas: "double"
        property int caracteres: 42
        property real duracionMinima: 3.0
        property int separacion: 0
        property bool sinPuntuacion: false
        property int idioma: 0
        property string ajuste: "Clásico"
        property int calidad: 0
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 460
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.velo }
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radio
        border.color: Theme.borde
    }

    readonly property var idiomas: [
        { codigo: "es", nombre: "Español" },
        { codigo: "en", nombre: "Inglés" },
        { codigo: "auto", nombre: "Detectar automáticamente" }
    ]
    readonly property var pistasAudio: editor.tracks.filter((t) => !t.video)
    property int audioElegido: 0

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: dialogo.modo === "crear" ? "Crear subtítulos" : "Volver a dividir los subtítulos"
            color: Theme.texto
            font.pixelSize: 16
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialogo.modo === "crear"
                  ? "SharkCut escucha el audio de la secuencia y escribe lo que se dice, en tu PC "
                    + "(sin internet). Los subtítulos van a una pista nueva, ya divididos."
                  : "Se dividen de nuevo con estas medidas, manteniendo el momento de cada palabra. "
                    + "El estilo propio de cada subtítulo se pierde."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }

        // --- Solo al crear: qué se transcribe ---
        Selector {
            visible: dialogo.modo === "crear"
            etiqueta: "Idioma"
            model: dialogo.idiomas.map((i) => i.nombre)
            indice: recuerdo.idioma
            onElegido: (i) => recuerdo.idioma = i
        }
        Selector {
            id: selectorAudio
            visible: dialogo.modo === "crear"
            etiqueta: "Audio"
            model: ["Toda la mezcla"].concat(dialogo.pistasAudio.map((t) => "Solo " + t.name))
            indice: dialogo.audioElegido
            onElegido: (i) => dialogo.audioElegido = i
        }
        Selector {
            visible: dialogo.modo === "crear"
            etiqueta: "Precisión"
            model: [dialogo.textoModelo(0, "Alta (recomendada") + ")",
                    dialogo.textoModelo(1, "Rápida (menos precisa") + ")"]
            indice: recuerdo.calidad
            onElegido: (i) => recuerdo.calidad = i
        }
        Selector {
            id: selectorAjuste
            visible: dialogo.modo === "crear"
            etiqueta: "Estilo"
            readonly property var nombres: dialogo.editor.captionPresets.map((p) => p.name)
            model: nombres
            indice: Math.max(0, nombres.indexOf(recuerdo.ajuste))
            onElegido: (i) => recuerdo.ajuste = nombres[i]
        }

        // --- Diseño ---
        Text {
            text: "DISEÑO"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.letterSpacing: 1
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [{ clave: "single", texto: "Una sola línea" },
                        { clave: "double", texto: "Línea doble" },
                        { clave: "word", texto: "Una sola palabra" }]
                delegate: BotonIcono {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 28
                    texto: modelData.texto
                    activo: recuerdo.lineas === modelData.clave
                    onPulsado: recuerdo.lineas = modelData.clave
                }
            }
        }
        CampoNumero {
            etiqueta: "Caracteres por línea"
            anchoEtiqueta: 150
            enabled: recuerdo.lineas !== "word"
            opacity: enabled ? 1 : 0.4
            desde: 1
            hasta: 200
            paso: 0.2
            valor: recuerdo.caracteres
            onCambiado: (v) => recuerdo.caracteres = Math.round(v)
        }
        CampoNumero {
            etiqueta: "Duración mínima"
            anchoEtiqueta: 150
            unidad: "s"
            decimales: 1
            desde: 0
            hasta: 10
            paso: 0.02
            valor: recuerdo.duracionMinima
            onCambiado: (v) => recuerdo.duracionMinima = v
        }
        CampoNumero {
            etiqueta: "Separación"
            anchoEtiqueta: 150
            unidad: "fot."
            desde: 0
            hasta: 60
            paso: 0.1
            valor: recuerdo.separacion
            onCambiado: (v) => recuerdo.separacion = Math.round(v)
        }
        BotonIcono {
            Layout.fillWidth: true
            implicitHeight: 28
            texto: "Quitar puntuación"
            activo: recuerdo.sinPuntuacion
            ayuda: "Sin puntos, comas, signos de interrogación ni de exclamación"
            onPulsado: recuerdo.sinPuntuacion = !recuerdo.sinPuntuacion
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Item { Layout.fillWidth: true }
            BotonIcono {
                texto: "Cancelar"
                onPulsado: dialogo.close()
            }
            BotonIcono {
                texto: dialogo.modo === "crear" ? "Transcribir y crear subtítulos" : "Dividir"
                acentuado: true
                onPulsado: {
                    const opciones = {
                        lines: recuerdo.lineas,
                        maxChars: recuerdo.caracteres,
                        minSeconds: recuerdo.duracionMinima,
                        gapFrames: recuerdo.separacion,
                        removePunctuation: recuerdo.sinPuntuacion
                    }
                    if (dialogo.modo === "crear") {
                        opciones.language = dialogo.idiomas[recuerdo.idioma].codigo
                        const elegida = dialogo.audioElegido
                        opciones.audioTrack = elegida > 0 ? dialogo.pistasAudio[elegida - 1].index : -1
                        opciones.preset = recuerdo.ajuste
                        opciones.quality = recuerdo.calidad
                    }
                    dialogo.close()
                    dialogo.aceptado(opciones)
                }
            }
        }
    }
}
