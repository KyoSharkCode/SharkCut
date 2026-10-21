import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SharkCut

// Pestaña Proyectos: crear o abrir un proyecto, los proyectos abiertos y los recientes.
// Al abrir uno se pasa al Editor (`irAlEditor`).
Item {
    id: pagina

    required property Editor editor
    property string apiGrafica
    property bool apiEsperada: true
    signal nuevoProyecto()
    signal abrirProyecto()
    signal irAlEditor()

    // Al terminar de abrir un reciente, al Editor.
    property bool esperandoApertura: false
    Connections {
        target: pagina.editor
        function onOpened(ok) {
            if (pagina.esperandoApertura && ok)
                pagina.irAlEditor()
            pagina.esperandoApertura = false
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.margen

        // --- Izquierda: crear, abrir y lo que ya está abierto ---
        Rectangle {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            color: Theme.panel
            radius: Theme.radio
            border.color: Theme.borde

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 14

                RowLayout {
                    spacing: 12
                    Rectangle {
                        implicitWidth: 44
                        implicitHeight: 44
                        radius: 11
                        color: Theme.marca.abisal
                        border.color: Theme.marca.aleta
                        Icono {
                            anchors.centerIn: parent
                            nombre: "aleta"
                            tam: 26
                            color: Theme.cabezal
                        }
                    }
                    ColumnLayout {
                        spacing: 0
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
                    }
                }

                BotonIcono {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    implicitHeight: 40
                    icono: "nuevo"
                    texto: "Nuevo proyecto"
                    ayuda: "Ctrl+N"
                    acentuado: true
                    onPulsado: pagina.nuevoProyecto()
                }
                BotonIcono {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    icono: "abrir"
                    texto: "Abrir proyecto…"
                    ayuda: "Ctrl+O"
                    onPulsado: pagina.abrirProyecto()
                }

                Etiqueta { texto: "Abiertos"; Layout.topMargin: 10 }
                Repeater {
                    model: pagina.editor.projects
                    delegate: Rectangle {
                        id: abierto
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 40
                        radius: Theme.radio
                        color: zonaAbierto.containsMouse ? Theme.panelElevado : Theme.fondo
                        border.color: modelData.active ? Theme.acento : Theme.borde
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8
                            Icono {
                                nombre: "carpeta"
                                tam: 15
                                color: abierto.modelData.active ? Theme.acento : Theme.textoSecundario
                            }
                            Text {
                                Layout.fillWidth: true
                                text: abierto.modelData.name
                                color: Theme.texto
                                font.pixelSize: Theme.tamTexto
                                font.bold: abierto.modelData.active
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: abierto.modelData.modified
                                text: "Sin guardar"
                                color: Theme.acento
                                font.pixelSize: Theme.tamTextoPequeno
                            }
                        }
                        MouseArea {
                            id: zonaAbierto
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                pagina.editor.activateProject(abierto.modelData.uid)
                                pagina.irAlEditor()
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Diagnóstico: útil si algo va lento o no se ve.
                Etiqueta { texto: "Sistema" }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 4
                    Dato { texto: "API gráfica" }
                    Dato {
                        texto: pagina.apiGrafica
                        valor: true
                        color: pagina.apiEsperada ? Theme.ok : Theme.aviso
                    }
                    Dato { texto: "Qt" }
                    Dato { texto: AppInfo.qtVersion; valor: true }
                    Dato { texto: "Sistema" }
                    Dato { texto: AppInfo.operatingSystem; valor: true }
                    Dato { texto: "Compilación" }
                    Dato { texto: AppInfo.buildType; valor: true }
                }
                BotonIcono {
                    Layout.fillWidth: true
                    icono: "carpeta"
                    texto: "Abrir la carpeta de registros"
                    ayuda: "Lo que pasó en cada sesión (y si se cerró por un fallo), para enviarlo"
                    onPulsado: pagina.editor.openLogFolder()
                }
            }
        }

        // --- Derecha: recientes ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panel
            radius: Theme.radio
            border.color: Theme.borde

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 12

                // Copias de recuperación de una sesión que no se cerró bien
                Rectangle {
                    Layout.fillWidth: true
                    visible: pagina.editor.recoverableProjects.length > 0
                    implicitHeight: recuperacion.implicitHeight + 28
                    radius: Theme.radio
                    color: Theme.fondo
                    border.color: Theme.aviso
                    ColumnLayout {
                        id: recuperacion
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 8
                        Text {
                            text: "SharkCut no se cerró bien la última vez"
                            color: Theme.aviso
                            font.pixelSize: Theme.tamTexto
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Hay cambios sin guardar que se copiaron automáticamente. Al "
                                  + "recuperar un proyecto se abre con esos cambios; guárdalo para "
                                  + "conservarlos."
                            color: Theme.textoSecundario
                            font.pixelSize: Theme.tamTextoPequeno
                        }
                        Repeater {
                            model: pagina.editor.recoverableProjects
                            delegate: RowLayout {
                                id: copia
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: copia.modelData.name + "  ·  copia del "
                                              + copia.modelData.date
                                        color: Theme.texto
                                        font.pixelSize: Theme.tamTexto
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: copia.modelData.path.length > 0 ? copia.modelData.path
                                              : "Proyecto que aún no se había guardado"
                                        color: Theme.textoSecundario
                                        font.pixelSize: Theme.tamTextoPequeno
                                        elide: Text.ElideMiddle
                                    }
                                }
                                BotonIcono {
                                    texto: "Descartar"
                                    ayuda: "Borra la copia (el archivo del proyecto no se toca)"
                                    implicitHeight: 32
                                    onPulsado: pagina.editor.discardRecovery(copia.modelData.key)
                                }
                                BotonIcono {
                                    icono: "restablecer"
                                    texto: "Recuperar"
                                    acentuado: true
                                    implicitHeight: 32
                                    enabled: !pagina.editor.fileBusy
                                    onPulsado: {
                                        pagina.esperandoApertura = true
                                        pagina.editor.recoverProject(copia.modelData.key)
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    text: "Recientes"
                    color: Theme.texto
                    font.pixelSize: 17
                    font.bold: true
                }

                ListView {
                    id: recientes
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: pagina.editor.recentProjects
                    ScrollBar.vertical: BarraDesplazamiento {}

                    delegate: Rectangle {
                        id: reciente
                        required property var modelData
                        width: ListView.view.width - 12
                        height: 56
                        radius: Theme.radio
                        color: zonaReciente.containsMouse && modelData.exists ? Theme.panelElevado
                                                                              : Theme.fondo
                        border.color: zonaReciente.containsMouse && modelData.exists ? Theme.acento
                                                                                     : Theme.borde
                        opacity: modelData.exists ? 1 : 0.6

                        MouseArea {
                            id: zonaReciente
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: reciente.modelData.exists && !pagina.editor.fileBusy
                            onClicked: {
                                pagina.esperandoApertura = true
                                pagina.editor.openProjectPath(reciente.modelData.path)
                            }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 12
                            Icono {
                                nombre: "carpeta"
                                tam: 18
                                color: reciente.modelData.exists ? Theme.acento : Theme.textoDesactivado
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: reciente.modelData.name
                                    color: Theme.texto
                                    font.pixelSize: Theme.tamTexto
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: reciente.modelData.exists ? reciente.modelData.folder
                                          : "No se encuentra: " + reciente.modelData.folder
                                    color: reciente.modelData.exists ? Theme.textoSecundario
                                                                     : Theme.aviso
                                    font.pixelSize: Theme.tamTextoPequeno
                                    elide: Text.ElideMiddle
                                }
                            }
                            Text {
                                text: reciente.modelData.date
                                color: Theme.textoSecundario
                                font.pixelSize: Theme.tamTextoPequeno
                            }
                            BotonIcono {
                                icono: "cerrar"
                                ayuda: "Quitar de la lista (no borra el archivo)"
                                implicitWidth: 28
                                implicitHeight: 26
                                onPulsado: pagina.editor.forgetRecentProject(reciente.modelData.path)
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: recientes.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Aquí aparecerán los proyectos que abras o guardes."
                    color: Theme.textoSecundario
                    font.pixelSize: Theme.tamTexto
                }
                Item { visible: recientes.count === 0; Layout.fillHeight: true }
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

    component Dato: Text {
        property string texto
        property bool valor: false
        Layout.fillWidth: valor
        text: texto
        color: valor ? Theme.texto : Theme.textoSecundario
        font.pixelSize: Theme.tamTextoPequeno
        elide: Text.ElideRight
    }
}
