import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    property bool _finished
    property bool _success

    allowedOrientations: Orientation.All

    Connections {
        target: pangolin
        onActionFinished: {
            if (action === "login") {
                page._finished = true
                page._success = ok
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator { }

        Column {
            id: column
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Login") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Runs 'pangolin login <host>'. You get a one-time code to confirm in the browser. If your account has several organizations the CLI may ask interactively; in that case run 'pangolin login' once in the Terminal app.")
            }

            TextField {
                id: hostField
                width: parent.width
                label: qsTr("Pangolin server")
                placeholderText: "app.pangolin.net"
                text: "app.pangolin.net"
                enabled: !pangolin.loginActive
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: pangolin.loginActive ? qsTr("Cancel") : qsTr("Start login")
                enabled: pangolin.binaryFound
                onClicked: {
                    if (pangolin.loginActive) {
                        pangolin.cancelLogin()
                    } else {
                        page._finished = false
                        pangolin.login(hostField.text)
                    }
                }
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: pangolin.loginActive && pangolin.loginCode.length === 0
                size: BusyIndicatorSize.Medium
                visible: running
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                visible: pangolin.loginCode.length > 0 && pangolin.loginActive

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("One-time code")
                    color: Theme.secondaryHighlightColor
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: pangolin.loginCode
                    font.pixelSize: Theme.fontSizeHuge
                    color: Theme.highlightColor
                }
                ButtonLayout {
                    Button {
                        text: qsTr("Copy code")
                        onClicked: Clipboard.text = pangolin.loginCode
                    }
                    Button {
                        text: qsTr("Open browser")
                        enabled: pangolin.loginUrl.length > 0
                        onClicked: Qt.openUrlExternally(pangolin.loginUrl + "?code=" + encodeURIComponent(pangolin.loginCode))
                    }
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Waiting for confirmation (up to 5 min)…")
                }
            }

            Label {
                visible: page._finished
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: page._success ? Theme.highlightColor : Theme.errorColor
                text: page._success ? qsTr("Logged in.") : qsTr("Login failed or was cancelled. See the output on the main page.")
            }
        }
    }
}
