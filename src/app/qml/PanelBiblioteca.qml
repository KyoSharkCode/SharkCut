import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Pestaña Medios: los proyectos abiertos (pestañas) y la biblioteca del activo, con sus
// carpetas, secuencias y medios.
//
// Las carpetas tienen un solo nivel. Un medio o una secuencia se mueve arrastrándolo
// sobre una carpeta (o sobre «volver», para sacarlo a la raíz).
ColumnLayout {
    id: biblioteca

    required property Editor editor
    required property Item fantasma
    property bool cuadricula: false
    signal importar()
    signal abrirEnOrigen(string ruta)
    signal nuevaSecuencia()
    signal cerrarProyecto(var uid)

    spacing: 10

    // Carpeta abierta de cada proyecto (uid → id de carpeta; 0 = raíz).
    property var carpetas: ({})
    readonly property var carpeta: {
        const id = carpetas[editor.activeProject] || 0
        return editor.folders.some((c) => c.id === id) ? id : 0
    }
    readonly property string nombreCarpeta: {
        const c = editor.folders.find((c) => c.id === carpeta)
        return c ? c.name : ""
    }
    function entrar(id) {
        const copia = Object.assign({}, carpetas)
        copia[editor.activeProject] = id
        carpetas = copia
    }

    readonly property var elementos: {
        const texto = buscador.text.trim().toLowerCase()
        const coincide = (e) => texto.length === 0 || e.name.toLowerCase().indexOf(texto) >= 0
        // Al buscar se mira en todas las carpetas.
        const aqui = (e) => texto.length > 0 || e.folder === carpeta
        const lista = []
        if (texto.length === 0 && carpeta === 0) {
            for (const c of editor.folders)
                lista.push(Object.assign({ tipo: "carpeta" }, c))
        }
        for (const s of editor.sequences) {
            if (aqui(s) && coincide(s))
                lista.push(Object.assign({ tipo: "secuencia" }, s))
        }
        for (const m of editor.mediaItems) {
            if (aqui(m) && coincide(m))
                lista.push(Object.assign({ tipo: "medio" }, m))
        }
        return lista
    }

    // --- Proyectos abiertos ---
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: biblioteca.editor.projects
            delegate: Rectangle {
                id: proyecto
                required property var modelData
                width: filaProyecto.implicitWidth + 16
                height: 28
                radius: Theme.radio
                color: modelData.active ? Theme.panelElevado
                       : zonaProyecto.containsMouse ? Theme.fondo : "transparent"
                border.color: modelData.active ? Theme.acento : Theme.borde
                MouseArea {
                    id: zonaProyecto
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: biblioteca.editor.activateProject(proyecto.modelData.uid)
                }
                Row {
                    id: filaProyecto
                    anchors.verticalCenter: parent.verticalCenter
                    x: 8
                    spacing: 6
                    Icono {
                        anchors.verticalCenter: parent.verticalCenter
                        nombre: "carpeta"
                        tam: 13
                        color: proyecto.modelData.active ? Theme.acento : Theme.textoSecundario
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: proyecto.modelData.name
                        color: proyecto.modelData.active ? Theme.texto : Theme.textoSecundario
                        font.pixelSize: Theme.tamTexto
                        font.bold: proyecto.modelData.active
                        width: Math.min(implicitWidth, 160)
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: proyecto.modelData.modified
                        width: 6
                        height: 6
                        radius: 3
                        color: Theme.acento
                    }
                    BotonCerrar {
                        ayuda: "Cerrar el proyecto"
                        onPulsado: biblioteca.cerrarProyecto(proyecto.modelData.uid)
                    }
                }
            }
        }
    }

    // --- Barra: búsqueda, vista y crear ---
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            radius: Theme.radio
            color: Theme.fondo
            border.color: buscador.activeFocus ? Theme.acento : Theme.borde
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 6
                spacing: 6
                Icono { nombre: "buscar"; tam: 14; color: Theme.textoDesactivado }
                TextField {
                    id: buscador
                    Layout.fillWidth: true
                    placeholderText: "Buscar"
                    placeholderTextColor: Theme.textoDesactivado
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                    background: null
                    padding: 0
                }
            }
        }
        BotonIcono {
            icono: "lista"
            ayuda: "Ver como lista"
            activo: !biblioteca.cuadricula
            onPulsado: biblioteca.cuadricula = false
        }
        BotonIcono {
            icono: "cuadricula"
            ayuda: "Ver como cuadrícula"
            activo: biblioteca.cuadricula
            onPulsado: biblioteca.cuadricula = true
        }
        BotonIcono {
            icono: "carpetaNueva"
            ayuda: "Nueva carpeta"
            onPulsado: dialogoNombre.pedir("Nueva carpeta", "Carpeta nueva", "Crear",
                                           (nombre) => biblioteca.editor.newFolder(nombre))
        }
        BotonIcono {
            icono: "secuenciaNueva"
            ayuda: "Nueva secuencia"
            onPulsado: biblioteca.nuevaSecuencia()
        }
        BotonIcono {
            icono: "importar"
            texto: "Importar…"
            ayuda: "Importar medios (Ctrl+I)"
            acentuado: true
            onPulsado: biblioteca.importar()
        }
    }

    // Dentro de una carpeta: volver a la raíz (también se puede soltar ahí).
    Rectangle {
        Layout.fillWidth: true
        visible: biblioteca.carpeta !== 0 && buscador.text.trim().length === 0
        implicitHeight: 30
        radius: Theme.radio
        color: zonaVolver.containsDrag ? Theme.panelElevado
               : zonaAtras.containsMouse ? Theme.fondo : "transparent"
        border.color: zonaVolver.containsDrag ? Theme.acento : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            spacing: 6
            Icono { nombre: "atras"; tam: 15; color: Theme.textoSecundario }
            Text {
                text: biblioteca.editor.projectName
                color: Theme.textoSecundario
                font.pixelSize: Theme.tamTexto
            }
            Text { text: "›"; color: Theme.textoDesactivado; font.pixelSize: Theme.tamTexto }
            Icono { nombre: "carpeta"; tam: 14; color: Theme.acento }
            Text {
                Layout.fillWidth: true
                text: biblioteca.nombreCarpeta
                color: Theme.texto
                font.pixelSize: Theme.tamTexto
                font.bold: true
                elide: Text.ElideRight
            }
        }
        MouseArea {
            id: zonaAtras
            anchors.fill: parent
            hoverEnabled: true
            onClicked: biblioteca.entrar(0)
        }
        DropArea {
            id: zonaVolver
            anchors.fill: parent
            keys: ["sharkcut/medio", "sharkcut/secuencia"]
            onDropped: (d) => {
                biblioteca.editor.moveToFolder([d.source.mediaId], 0)
                d.accept()
            }
        }
    }

    // --- Lista ---
    ListView {
        id: lista
        visible: !biblioteca.cuadricula
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: biblioteca.elementos
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: fila
            required property var modelData
            readonly property bool esCarpeta: modelData.tipo === "carpeta"
            readonly property bool esSecuencia: modelData.tipo === "secuencia"
            readonly property bool esMedio: modelData.tipo === "medio"
            width: ListView.view.width
            height: 42
            radius: Theme.radio
            color: soltarFila.containsDrag || zonaFila.containsMouse ? Theme.panelElevado
                   : Theme.fondo
            border.color: soltarFila.containsDrag ? Theme.acento
                          : esSecuencia && modelData.open ? Theme.acentoSecundario : Theme.borde

            Zona {
                id: zonaFila
                anchors.fill: parent
                elemento: fila.modelData
            }
            SoltarEnCarpeta {
                id: soltarFila
                anchors.fill: parent
                elemento: fila.modelData
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 8
                Icono {
                    nombre: fila.esCarpeta ? "carpeta" : fila.esSecuencia ? "secuencia"
                            : fila.modelData.video ? "video" : "audio"
                    tam: 15
                    color: fila.esCarpeta ? Theme.textoSecundario
                           : fila.esSecuencia ? Theme.acento
                           : fila.modelData.video ? Theme.clipVideoClaro : Theme.clipAudioClaro
                }
                Text {
                    Layout.fillWidth: true
                    text: fila.modelData.name
                    color: Theme.texto
                    font.pixelSize: Theme.tamTexto
                    font.bold: fila.esCarpeta
                    elide: Text.ElideMiddle
                }
                Text {
                    visible: !fila.esCarpeta
                    text: fila.modelData.duration || ""
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                    font.family: "Consolas, monospace"
                }
                Text {
                    Layout.preferredWidth: 118
                    text: fila.esCarpeta
                          ? (fila.modelData.count === 1 ? "1 elemento"
                                                        : fila.modelData.count + " elementos")
                          : fila.modelData.details
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTextoPequeno
                    elide: Text.ElideRight
                }
                BotonIcono {
                    visible: fila.esMedio
                    icono: "mas"
                    ayuda: "Añadir al final de la línea de tiempo"
                    implicitWidth: 28
                    implicitHeight: 26
                    enabled: biblioteca.editor.hasSequence
                    onPulsado: biblioteca.editor.addMedia(fila.modelData.id, -1)
                }
            }
        }
    }

    // --- Cuadrícula con miniaturas ---
    GridView {
        id: rejilla
        visible: biblioteca.cuadricula
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        cellWidth: Math.floor(width / 3)
        cellHeight: cellWidth * 9 / 16 + 30
        model: biblioteca.elementos
        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: celda
            required property var modelData
            readonly property bool esCarpeta: modelData.tipo === "carpeta"
            readonly property bool esSecuencia: modelData.tipo === "secuencia"
            readonly property bool esMedio: modelData.tipo === "medio"
            width: rejilla.cellWidth
            height: rejilla.cellHeight

            Rectangle {
                id: marco
                x: 3
                width: parent.width - 6
                height: width * 9 / 16
                radius: Theme.radio
                color: soltarCelda.containsDrag ? Theme.panelElevado : Theme.fondo
                border.color: soltarCelda.containsDrag || zonaCelda.containsMouse ? Theme.acento
                              : celda.esSecuencia && celda.modelData.open ? Theme.acentoSecundario
                              : Theme.borde
                clip: true

                Icono {
                    anchors.centerIn: parent
                    visible: miniatura.status !== Image.Ready
                    nombre: celda.esCarpeta ? "carpeta" : celda.esSecuencia ? "secuencia"
                            : celda.modelData.video ? "video" : "audio"
                    tam: 26
                    color: celda.esSecuencia ? Theme.acento : Theme.textoDesactivado
                }
                Image {
                    id: miniatura
                    anchors.fill: parent
                    anchors.margins: 1
                    source: celda.esMedio ? celda.modelData.thumbnail : ""
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 320
                }
                Rectangle {
                    visible: !celda.esCarpeta
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 5
                    width: duracion.implicitWidth + 10
                    height: duracion.implicitHeight + 2
                    radius: 3
                    color: Theme.visorFondo
                    Text {
                        id: duracion
                        anchors.centerIn: parent
                        text: celda.esSecuencia ? celda.modelData.canvas + " · " + celda.modelData.duration
                                                : celda.modelData.duration || ""
                        color: Theme.texto
                        font.pixelSize: 10
                        font.family: "Consolas, monospace"
                    }
                }
                Zona {
                    id: zonaCelda
                    anchors.fill: parent
                    elemento: celda.modelData
                }
                SoltarEnCarpeta {
                    id: soltarCelda
                    anchors.fill: parent
                    elemento: celda.modelData
                }
                BotonIcono {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    visible: celda.esMedio && biblioteca.editor.hasSequence
                             && (zonaCelda.containsMouse || hovered)
                    property bool hovered: false
                    icono: "mas"
                    ayuda: "Añadir al final de la línea de tiempo"
                    implicitWidth: 26
                    implicitHeight: 24
                    color: Theme.panelElevado
                    onPulsado: biblioteca.editor.addMedia(celda.modelData.id, -1)
                    HoverHandler { onHoveredChanged: parent.hovered = hovered }
                }
            }
            Text {
                anchors.top: marco.bottom
                anchors.topMargin: 5
                x: 4
                width: parent.width - 8
                text: celda.modelData.name
                color: celda.esMedio ? Theme.textoSecundario : Theme.texto
                font.pixelSize: Theme.tamTextoPequeno
                font.bold: !celda.esMedio
                elide: Text.ElideMiddle
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: biblioteca.elementos.length === 0
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: buscador.text.trim().length > 0 ? "Nada coincide con la búsqueda."
              : biblioteca.carpeta !== 0 ? "Carpeta vacía: arrastra aquí medios o secuencias."
              : "Pulsa «Importar…» o arrastra videos aquí."
        color: Theme.textoSecundario
        font.pixelSize: Theme.tamTexto
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: "Arrastra un medio a la línea de tiempo o a una carpeta · doble clic en una "
              + "secuencia para abrirla · clic derecho para más opciones"
        color: Theme.textoDesactivado
        font.pixelSize: Theme.tamTextoPequeno
    }

    // --- Menús y diálogos ---
    DialogoNombre { id: dialogoNombre }

    MenuTema {
        id: menuSecuencia
        property var elemento: null
        function abrir(e) {
            elemento = e
            popup()
        }
        Action {
            text: "Abrir"
            onTriggered: biblioteca.editor.openSequence(menuSecuencia.elemento.id)
        }
        Action {
            text: "Renombrar…"
            onTriggered: {
                const e = menuSecuencia.elemento
                dialogoNombre.pedir("Renombrar secuencia", e.name, "Renombrar",
                                    (nombre) => biblioteca.editor.renameSequence(e.id, nombre))
            }
        }
        Action {
            text: "Duplicar"
            onTriggered: biblioteca.editor.duplicateSequence(menuSecuencia.elemento.id, false)
        }
        Action {
            text: "Duplicar como vertical (9:16)"
            onTriggered: biblioteca.editor.duplicateSequence(menuSecuencia.elemento.id, true)
        }
        Action {
            text: "Sacar de la carpeta"
            enabled: menuSecuencia.elemento !== null && menuSecuencia.elemento.folder !== 0
            onTriggered: biblioteca.editor.moveToFolder([menuSecuencia.elemento.id], 0)
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Quitar la secuencia"
            enabled: biblioteca.editor.sequences.length > 1
            onTriggered: biblioteca.editor.removeSequence(menuSecuencia.elemento.id)
        }
    }

    MenuTema {
        id: menuCarpeta
        property var elemento: null
        function abrir(e) {
            elemento = e
            popup()
        }
        Action {
            text: "Abrir"
            onTriggered: biblioteca.entrar(menuCarpeta.elemento.id)
        }
        Action {
            text: "Renombrar…"
            onTriggered: {
                const e = menuCarpeta.elemento
                dialogoNombre.pedir("Renombrar carpeta", e.name, "Renombrar",
                                    (nombre) => biblioteca.editor.renameFolder(e.id, nombre))
            }
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Quitar la carpeta (su contenido pasa a la raíz)"
            onTriggered: biblioteca.editor.removeFolder(menuCarpeta.elemento.id)
        }
    }

    MenuTema {
        id: menuMedio
        property var elemento: null
        function abrir(e) {
            elemento = e
            popup()
        }
        Action {
            text: "Añadir al final de la línea de tiempo"
            enabled: biblioteca.editor.hasSequence
            onTriggered: biblioteca.editor.addMedia(menuMedio.elemento.id, -1)
        }
        Action {
            text: "Sacar de la carpeta"
            enabled: menuMedio.elemento !== null && menuMedio.elemento.folder !== 0
            onTriggered: biblioteca.editor.moveToFolder([menuMedio.elemento.id], 0)
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borde }
        }
        Action {
            text: "Quitar del proyecto (y de todas sus secuencias)"
            onTriggered: biblioteca.editor.removeMedia(menuMedio.elemento.id)
        }
    }

    // Ratón sobre un elemento: arrastrar (medio a la línea de tiempo o a una carpeta,
    // secuencia a una carpeta), doble clic para abrir y clic derecho para su menú.
    component Zona: MouseArea {
        id: zona
        required property var elemento
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        drag.target: elemento.tipo === "carpeta" ? null : biblioteca.fantasma
        drag.threshold: 6

        onPressed: (m) => {
            if (m.button === Qt.RightButton) {
                if (elemento.tipo === "carpeta")
                    menuCarpeta.abrir(elemento)
                else if (elemento.tipo === "secuencia")
                    menuSecuencia.abrir(elemento)
                else
                    menuMedio.abrir(elemento)
                return
            }
            if (elemento.tipo === "carpeta")
                return
            const f = biblioteca.fantasma
            const p = mapToItem(f.parent, m.x, m.y)
            f.x = p.x - 10
            f.y = p.y - 14
            f.mediaId = elemento.id
            f.transicion = -1
            f.nombre = elemento.name
            f.Drag.keys = [elemento.tipo === "secuencia" ? "sharkcut/secuencia" : "sharkcut/medio"]
        }
        onPositionChanged: {
            if (drag.active && (pressedButtons & Qt.LeftButton) && !biblioteca.fantasma.Drag.active)
                biblioteca.fantasma.Drag.active = true
        }
        onReleased: {
            const f = biblioteca.fantasma
            if (f.Drag.active) {
                f.Drag.drop()
                f.Drag.active = false
            }
        }
        onDoubleClicked: (m) => {
            if (m.button !== Qt.LeftButton)
                return
            if (elemento.tipo === "carpeta")
                biblioteca.entrar(elemento.id)
            else if (elemento.tipo === "secuencia")
                biblioteca.editor.openSequence(elemento.id)
            else
                biblioteca.abrirEnOrigen(elemento.path)
        }
    }

    // Una carpeta acepta medios y secuencias soltados encima.
    component SoltarEnCarpeta: DropArea {
        required property var elemento
        enabled: elemento.tipo === "carpeta"
        keys: ["sharkcut/medio", "sharkcut/secuencia"]
        onDropped: (d) => {
            biblioteca.editor.moveToFolder([d.source.mediaId], elemento.id)
            d.accept()
        }
    }

    // × pequeña de una pestaña.
    component BotonCerrar: Rectangle {
        id: cerrar
        property string ayuda
        signal pulsado()
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: 3
        color: zonaCerrar.containsMouse ? Theme.borde : "transparent"
        Icono {
            anchors.centerIn: parent
            nombre: "cerrar"
            tam: 11
            color: zonaCerrar.containsMouse ? Theme.texto : Theme.textoDesactivado
        }
        MouseArea {
            id: zonaCerrar
            anchors.fill: parent
            hoverEnabled: true
            onClicked: cerrar.pulsado()
        }
    }
}
