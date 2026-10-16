import QtQuick 2.0
import Sailfish.Silica 1.0

// A short message at the bottom of the window. Silica's own notices are not available to a
// Harbour app, so this does the same job.
Rectangle {
    id: toast

    property alias text: label.text

    function show(message) {
        label.text = message
        opacity = 1.0
        hideTimer.restart()
    }

    z: 1000
    anchors {
        horizontalCenter: parent.horizontalCenter
        bottom: parent.bottom
        bottomMargin: Theme.itemSizeLarge
    }
    width: Math.min(parent.width - 2 * Theme.horizontalPageMargin, label.implicitWidth + 2 * Theme.paddingLarge)
    height: label.implicitHeight + 2 * Theme.paddingMedium
    radius: Theme.paddingMedium
    color: Theme.rgba(Theme.highlightDimmerColor, 0.95)
    border {
        width: 1
        color: Theme.rgba(Theme.highlightColor, 0.5)
    }
    opacity: 0
    visible: opacity > 0.01

    Behavior on opacity { FadeAnimation { duration: 250 } }

    Label {
        id: label
        anchors {
            left: parent.left
            right: parent.right
            margins: Theme.paddingLarge
            verticalCenter: parent.verticalCenter
        }
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.primaryColor
    }

    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: toast.opacity = 0
    }
}
