import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import SharkCut

// Guarda el aspecto de un clip de video como ajuste (preset): nombre, qué partes lleva
// y, si tiene animación, desde dónde se cuenta (inicio o final del clip). Las partes
// elegidas se recuerdan para la próxima vez.
Popup {
    id: dialogo

    required property Editor editor
    property var clipId: 0        // id del clip de video
    property bool animado: false  // ¿el clip tiene keyframes?

    function guardar(clip, tieneAnimacion) {
        clipId = clip
        animado = tieneAnimacion
        open()
    }

    Settings {
        id: recuerdo
        category: "efectos/dialogoAjuste"
        property bool colocacion: true
        property bool opacidad: true
        property bool recorte: true
        property bool efectos: true
        property bool croma: true
        property bool animacion: true
        property bool desdeFinal: false
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
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

    onAboutToShow: {
        campo.text = ""
        campo.forceActiveFocus()
    }

    readonly property string nombre: campo.text.trim()
    readonly property bool algunaParte: recuerdo.colocacion || recuerdo.opacidad
                                        || recuerdo.recorte || recuerdo.efectos || recuerdo.croma
    readonly property bool existe: nombre.length > 0 && editor.clipPresets.some((p) => p.name === nombre)

    function aceptar() {
        if (nombre.length === 0 || !algunaParte)
            return
        const guardado = editor.saveClipPreset(clipId, nombre, {
            placement: recuerdo.colocacion,
            opacity: recuerdo.opacidad,
            crop: recuerdo.recorte,
            effects: recuerdo.efectos,
            chroma: recuerdo.croma,
            animation: recuerdo.animacion,
            anchor: recuerdo.desdeFinal ? "end" : "start"
        })
        if (guardado)
            close()
    }

    component Parte: BotonIcono {
        Layout.fillWidth: true
        implicitHeight: 28
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: "Guardar como ajuste"
            color: Theme.texto
            font.pixelSize: 16
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "El ajuste aparece en la pestaña Efectos: arrástralo sobre otro clip de video "
                  + "para darle el mismo aspecto. Sirve en todos tus proyectos."
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            radius: Theme.radio
            color: Theme.fondo
            border.color: campo.activeFocus ? Theme.acento : Theme.borde
            TextField {
                id: campo
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                placeholderText: "Nombre del ajuste"
                placeholderTextColor: Theme.textoDesactivado
                background: null
                padding: 0
                selectByMouse: true
                onAccepted: dialogo.aceptar()
            }
        }
        Text {
            visible: dialogo.existe
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Ya hay un ajuste con este nombre: se sustituirá."
            color: Theme.aviso
            font.pixelSize: Theme.tamTextoPequeno
        }

        Text {
            text: "QUÉ LLEVA"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTextoPequeno
            font.letterSpacing: 1
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 6
            columnSpacing: 6
            Parte {
                texto: "Posición, escala y rotación"
                activo: recuerdo.colocacion
                onPulsado: recuerdo.colocacion = !recuerdo.colocacion
            }
            Parte {
                texto: "Opacidad"
                activo: recuerdo.opacidad
                onPulsado: recuerdo.opacidad = !recuerdo.opacidad
            }
            Parte {
                texto: "Recorte"
                activo: recuerdo.recorte
                onPulsado: recuerdo.recorte = !recuerdo.recorte
            }
            Parte {
                texto: "Efectos"
                activo: recuerdo.efectos
                ayuda: "Blanco y negro, desenfoque, esquinas, sombra y resplandor"
                onPulsado: recuerdo.efectos = !recuerdo.efectos
            }
            Parte {
                texto: "Pantalla verde"
                activo: recuerdo.croma
                onPulsado: recuerdo.croma = !recuerdo.croma
            }
            Parte {
                texto: "Animación (keyframes)"
                enabled: dialogo.animado
                activo: dialogo.animado && recuerdo.animacion
                ayuda: "Los keyframes de las partes elegidas"
                onPulsado: recuerdo.animacion = !recuerdo.animacion
            }
        }

        ColumnLayout {
            visible: dialogo.animado && recuerdo.animacion
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: "LA ANIMACIÓN EMPIEZA"
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTextoPequeno
                font.letterSpacing: 1
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Parte {
                    texto: "Desde el inicio del clip"
                    activo: !recuerdo.desdeFinal
                    ayuda: "Para entradas: los keyframes cuentan desde el principio"
                    onPulsado: recuerdo.desdeFinal = false
                }
                Parte {
                    texto: "Desde el final del clip"
                    activo: recuerdo.desdeFinal
                    ayuda: "Para salidas: los keyframes acaban donde acaba el clip"
                    onPulsado: recuerdo.desdeFinal = true
                }
            }
        }
        Text {
            visible: !dialogo.animado || !recuerdo.animacion
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Sin animación se guardan los valores que se ven en el cabezal."
            color: Theme.textoDesactivado
            font.pixelSize: Theme.tamTextoPequeno
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Item { Layout.fillWidth: true }
            Boton { texto: "Cancelar"; onPulsado: dialogo.close() }
            Boton {
                texto: dialogo.existe ? "Sustituir" : "Guardar"
                acentuado: true
                enabled: dialogo.nombre.length > 0 && dialogo.algunaParte
                onPulsado: dialogo.aceptar()
            }
        }
    }
}
