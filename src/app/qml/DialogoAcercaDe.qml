import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Acerca de SharkCut: versión, copyright, licencia y créditos de las bibliotecas que usa.
Popup {
    id: dialogo

    // Página de Ko-fi para apoyar SharkCut. Mientras esté vacía, no se muestra el botón.
    readonly property string enlaceKofi: ""
    readonly property string repositorio: "https://github.com/KyoSharkCode/SharkCut"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 500
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

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            spacing: 12
            Rectangle {
                implicitWidth: 52
                implicitHeight: 52
                radius: 13
                color: Theme.marca.abisal
                border.color: Theme.marca.aleta
                Icono {
                    anchors.centerIn: parent
                    nombre: "aleta"
                    tam: 30
                    color: Theme.cabezal
                }
            }
            ColumnLayout {
                spacing: 1
                Text {
                    text: "SharkCut"
                    color: Theme.texto
                    font.pixelSize: 22
                    font.bold: true
                }
                Text {
                    text: "Versión " + AppInfo.appVersion
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                }
                Text {
                    text: "© 2026 KyoSumiVT"
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                }
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Editor de video para creadores, pensado para VODs largos y shorts."
            color: Theme.texto
            font.pixelSize: Theme.tamTexto
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            linkColor: Theme.acento
            text: "SharkCut es software libre bajo la licencia "
                  + "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GPL 3.0</a>: puedes "
                  + "usarlo, estudiarlo, modificarlo y compartirlo. Se ofrece sin ninguna "
                  + "garantía. El nombre, el logotipo y el icono de SharkCut no forman parte de "
                  + "la licencia GPL."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            onLinkActivated: (enlace) => Qt.openUrlExternally(enlace)
        }

        Text {
            Layout.topMargin: 4
            text: "HECHO CON"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.letterSpacing: 0.6
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: creditos.implicitHeight + 20
            radius: Theme.radio
            color: Theme.fondo
            border.color: Theme.borde
            ColumnLayout {
                id: creditos
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                spacing: 5
                Repeater {
                    model: [
                        { nombre: "FFmpeg", licencia: "GPL 3.0 (incluye x264 y x265)",
                          enlace: "https://ffmpeg.org" },
                        { nombre: "Qt " + AppInfo.qtVersion, licencia: "LGPL 3.0",
                          enlace: "https://www.qt.io" },
                        { nombre: "whisper.cpp", licencia: "MIT",
                          enlace: "https://github.com/ggml-org/whisper.cpp" },
                        { nombre: "miniaudio", licencia: "Dominio público (Unlicense) o MIT-0",
                          enlace: "https://miniaud.io" },
                        { nombre: "nlohmann/json", licencia: "MIT",
                          enlace: "https://github.com/nlohmann/json" }
                    ]
                    delegate: RowLayout {
                        id: credito
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 14
                        Text {
                            Layout.preferredWidth: 120
                            textFormat: Text.StyledText
                            linkColor: Theme.acento
                            text: "<a href=\"" + credito.modelData.enlace + "\">"
                                  + credito.modelData.nombre + "</a>"
                            font.pixelSize: Theme.tamTextoPequeno
                            onLinkActivated: (enlace) => Qt.openUrlExternally(enlace)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: credito.modelData.licencia
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Los textos de cada licencia van en la carpeta «licenses» de la instalación."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Boton {
                texto: "Código"
                onPulsado: Qt.openUrlExternally(dialogo.repositorio)
            }
            Boton {
                texto: "Licencias"
                onPulsado: Qt.openUrlExternally(dialogo.repositorio
                                                + "/blob/main/licenses/LEEME.md")
            }
            Boton {
                texto: "Privacidad"
                onPulsado: Qt.openUrlExternally("https://kyosharkcode.github.io/SharkCut/privacidad.html")
            }
            Item { Layout.fillWidth: true }
            BotonIcono {
                visible: dialogo.enlaceKofi.length > 0
                icono: "corazon"
                texto: "Apoya SharkCut"
                acentuado: true
                onPulsado: Qt.openUrlExternally(dialogo.enlaceKofi)
            }
            Boton {
                texto: "Cerrar"
                onPulsado: dialogo.close()
            }
        }
    }
}
