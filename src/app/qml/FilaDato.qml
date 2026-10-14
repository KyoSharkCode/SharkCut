import QtQuick
import QtQuick.Layouts
import SharkCut

// Fila "etiqueta: valor" de los paneles laterales.
ColumnLayout {
    property string etiqueta
    property string valor
    property color colorValor: Theme.texto

    Layout.fillWidth: true
    spacing: 2

    Text {
        text: parent.etiqueta.toUpperCase()
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
        font.letterSpacing: 1
    }
    Text {
        Layout.fillWidth: true
        text: parent.valor
        color: parent.colorValor
        font.pixelSize: Theme.tamTexto
        elide: Text.ElideMiddle
    }
}
