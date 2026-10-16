import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/Format.js" as Format

CoverBackground {
    id: cover

    readonly property var live: AppController.live
    readonly property bool isLive: AppController.linkState === 3

    Label {
        id: title
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            horizontalCenter: parent.horizontalCenter
        }
        text: AppController.hasBike ? AppController.bikeName : "Mahle Bike"
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeExtraSmall
        width: parent.width - 2 * Theme.paddingMedium
        horizontalAlignment: Text.AlignHCenter
        truncationMode: TruncationMode.Fade
    }

    Column {
        anchors {
            centerIn: parent
            verticalCenterOffset: Theme.paddingMedium
        }
        width: parent.width - 2 * Theme.paddingMedium
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: cover.isLive && cover.live.speedKmh !== undefined
            font.pixelSize: Theme.fontSizeHuge
            text: Format.speed(cover.live.speedKmh, AppController.imperial, 0)
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: cover.isLive && cover.live.speedKmh !== undefined
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: Format.speedUnit(AppController.imperial)
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: cover.isLive && cover.live.soc !== undefined
            font.pixelSize: Theme.fontSizeLarge
            color: Format.socColor(cover.live.soc, Theme.highlightColor, "#e8a317", Theme.errorColor)
            text: cover.live.soc + " %"
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: cover.isLive && cover.live.assist !== undefined
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: qsTr("Assist %1").arg(cover.live.assist)
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: AppController.recording
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeSmall
            text: "● " + Format.duration(AppController.recordingInfo.elapsedS)
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: !cover.isLive
            wrapMode: Text.Wrap
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: AppController.status.split("\n")[0]
        }
    }

    CoverActionList {
        enabled: AppController.hasBike
        CoverAction {
            iconSource: AppController.recording ? "image://theme/icon-cover-cancel" : "image://theme/icon-cover-play"
            onTriggered: AppController.recording ? AppController.stopRecording() : AppController.startRecording()
        }
    }
}
