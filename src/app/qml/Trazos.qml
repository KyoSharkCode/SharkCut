pragma Singleton

import QtQuick

// Trazos de los iconos de Icono.qml (sintaxis de rutas SVG, rejilla de 24 × 24).
// Están aparte, en un único objeto, para no repetir el diccionario en cada icono (la
// línea de tiempo puede tener cientos). Añadir aquí los nuevos.
QtObject {
    readonly property var trazos: ({
        "seleccion": "M5 3l14 8-6 2-2 6z",
        "cuchilla": "M9 6a3 3 0 1 1-6 0a3 3 0 1 1 6 0M9 18a3 3 0 1 1-6 0a3 3 0 1 1 6 0M20 4L8.1 15.9M14.5 14.5L20 20M8.1 8.1L12 12",
        "iman": "M6 3v8a6 6 0 0 0 12 0V3M6 7h4M14 7h4",
        "dividir": "M12 3v18M3 8h6v8H3M21 8h-6v8h6",
        "quitar": "M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3",
        "deshacer": "M9 14L4 9l5-5M4 9h11a5 5 0 0 1 0 10h-3",
        "rehacer": "M15 14l5-5-5-5M20 9H9a5 5 0 0 0 0 10h3",
        "acercar": "M18 11a7 7 0 1 1-14 0a7 7 0 1 1 14 0M20 20l-3.5-3.5M8 11h6M11 8v6",
        "alejar": "M18 11a7 7 0 1 1-14 0a7 7 0 1 1 14 0M20 20l-3.5-3.5M8 11h6",
        "ajustar": "M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5",
        "nuevo": "M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9zM14 3v6h6",
        "abrir": "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",
        "guardar": "M5 3h11l3 3v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM7 3v5h8V3M7 21v-7h10v7",
        "exportar": "M12 3v12M7 10l5 5 5-5M4 19h16",
        "importar": "M12 15V3M7 8l5-5 5 5M4 19h16",
        "inicio": "M6 5v14M18 6l-8 6 8 6z",
        "anterior": "M15 6l-6 6 6 6",
        "reproducir": "M7 5l12 7-12 7z",
        "pausa": "M7 5h3v14H7zM14 5h3v14h-3z",
        "siguiente": "M9 6l6 6-6 6",
        "final": "M18 5v14M6 6l8 6-8 6z",
        "sonido": "M4 9v6h4l5 4V5L8 9zM16 9a4 4 0 0 1 0 6",
        "silencio": "M4 9v6h4l5 4V5L8 9zM16 9l5 6M21 9l-5 6",
        "lista": "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01",
        "cuadricula": "M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z",
        "buscar": "M18 11a7 7 0 1 1-14 0a7 7 0 1 1 14 0M20 20l-3.5-3.5",
        "ojo": "M2 12c2-4 5.5-7 10-7s8 3 10 7c-2 4-5.5 7-10 7s-8-3-10-7zM15 12a3 3 0 1 1-6 0a3 3 0 1 1 6 0",
        "candado": "M7 11h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-6a2 2 0 0 1 2-2zM8 11V7a4 4 0 0 1 8 0v4",
        "candadoAbierto": "M7 11h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-6a2 2 0 0 1 2-2zM8 11V7a4 4 0 0 1 7.8-1.2",
        "ojoCerrado": "M2 12c2-4 5.5-7 10-7s8 3 10 7c-2 4-5.5 7-10 7s-8-3-10-7zM15 12a3 3 0 1 1-6 0a3 3 0 1 1 6 0M4 4l16 16",
        "desvincular": "M9 7H7a5 5 0 0 0 0 10h2M15 7h2a5 5 0 0 1 0 10h-2M12 4v3M12 17v3",
        "lienzo": "M8 3h8v18H8zM11 18h2",
        "rellenar": "M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5M8 8h8v8H8z",
        "restablecer": "M4 4v6h6M4.5 10A8 8 0 1 1 6 16.5",
        "mas": "M12 5v14M5 12h14",
        "cerrar": "M6 6l12 12M18 6L6 18",
        "transicion": "M4 12h16M14 6l6 6-6 6",
        "video": "M4 5h16v14H4zM4 9h16M4 15h16M8 5v4M16 5v4M8 15v4M16 15v4",
        "audio": "M9 18V5l12-2v13M9 18a3 3 0 1 1-6 0a3 3 0 1 1 6 0M21 16a3 3 0 1 1-6 0a3 3 0 1 1 6 0",
        "aleta": "M3 18c4-1 7-6 9-14 1 6 4 11 9 14M3 18h18",
        "keyframe": "M12 3l9 9-9 9-9-9z",
        "keyframeLleno": "M12 4l8 8-8 8-8-8z",
        "reloj": "M12 21a8 8 0 1 0 0-16a8 8 0 1 0 0 16M12 9v4l2.5 1.5M10 2.5h4M12 2.5v2.5",
        "carpeta": "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",
        "carpetaNueva": "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2zM12 10v6M9 13h6",
        "secuencia": "M3 6h9M15 6h6M3 12h5M11 12h10M3 18h12M18 18h3",
        "secuenciaNueva": "M3 6h9M3 12h9M3 18h7M18 13v8M14 17h8",
        "atras": "M15 6l-6 6 6 6M9 12h11",
        "proxy": "M3 6h12v9H3zM9 9h12v9H9z",
        "estrella": "M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8L12 16.8l-5.2 2.8 1-5.8-4.3-4.1 5.9-.9z",
        "estrellaLlena": "M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8L12 16.8l-5.2 2.8 1-5.8-4.3-4.1 5.9-.9z",
        "gotero": "M16 3.5a2.1 2.1 0 0 1 3 3L17 8.5l-3-3zM13 6.5l4 4M14.5 8L6 16.5V19h2.5L17 10.5"
    })
}
