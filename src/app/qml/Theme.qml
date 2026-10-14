pragma Singleton

import QtQuick

// Tema "Marea Profunda": versión sobria de la marca Marea Nocturna para el editor.
//
// Un editor se mira durante horas, así que la base son grises carbón neutros (con un
// matiz azul muy leve) y la marca solo aparece en acentos: cabezal, selección, foco y
// detalles. El entorno del visor es casi negro y neutro para no alterar la percepción
// del color del video.
QtObject {
    // Superficies (de más profunda a más elevada)
    readonly property color fondo: "#14161C"
    readonly property color panel: "#1B1E26"
    readonly property color panelElevado: "#232733"
    readonly property color borde: "#2C3140"
    readonly property color visorFondo: "#0B0C10"

    // Texto (nunca blanco puro: cansa menos la vista)
    readonly property color texto: "#E6E8EE"
    readonly property color textoSecundario: "#9AA3B5"
    readonly property color textoDesactivado: "#5F6778"

    // Acentos derivados de la marca, con saturación contenida
    readonly property color acento: "#4FC3D1"      // Bioluz suavizado: foco, selección
    readonly property color cabezal: "#6FE3F0"     // Bioluz puro: solo el cabezal de reproducción
    readonly property color acentoSecundario: "#3A7BD5" // Aleta

    // Colores de clips en la línea de tiempo (apagados para no competir con el video)
    readonly property color clipVideo: "#3D5A8A"
    readonly property color clipAudio: "#2F6F6A"
    readonly property color clipTransicion: "#7D74A8"
    readonly property color clipTexto: "#9C8340"
    // Versiones claras para iconos de tipo de medio sobre fondos oscuros
    readonly property color clipVideoClaro: "#7FA2DA"
    readonly property color clipAudioClaro: "#6FC1B8"
    // Pistas de la línea de tiempo: un tono azulado para las de video y uno verdoso para
    // las de audio (fondo de la fila y cabecera), muy suaves para no cansar.
    readonly property color pistaVideo: "#161C29"
    readonly property color pistaAudio: "#14201F"
    readonly property color cabeceraVideo: "#1F2940"
    readonly property color cabeceraAudio: "#1C2E2C"

    // Fondo semitransparente detrás de un diálogo
    readonly property color velo: "#99000000"

    // Estados
    readonly property color ok: "#4FBF8F"
    readonly property color aviso: "#E0B440"
    readonly property color error: "#D0465F"

    // Marca original, reservada para pantalla de inicio, logo y momentos puntuales
    readonly property QtObject marca: QtObject {
        readonly property color abisal: "#0A1030"
        readonly property color indigo: "#2B2F7A"
        readonly property color aleta: "#3A7BD5"
        readonly property color bioluz: "#6FE3F0"
        readonly property color espuma: "#C9E6F2"
        readonly property color lavanda: "#BDB0E6"
        readonly property color carmesi: "#B3263F"
        readonly property color perla: "#F5F3FB"
        readonly property color oroLuna: "#F4C542"
    }

    // Medidas
    readonly property int radio: 6
    readonly property int margen: 8
    readonly property int tamTexto: 13
    readonly property int tamTextoPequeno: 11
    readonly property int tamTitulo: 18
}
