import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    // Same rule as MainPage.isRunning, so the cover action matches Start/Stop.
    readonly property bool isRunning: pangolin.state === "connected"
                                      || pangolin.state === "registered"
                                      || pangolin.state === "disconnected"
                                      || pangolin.state === "starting"

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Pangolin"
            font.pixelSize: Theme.fontSizeLarge
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: pangolin.state === "connected" ? Theme.highlightColor : Theme.secondaryColor
            text: pangolin.busy ? qsTr("Working…") : pangolin.statusSummary
        }
    }

    CoverActionList {
        enabled: !pangolin.busy && pangolin.binaryFound
        CoverAction {
            iconSource: isRunning ? "image://theme/icon-cover-pause" : "image://theme/icon-cover-play"
            onTriggered: isRunning ? pangolin.stop() : pangolin.start()
        }
    }
}
