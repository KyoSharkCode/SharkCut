import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Nuevo proyecto, nueva secuencia o los ajustes de la secuencia activa: nombre, lienzo
// (16:9, 9:16, 1:1 o a medida) y fotogramas por segundo, con ajustes guardados propios.
// El lienzo es de la secuencia: se edita en él y se exporta en él.
Popup {
    id: dialogo

    property int lienzo: 0 // 0 horizontal, 1 vertical, 2 cuadrado, 3 a medida
    property int ritmo: 6  // índice en `ritmos` (60 fps)
    property bool secuencia: false // true: nueva secuencia del proyecto activo
    property bool editar: false    // true: ajustes de la secuencia activa
    property var editor: null      // para los ajustes guardados
    property int anchoMedida: 1920
    property int altoMedida: 1080
    signal crear(string nombre, int ancho, int alto, int fps, int fpsDen)

    readonly property var lienzos: [
        { nombre: "Horizontal 16:9", tam: "1920×1080", uso: "YouTube, VODs", ancho: 1920, alto: 1080, que: "horizontal" },
        { nombre: "Vertical 9:16", tam: "1080×1920", uso: "Shorts, TikTok, Reels", ancho: 1080, alto: 1920, que: "vertical" },
        { nombre: "Cuadrado 1:1", tam: "1080×1080", uso: "Instagram", ancho: 1080, alto: 1080, que: "cuadrado" },
        { nombre: "A medida", tam: "", uso: "El tamaño que quieras", ancho: 0, alto: 0, que: "a medida" }
    ]
    // "Como el primer clip" (num 0) solo al crear.
    readonly property var ritmos: [
        { texto: "24", num: 24, den: 1 }, { texto: "25", num: 25, den: 1 },
        { texto: "29,97", num: 30000, den: 1001 }, { texto: "30", num: 30, den: 1 },
        { texto: "50", num: 50, den: 1 }, { texto: "59,94", num: 60000, den: 1001 },
        { texto: "60", num: 60, den: 1 }, { texto: "Como el primer clip", num: 0, den: 1 }
    ]
    readonly property int ancho: lienzo === 3 ? anchoMedida : lienzos[lienzo].ancho
    readonly property int alto: lienzo === 3 ? altoMedida : lienzos[lienzo].alto
    readonly property string tamTexto: ancho + "×" + alto

    // Lienzo y fps de unos valores (los de la secuencia o de un ajuste guardado).
    function poner(ancho, alto, num, den) {
        let l = 3
        for (let i = 0; i < 3; ++i) {
            if (lienzos[i].ancho === ancho && lienzos[i].alto === alto)
                l = i
        }
        lienzo = l
        anchoMedida = ancho
        altoMedida = alto
        for (let i = 0; i < ritmos.length; ++i) {
            if (ritmos[i].num * den === num * ritmos[i].den && ritmos[i].num > 0)
                ritmo = i
        }
    }

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
        nombreAjuste.text = ""
        if (editar && editor) {
            const s = editor.sequenceSettings()
            nombre.text = s.name
            poner(s.width, s.height, s.fpsNum, s.fpsDen)
        } else {
            nombre.text = ""
            if (ritmos[ritmo].num === 0 && editar)
                ritmo = 6
        }
        nombre.forceActiveFocus()
    }

    function aceptar() {
        const r = ritmos[ritmo]
        crear(nombre.text.trim(), ancho, alto, r.num, r.den)
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
                text: dialogo.editar ? "Ajustes de la secuencia"
                      : dialogo.secuencia ? "Nueva secuencia" : "Nuevo proyecto"
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
                    placeholderText: dialogo.secuencia || dialogo.editar ? "Secuencia nueva" : "Sin título"
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
                        implicitHeight: 112
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
                                    readonly property real w: opcion.index === 3 ? dialogo.anchoMedida
                                                                                 : opcion.modelData.ancho
                                    readonly property real h: opcion.index === 3 ? dialogo.altoMedida
                                                                                 : opcion.modelData.alto
                                    anchors.centerIn: parent
                                    width: w >= h ? 44 : 44 * w / Math.max(1, h) * 0.9
                                    height: w >= h ? 44 * h / Math.max(1, w) : 40
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
                                text: opcion.modelData.tam.length > 0 ? opcion.modelData.tam
                                                                      : dialogo.tamTexto
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTextoPequeno
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: opcion.modelData.uso
                                color: Theme.textoDesactivado
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

            // Tamaño a medida.
            RowLayout {
                visible: dialogo.lienzo === 3
                Layout.fillWidth: true
                Layout.topMargin: -8
                spacing: 12
                CampoNumero {
                    Layout.fillWidth: true
                    etiqueta: "Ancho"
                    anchoEtiqueta: 50
                    unidad: "px"
                    paso: 2
                    desde: 16
                    hasta: 8192
                    valor: dialogo.anchoMedida
                    onCambiado: (v) => dialogo.anchoMedida = Math.round(v / 2) * 2
                }
                CampoNumero {
                    Layout.fillWidth: true
                    etiqueta: "Alto"
                    anchoEtiqueta: 50
                    unidad: "px"
                    paso: 2
                    desde: 16
                    hasta: 8192
                    valor: dialogo.altoMedida
                    onCambiado: (v) => dialogo.altoMedida = Math.round(v / 2) * 2
                }
            }

            Etiqueta { texto: "Fotogramas por segundo" }
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: -10
                spacing: 6
                Repeater {
                    model: dialogo.ritmos
                    delegate: BotonIcono {
                        required property var modelData
                        required property int index
                        visible: modelData.num > 0 || !dialogo.editar
                        texto: modelData.texto
                        implicitHeight: 32
                        activo: dialogo.ritmo === index
                        onPulsado: dialogo.ritmo = index
                    }
                }
            }

            // Ajustes guardados: un clic los aplica; se guardan los de ahora con un nombre.
            Etiqueta { texto: "Ajustes guardados"; visible: dialogo.editor !== null }
            Flow {
                visible: dialogo.editor !== null
                Layout.fillWidth: true
                Layout.topMargin: -10
                spacing: 6
                Repeater {
                    model: dialogo.editor ? dialogo.editor.sequencePresets : []
                    delegate: Rectangle {
                        id: ajuste
                        required property var modelData
                        implicitWidth: filaAjuste.implicitWidth + 16
                        implicitHeight: 30
                        radius: Theme.radio
                        color: Theme.fondo
                        border.color: zonaAjuste.containsMouse ? Theme.acento : Theme.borde
                        Row {
                            id: filaAjuste
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                text: ajuste.modelData.name + "  ·  " + ajuste.modelData.width + "×"
                                      + ajuste.modelData.height + " · "
                                      + (Math.round(ajuste.modelData.fpsNum / ajuste.modelData.fpsDen * 100) / 100)
                                        .toString().replace(".", ",") + " fps"
                                color: Theme.texto
                                font.pixelSize: Theme.tamTextoPequeno
                            }
                            Text {
                                text: "✕"
                                color: zonaQuitar.containsMouse ? Theme.aviso : Theme.textoDesactivado
                                font.pixelSize: Theme.tamTextoPequeno
                                MouseArea {
                                    id: zonaQuitar
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    onClicked: dialogo.editor.removeSequencePreset(ajuste.modelData.name)
                                }
                            }
                        }
                        MouseArea {
                            id: zonaAjuste
                            anchors.fill: parent
                            anchors.rightMargin: 22
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialogo.poner(ajuste.modelData.width, ajuste.modelData.height,
                                                     ajuste.modelData.fpsNum, ajuste.modelData.fpsDen)
                        }
                    }
                }
                Text {
                    visible: dialogo.editor !== null && dialogo.editor.sequencePresets.length === 0
                    text: "Aún no hay ninguno."
                    color: Theme.textoDesactivado
                    font.pixelSize: Theme.tamTextoPequeno
                }
            }
            RowLayout {
                visible: dialogo.editor !== null
                Layout.fillWidth: true
                Layout.topMargin: -8
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    radius: Theme.radio
                    color: Theme.fondo
                    border.color: nombreAjuste.activeFocus ? Theme.acento : Theme.borde
                    TextField {
                        id: nombreAjuste
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        placeholderText: "Nombre para guardar estos ajustes"
                        placeholderTextColor: Theme.textoDesactivado
                        color: Theme.texto
                        font.pixelSize: Theme.tamTextoPequeno
                        background: null
                        padding: 0
                        selectByMouse: true
                    }
                }
                BotonIcono {
                    icono: "guardar"
                    texto: "Guardar ajuste"
                    implicitHeight: 30
                    enabled: nombreAjuste.text.trim().length > 0
                             && dialogo.ritmos[dialogo.ritmo].num > 0
                    ayuda: "Guarda el lienzo y los fps de ahora para usarlos en otras secuencias"
                    onPulsado: {
                        const r = dialogo.ritmos[dialogo.ritmo]
                        dialogo.editor.saveSequencePreset(nombreAjuste.text, dialogo.ancho,
                                                          dialogo.alto, r.num, r.den)
                        nombreAjuste.text = ""
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                lineHeight: 1.2
                text: dialogo.editar
                      ? "Al cambiar los fps, los clips, keyframes, transiciones, subtítulos y "
                        + "marcadores mantienen sus segundos. Al cambiar el lienzo, los clips "
                        + "siguen ajustados a él; las posiciones movidas a mano no cambian. "
                        + "Se puede deshacer (Ctrl+Z)."
                      : "Editarás en un lienzo " + dialogo.lienzos[dialogo.lienzo].que + " ("
                        + dialogo.tamTexto + "). Los videos de OBS (16:9) se "
                        + "colocan ajustados al lienzo; para reencuadrarlos, usa las propiedades "
                        + "del clip."
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
                icono: dialogo.editar ? "" : "nuevo"
                texto: dialogo.editar ? "Aplicar" : dialogo.secuencia ? "Crear secuencia" : "Crear proyecto"
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
