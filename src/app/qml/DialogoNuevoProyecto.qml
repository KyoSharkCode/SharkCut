import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Nuevo proyecto: nombre, lienzo (16:9, 9:16 o 1:1) y fotogramas por segundo.
// El lienzo es del proyecto: se edita en él y se exporta en él.
Popup {
    id: dialogo

    property int lienzo: 0 // 0 horizontal, 1 vertical, 2 cuadrado
    property int ritmo: 0  // 0 → 60 fps, 1 → 30 fps, 2 → como el primer clip
    signal crear(string nombre, int ancho, int alto, int fps)

    readonly property var lienzos: [
        { nombre: "Horizontal 16:9", tam: "1920×1080", uso: "YouTube, VODs", ancho: 1920, alto: 1080, que: "horizontal" },
        { nombre: "Vertical 9:16", tam: "1080×1920", uso: "Shorts, TikTok, Reels", ancho: 1080, alto: 1920, que: "vertical" },
        { nombre: "Cuadrado 1:1", tam: "1080×1080", uso: "Instagram", ancho: 1080, alto: 1080, que: "cuadrado" }
    ]

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 600
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.velo }
    background: Rectangle {
        color: Theme.panel
        radius: Theme.radio
        border.color: Theme.borde
    }

    onAboutToShow: {
        nombre.text = ""
        nombre.forceActiveFocus()
    }

    function aceptar() {
        const l = lienzos[lienzo]
        crear(nombre.text.trim(), l.ancho, l.alto, ritmo === 0 ? 60 : ritmo === 1 ? 30 : 0)
        close()
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 20
            Text {
                Layout.fillWidth: true
                text: "Nuevo proyecto"
                color: Theme.texto
                font.pixelSize: 17
                font.bold: true
            }
            BotonIcono { icono: "cerrar"; ayuda: "Cerrar (Esc)"; onPulsado: dialogo.close() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borde }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 18

            Etiqueta { texto: "Nombre" }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: -10
                implicitHeight: 36
                radius: Theme.radio
                color: Theme.fondo
                border.color: nombre.activeFocus ? Theme.acento : Theme.borde
                TextField {
                    id: nombre
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    placeholderText: "Sin título"
                    placeholderTextColor: Theme.textoDesactivado
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                    background: null
                    padding: 0
                    selectByMouse: true
                    onAccepted: dialogo.aceptar()
                }
            }

            Etiqueta { texto: "Lienzo" }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: -10
                spacing: 8
                Repeater {
                    model: dialogo.lienzos
                    delegate: Rectangle {
                        id: opcion
                        required property var modelData
                        required property int index
                        readonly property bool elegida: dialogo.lienzo === index
                        Layout.fillWidth: true
                        implicitHeight: 124
                        radius: Theme.radio
                        color: elegida ? Theme.panelElevado : Theme.fondo
                        border.color: elegida ? Theme.acento : Theme.borde
                        Column {
                            anchors.centerIn: parent
                            spacing: 4
                            Item {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 44
                                height: 40
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: opcion.modelData.ancho >= opcion.modelData.alto
                                           ? 44 : 44 * opcion.modelData.ancho / opcion.modelData.alto * 0.9
                                    height: opcion.modelData.ancho >= opcion.modelData.alto
                                            ? 44 * opcion.modelData.alto / opcion.modelData.ancho : 40
                                    radius: 3
                                    color: "transparent"
                                    border.width: 2
                                    border.color: opcion.elegida ? Theme.acento : Theme.textoDesactivado
                                }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: opcion.modelData.nombre
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                font.bold: true
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: opcion.modelData.tam + " · " + opcion.modelData.uso
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTextoPequeno
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: dialogo.lienzo = opcion.index
                        }
                    }
                }
            }

            Etiqueta { texto: "Fotogramas por segundo" }
            RowLayout {
                Layout.topMargin: -10
                spacing: 8
                Repeater {
                    model: ["60 fps", "30 fps", "Como el primer clip"]
                    delegate: BotonIcono {
                        required property string modelData
                        required property int index
                        texto: modelData
                        implicitHeight: 34
                        activo: dialogo.ritmo === index
                        onPulsado: dialogo.ritmo = index
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                lineHeight: 1.2
                text: "Editarás en un lienzo " + dialogo.lienzos[dialogo.lienzo].que + " ("
                      + dialogo.lienzos[dialogo.lienzo].tam + "). Los videos de OBS (16:9) se "
                      + "colocan ajustados al lienzo; para reencuadrarlos (mover y escalar) "
                      + "llegan las propiedades del clip en la etapa 2."
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borde }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.rightMargin: 20
            spacing: 10
            Item { Layout.fillWidth: true }
            BotonIcono { texto: "Cancelar"; implicitHeight: 36; onPulsado: dialogo.close() }
            BotonIcono {
                icono: "nuevo"
                texto: "Crear proyecto"
                acentuado: true
                implicitHeight: 36
                onPulsado: dialogo.aceptar()
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
}
