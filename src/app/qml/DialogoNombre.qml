import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Pide un nombre (renombrar una secuencia o una carpeta, carpeta nueva…).
Popup {
    id: dialogo

    property string titulo: "Renombrar"
    property string textoBoton: "Aceptar"
    property string inicial: ""
    signal aceptado(string nombre)

    // Abre el diálogo; `alAceptar` recibe el nombre escrito (sin espacios a los lados).
    property var alAceptar: null
    function pedir(titulo, inicial, textoBoton, alAceptar) {
        dialogo.titulo = titulo
        dialogo.inicial = inicial
        dialogo.textoBoton = textoBoton
        dialogo.alAceptar = alAceptar
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 380
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
        campo.text = inicial
        campo.selectAll()
        campo.forceActiveFocus()
    }

    function aceptar() {
        const nombre = campo.text.trim()
        if (nombre.length === 0)
            return
        close()
        if (alAceptar)
            alAceptar(nombre)
        aceptado(nombre)
    }

    contentItem: ColumnLayout {
        spacing: 14
        Text {
            text: dialogo.titulo
            color: Theme.texto
            font.pixelSize: 16
            font.bold: true
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
                background: null
                padding: 0
                selectByMouse: true
                onAccepted: dialogo.aceptar()
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Boton { texto: "Cancelar"; onPulsado: dialogo.close() }
            Boton {
                texto: dialogo.textoBoton
                acentuado: true
                enabled: campo.text.trim().length > 0
                onPulsado: dialogo.aceptar()
            }
        }
    }
}
