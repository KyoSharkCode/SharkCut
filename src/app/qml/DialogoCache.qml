import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SharkCut

// Caché de medios: dónde se guardan las ondas y los proxies (común a todos los proyectos),
// cuánto ocupa, y vaciarla o cambiarla de carpeta.
Popup {
    id: dialogo

    required property var editor
    property bool confirmando: false // vaciar pide un segundo clic

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    focus: true
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.velo }
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radio
        border.color: Theme.borde
    }

    onOpened: {
        confirmando = false
        editor.refreshCacheSize()
    }

    function tamano(bytes) {
        if (bytes < 0)
            return "Calculando…"
        if (bytes < 1024 * 1024)
            return Math.round(bytes / 1024) + " KB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(0) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1).replace(".", ",") + " GB"
    }

    FolderDialog {
        id: elegirCarpeta
        title: "Carpeta de la caché de medios"
        onAccepted: dialogo.editor.cacheFolder = selectedFolder
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: "Caché de medios"
            color: Theme.texto
            font.pixelSize: 18
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Las ondas del audio y los proxies se guardan aquí, una vez por archivo: si "
                  + "dos proyectos usan el mismo video, comparten su caché. Vaciarla no toca tus "
                  + "videos ni tus proyectos; lo que haga falta se vuelve a crear."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: datos.implicitHeight + 20
            radius: Theme.radio
            color: Theme.fondo
            border.color: Theme.borde
            ColumnLayout {
                id: datos
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                spacing: 6
                Text {
                    text: "Ocupa " + dialogo.tamano(dialogo.editor.cacheBytes)
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: dialogo.editor.cacheFolder
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                    elide: Text.ElideMiddle
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            BotonIcono {
                icono: "carpeta"
                texto: "Abrir"
                onPulsado: dialogo.editor.openCacheFolder()
            }
            BotonIcono {
                texto: "Cambiar la carpeta…"
                ayuda: "Por ejemplo, a un disco con más espacio. Lo que ya hay se queda en la "
                       + "carpeta anterior"
                onPulsado: elegirCarpeta.open()
            }
            Item { Layout.fillWidth: true }
            BotonIcono {
                icono: dialogo.confirmando ? "" : "quitar"
                texto: dialogo.confirmando ? "¿Vaciar?" : "Vaciar"
                acentuado: dialogo.confirmando
                ayuda: "Borra las ondas y los proxies guardados"
                onPulsado: {
                    if (dialogo.confirmando) {
                        dialogo.editor.clearCache()
                        dialogo.confirmando = false
                    } else {
                        dialogo.confirmando = true
                    }
                }
            }
        }
    }
}
