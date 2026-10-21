import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import SharkCut

// Elegir una fuente: lista que se desplaza, búsqueda escribiendo y favoritas (arriba,
// con estrella; se recuerdan entre sesiones). Cada fuente se ve escrita con ella misma.
ColumnLayout {
    id: raiz

    property string etiqueta: "Fuente"
    property string fuente // la elegida ahora
    signal elegida(string fuente)

    Layout.fillWidth: true
    spacing: 4

    Settings {
        id: recuerdo
        category: "fuentes"
        property string favoritas: "" // nombres separados por saltos de línea
    }
    readonly property var favoritas: recuerdo.favoritas.length > 0
                                     ? recuerdo.favoritas.split("\n") : []
    function esFavorita(nombre) { return favoritas.indexOf(nombre) >= 0 }
    function cambiarFavorita(nombre) {
        const lista = favoritas.slice()
        const i = lista.indexOf(nombre)
        if (i >= 0)
            lista.splice(i, 1)
        else
            lista.push(nombre)
        lista.sort((a, b) => a.localeCompare(b))
        recuerdo.favoritas = lista.join("\n")
    }

    // Todas las del sistema (y la elegida aunque no esté instalada, p. ej. un proyecto
    // de otro PC).
    readonly property var todas: {
        const lista = Qt.fontFamilies()
        return fuente === "" || lista.indexOf(fuente) >= 0 ? lista : [fuente].concat(lista)
    }
    // Lo que se ve: las favoritas primero y después el resto, filtradas por lo escrito.
    readonly property var visibles: {
        const texto = busqueda.text.trim().toLowerCase()
        const pasa = (f) => texto === "" || f.toLowerCase().indexOf(texto) >= 0
        const favs = todas.filter((f) => esFavorita(f) && pasa(f))
        const resto = soloFavoritas.activo ? [] : todas.filter((f) => !esFavorita(f) && pasa(f))
        return favs.concat(resto)
    }

    Text {
        text: raiz.etiqueta.toUpperCase()
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
        font.letterSpacing: 1
    }

    // Botón con la fuente actual (escrita con ella).
    Rectangle {
        id: boton
        Layout.fillWidth: true
        implicitHeight: 32
        radius: Theme.radio
        color: zonaBoton.containsMouse ? Theme.panelElevado : Theme.fondo
        border.color: lista.visible ? Theme.acento : Theme.borde
        Text {
            anchors.left: parent.left
            anchors.right: flecha.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: raiz.fuente
            font.family: raiz.fuente
            font.pixelSize: Theme.tamTexto + 1
            color: Theme.texto
            elide: Text.ElideRight
        }
        Text {
            id: flecha
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: Theme.textoSecundario
            font.pixelSize: Theme.tamTexto
        }
        MouseArea {
            id: zonaBoton
            anchors.fill: parent
            hoverEnabled: true
            onClicked: lista.visible ? lista.close() : lista.open()
        }
    }

    Popup {
        id: lista
        y: boton.y + boton.height + 2
        width: Math.max(boton.width, 280)
        height: 420
        margins: 8 // sin salirse de la ventana
        padding: 6
        focus: true
        onOpened: {
            busqueda.text = ""
            busqueda.forceActiveFocus()
            // La elegida, a la vista.
            const i = raiz.visibles.indexOf(raiz.fuente)
            if (i >= 0)
                vistaFuentes.positionViewAtIndex(i, ListView.Center)
        }
        background: Rectangle {
            color: Theme.panel
            radius: Theme.radio
            border.color: Theme.borde
        }

        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: busqueda
                    Layout.fillWidth: true
                    implicitHeight: 30
                    placeholderText: "Buscar fuente…"
                    placeholderTextColor: Theme.textoDesactivado
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                    selectByMouse: true
                    leftPadding: 28
                    background: Rectangle {
                        radius: Theme.radio
                        color: Theme.fondo
                        border.color: busqueda.activeFocus ? Theme.acento : Theme.borde
                        Icono {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            nombre: "buscar"
                            tam: 14
                            color: Theme.textoSecundario
                        }
                    }
                    // Intro: la primera que coincide. Flechas: moverse por la lista.
                    Keys.onReturnPressed: {
                        const i = Math.max(0, vistaFuentes.currentIndex)
                        if (raiz.visibles.length > 0)
                            raiz.elegir(raiz.visibles[Math.min(i, raiz.visibles.length - 1)])
                    }
                    Keys.onDownPressed: vistaFuentes.incrementCurrentIndex()
                    Keys.onUpPressed: vistaFuentes.decrementCurrentIndex()
                    Keys.onEscapePressed: lista.close()
                    onTextChanged: vistaFuentes.currentIndex = 0
                }
                BotonIcono {
                    id: soloFavoritas
                    icono: activo ? "estrellaLlena" : "estrella"
                    implicitWidth: 30
                    implicitHeight: 30
                    ayuda: activo ? "Ver todas las fuentes" : "Ver solo las favoritas"
                    onPulsado: activo = !activo
                }
            }

            ListView {
                id: vistaFuentes
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: raiz.visibles
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                ScrollBar.vertical: BarraDesplazamiento {}
                delegate: Rectangle {
                    id: fila
                    required property string modelData
                    required property int index
                    readonly property bool favorita: raiz.esFavorita(modelData)
                    // Separación entre favoritas y el resto.
                    readonly property bool primeraNormal: !favorita && index > 0
                        && raiz.esFavorita(raiz.visibles[index - 1])
                    width: vistaFuentes.width - 10
                    height: 34
                    radius: 4
                    color: fila.modelData === raiz.fuente ? Theme.panelElevado
                           : vistaFuentes.currentIndex === index || zonaFila.containsMouse
                             ? Theme.fondo : "transparent"
                    border.color: fila.modelData === raiz.fuente ? Theme.acento : "transparent"
                    Rectangle {
                        visible: fila.primeraNormal
                        width: parent.width
                        height: 1
                        color: Theme.borde
                    }
                    MouseArea {
                        id: zonaFila
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: raiz.elegir(fila.modelData)
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.right: estrella.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: fila.modelData
                        font.family: fila.modelData
                        font.pixelSize: 15
                        color: Theme.texto
                        elide: Text.ElideRight
                    }
                    // Estrella: marcar o quitar de favoritas (sin elegirla).
                    Rectangle {
                        id: estrella
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 26
                        radius: 4
                        color: zonaEstrella.containsMouse ? Theme.borde : "transparent"
                        Icono {
                            anchors.centerIn: parent
                            nombre: fila.favorita ? "estrellaLlena" : "estrella"
                            tam: 15
                            color: fila.favorita ? Theme.marcador : Theme.textoDesactivado
                        }
                        MouseArea {
                            id: zonaEstrella
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: raiz.cambiarFavorita(fila.modelData)
                        }
                        ToolTip.visible: zonaEstrella.containsMouse
                        ToolTip.delay: 600
                        ToolTip.text: fila.favorita ? "Quitar de favoritas" : "Marcar como favorita"
                    }
                }
            }
            Text {
                visible: raiz.visibles.length === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: soloFavoritas.activo && raiz.favoritas.length === 0
                      ? "Aún no tienes favoritas: márcalas con la estrella."
                      : "Ninguna fuente se llama así."
                color: Theme.textoDesactivado
                font.pixelSize: Theme.tamTextoPequeno
            }
        }
    }

    function elegir(nombre) {
        lista.close()
        if (nombre !== fuente)
            elegida(nombre)
    }
}
