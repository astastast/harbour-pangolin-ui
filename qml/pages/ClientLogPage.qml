import QtQuick 2.0
import Sailfish.Silica 1.0

// Shows the output of harbour-pangolin.service ($XDG_RUNTIME_DIR/
// harbour-pangolin.log; `up --attach` logs to stdout), or the tail of
// ~/.config/pangolin/logs/client.log
// (written by a detached client, e.g. one started from Terminal, see
// internal/config/config.go defaultLogPath()).
Page {
    id: page

    property string _logText
    property bool _journal: !pangolin.isRoot

    function _reload() {
        _logText = _journal ? pangolin.readServiceLog(400) : pangolin.readClientLog(400)
    }

    allowedOrientations: Orientation.All

    Component.onCompleted: _reload()

    Timer {
        interval: 3000
        repeat: true
        running: autoRefresh.checked && page.status === PageStatus.Active
        onTriggered: page._reload()
    }

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: page._journal ? qsTr("Show client.log") : qsTr("Show service log")
                visible: pangolin.serviceAvailable
                onClicked: { page._journal = !page._journal; page._reload() }
            }
            MenuItem { text: qsTr("Reload"); onClicked: page._reload() }
            MenuItem { text: qsTr("Copy all"); onClicked: Clipboard.text = page._logText }
        }
        PushUpMenu {
            MenuItem { text: qsTr("Scroll to top"); onClicked: flick.scrollToTop() }
        }

        VerticalScrollDecorator { }

        ViewPlaceholder {
            enabled: page._logText.length === 0
            text: qsTr("Log is empty")
            hintText: page._journal ? qsTr("Start Pangolin to see the service output")
                                    : qsTr("Written only by a client started from Terminal")
        }

        Column {
            id: column
            width: page.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: page._journal ? qsTr("Service log") : qsTr("Client log")
                description: page._journal ? pangolin.serviceLogPath : pangolin.clientLogPath
            }

            TextSwitch {
                id: autoRefresh
                text: qsTr("Auto refresh")
                checked: true
            }

            Label {
                visible: page._logText.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                font {
                    family: "monospace"
                    pixelSize: Theme.fontSizeTiny
                }
                color: Theme.secondaryColor
                text: page._logText
            }
        }
    }
}
