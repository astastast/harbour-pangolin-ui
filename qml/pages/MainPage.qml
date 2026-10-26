import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    readonly property bool _isRunning: pangolin.state === "connected"
                                       || pangolin.state === "registered"
                                       || pangolin.state === "disconnected"
                                       || pangolin.state === "starting"

    allowedOrientations: Orientation.All

    Component.onCompleted: {
        pangolin.checkVersion()
        pangolin.refreshStatus()
    }

    // Poll `pangolin status --json` while this page is visible.
    Binding {
        target: pangolin
        property: "polling"
        value: page.status === PageStatus.Active && Qt.application.state === Qt.ApplicationActive
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            busy: pangolin.busy
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Login")
                onClicked: pageStack.push(Qt.resolvedUrl("LoginPage.qml"))
            }
            MenuItem {
                text: qsTr("Client log")
                onClicked: pageStack.push(Qt.resolvedUrl("ClientLogPage.qml"))
            }
            // Status refreshes itself every 5 s, so no "Refresh" item: the
            // guidelines keep pulley menus below five items (landscape).
            MenuItem {
                text: qsTr("Account status")
                enabled: !pangolin.busy
                onClicked: pangolin.authStatus()
            }
        }

        PushUpMenu {
            MenuItem {
                text: qsTr("Clear output")
                onClicked: pangolin.clearOutput()
            }
            MenuItem {
                text: qsTr("Cancel running command")
                enabled: pangolin.busy
                onClicked: pangolin.cancelAction()
            }
        }

        VerticalScrollDecorator { }

        Column {
            id: column
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Pangolin"
                description: pangolin.cliVersion.length > 0
                             ? qsTr("CLI %1").arg(pangolin.cliVersion)
                             : (pangolin.binaryFound ? "" : qsTr("CLI not installed"))
            }

            // ---- broken install warning
            Label {
                visible: !pangolin.isRoot && (!pangolin.binaryFound || !pangolin.helperOk)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: !pangolin.binaryFound
                      ? qsTr("Pangolin CLI not found at %1. Reinstall the harbour-pangolin-ui package.").arg(pangolin.binaryPath)
                      : qsTr("The root helper is missing or not setuid root, so Start will fail. Reinstall the package as root (pkcon install-local).")
            }

            // ---- status
            SectionHeader { text: qsTr("Status") }

            Row {
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                Rectangle {
                    width: Theme.iconSizeExtraSmall
                    height: width
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    // Theme colors, so the dot follows the ambience.
                    color: pangolin.state === "connected" ? Theme.highlightColor
                         : pangolin.state === "error" ? Theme.errorColor
                         : page._isRunning ? Theme.secondaryHighlightColor
                         : Theme.secondaryColor
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: page.width - 2 * Theme.horizontalPageMargin - Theme.iconSizeExtraSmall - Theme.paddingMedium
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeLarge
                    color: Theme.highlightColor
                    text: pangolin.statusSummary.length > 0 ? pangolin.statusSummary : qsTr("Unknown")
                }
            }

            DetailItem {
                visible: pangolin.orgId.length > 0
                label: qsTr("Organization")
                value: pangolin.orgId
            }
            DetailItem {
                visible: pangolin.agentVersion.trim().length > 0
                label: qsTr("Agent")
                value: pangolin.agentVersion
            }

            Repeater {
                model: pangolin.peers
                delegate: DetailItem {
                    label: modelData.name.length > 0 ? modelData.name : qsTr("Site")
                    value: (modelData.connected ? qsTr("Connected") : qsTr("Disconnected"))
                           + " · " + modelData.mode
                           + (modelData.rttMs > 0 ? " · " + Math.round(modelData.rttMs) + " ms" : "")
                }
            }

            // ---- actions
            SectionHeader { text: qsTr("Actions") }

            ButtonLayout {
                Button {
                    text: qsTr("Start")
                    enabled: pangolin.binaryFound && !pangolin.busy && !page._isRunning
                    onClicked: pangolin.start()
                }
                Button {
                    text: qsTr("Stop")
                    enabled: pangolin.binaryFound && !pangolin.busy && page._isRunning
                    onClicked: pangolin.stop()
                }
            }

            Row {
                visible: pangolin.busy
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium
                BusyIndicator {
                    running: pangolin.busy
                    size: BusyIndicatorSize.Small
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Running: pangolin %1").arg(pangolin.currentAction)
                }
            }

            // ---- output
            SectionHeader { text: qsTr("Output") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                font {
                    family: "monospace"
                    pixelSize: Theme.fontSizeTiny
                }
                color: Theme.secondaryColor
                text: pangolin.output.length > 0 ? pangolin.output : qsTr("No output yet.")
            }
        }
    }
}
