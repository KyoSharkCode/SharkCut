import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Versión nueva de SharkCut: sus novedades y descargarla. Con el instalador descargado,
// se instala al cerrar SharkCut (pregunta antes por los proyectos sin guardar) y vuelve a
// abrirse sola.
Popup {
    id: dialogo

    // Se llama para cerrar la app e instalar (la ventana pregunta por lo que no se guardó).
    signal cerrarEInstalar()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
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

    readonly property string estado: Updater.state

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: "SharkCut " + Updater.latestVersion + " está disponible"
            color: Theme.texto
            font.pixelSize: 16
            font.bold: true
        }
        Text {
            text: "Tienes la versión " + Updater.currentVersion + "."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }
        Rectangle {
            visible: Updater.notes.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(240, novedades.implicitHeight + 20)
            radius: Theme.radio
            color: Theme.fondo
            border.color: Theme.borde
            Flickable {
                id: vistaNovedades
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                contentHeight: novedades.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: BarraDesplazamiento {}
                Text {
                    id: novedades
                    width: vistaNovedades.width - 12
                    text: Updater.notes
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: Theme.texto
                    font.pixelSize: Theme.tamTextoPequeno
                    onLinkActivated: (enlace) => Qt.openUrlExternally(enlace)
                }
            }
        }

        // Descarga en curso
        ColumnLayout {
            visible: dialogo.estado === "descargando"
            Layout.fillWidth: true
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 6
                radius: 3
                color: Theme.fondo
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, Updater.progress))
                    height: parent.height
                    radius: 3
                    color: Theme.acento
                }
            }
            Text {
                text: Updater.message + " " + Math.round(Updater.progress * 100) + " %"
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
        }
        Text {
            visible: dialogo.estado === "lista" || dialogo.estado === "error"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialogo.estado === "lista"
                  ? "Descargada. Al cerrar SharkCut se instala (te pregunta antes por los "
                    + "proyectos sin guardar) y se vuelve a abrir sola."
                  : Updater.message
            color: dialogo.estado === "error" ? Theme.error : Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Boton {
                texto: "Ver en GitHub"
                onPulsado: Updater.openReleasePage()
            }
            Item { Layout.fillWidth: true }
            Boton {
                texto: dialogo.estado === "descargando" ? "Cancelar" : "Más tarde"
                onPulsado: {
                    if (dialogo.estado === "descargando")
                        Updater.cancelDownload()
                    else
                        dialogo.close()
                }
            }
            Boton {
                visible: dialogo.estado !== "descargando"
                acentuado: true
                texto: dialogo.estado === "lista" ? "Cerrar e instalar"
                       : Updater.canInstall ? "Descargar e instalar" : "Ir a la descarga"
                onPulsado: {
                    if (dialogo.estado === "lista") {
                        Updater.installOnExit()
                        dialogo.close()
                        dialogo.cerrarEInstalar()
                    } else {
                        Updater.download()
                    }
                }
            }
        }
    }
}
