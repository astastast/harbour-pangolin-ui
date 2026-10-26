import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator { }

        Column {
            id: column
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Background service / Top Menu") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Start/Stop use the systemd user unit harbour-pangolin.service. It runs 'pangolin up --attach' as root through the setuid helper installed with this package, so no sudo or password is needed. The Top Menu toggle controls the same unit.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: pangolin.serviceAvailable
                      ? qsTr("Quick toggle: open Settings › Top Menu, find \"Pangolin\" under the toggles and enable it. If it is missing, restart the home screen (Settings › Developer tools › Restart home screen) or reboot.")
                      : qsTr("harbour-pangolin.service is not installed; the Top Menu toggle is unavailable.")
            }

            SectionHeader { text: qsTr("Connection (pangolin up)") }

            TextSwitch {
                text: qsTr("Override system DNS")
                description: qsTr("Needed for private resource names (aliases): all DNS queries go through Pangolin's DNS proxy, which forwards other names to the upstream DNS. Off passes --override-dns=false. Takes effect on the next Start.")
                checked: pangolin.overrideDns
                onCheckedChanged: pangolin.overrideDns = checked
            }

            TextField {
                width: parent.width
                label: qsTr("Extra 'up' flags (space separated, no quoting)")
                description: qsTr("The root helper rejects flags that take a path or open a listener (--tls-client-cert, --http-addr, --interface-name, --subnet-router).")
                placeholderText: "--org myorg --mtu 1280"
                text: pangolin.extraUpArgs
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: { pangolin.extraUpArgs = text; focus = false }
                onActiveFocusChanged: if (!activeFocus) pangolin.extraUpArgs = text
            }

            SectionHeader { text: qsTr("Info") }
            DetailItem { label: qsTr("CPU architecture"); value: pangolin.cpuArch }
            DetailItem { label: qsTr("Pangolin CLI"); value: pangolin.binaryFound ? pangolin.binaryPath : qsTr("missing") }
            DetailItem { label: qsTr("Root helper"); value: pangolin.helperOk ? qsTr("ok (setuid root)") : qsTr("missing or not setuid") }
        }
    }
}
