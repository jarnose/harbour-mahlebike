import QtQuick 2.0
import Sailfish.Silica 1.0

// The assist level as a row of segments, with - and + at the ends when the app may change it.
Item {
    id: bar

    property int level: 0
    property int low: 0
    property int high: 5
    property bool known: false
    property bool controllable: false
    signal decrease()
    signal increase()

    height: Theme.itemSizeMedium

    IconButton {
        id: minus
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-remove"
        enabled: bar.controllable && bar.known && bar.level > bar.low
        opacity: bar.controllable ? 1.0 : 0.25
        onClicked: bar.decrease()
    }
    IconButton {
        id: plus
        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-add"
        enabled: bar.controllable && bar.known && bar.level < bar.high
        opacity: bar.controllable ? 1.0 : 0.25
        onClicked: bar.increase()
    }

    Row {
        id: segments
        anchors {
            left: minus.right
            right: plus.left
            verticalCenter: parent.verticalCenter
        }
        spacing: Theme.paddingSmall

        Repeater {
            model: Math.max(1, bar.high - bar.low + 1)
            Rectangle {
                property int value: bar.low + index
                width: (segments.width - (Math.max(1, bar.high - bar.low + 1) - 1) * segments.spacing)
                       / Math.max(1, bar.high - bar.low + 1)
                height: Theme.itemSizeExtraSmall * 0.9
                radius: Theme.paddingSmall
                color: !bar.known ? Theme.rgba(Theme.primaryColor, 0.12)
                       : value === bar.level ? Theme.highlightColor
                       : value < bar.level ? Theme.rgba(Theme.highlightColor, 0.45)
                       : Theme.rgba(Theme.primaryColor, 0.15)

                Label {
                    anchors.centerIn: parent
                    text: parent.value
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.value === bar.level ? Theme.highlightDimmerColor : Theme.secondaryColor
                }
            }
        }
    }
}
