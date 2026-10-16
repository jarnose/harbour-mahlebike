import QtQuick 2.0
import Sailfish.Silica 1.0
import "Format.js" as Format

// A horizontal charge gauge with the percentage written in it.
Item {
    id: bar

    property int soc: -1
    property bool charging: false
    property string caption: ""

    height: Theme.itemSizeSmall * 0.8

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Theme.paddingSmall
        color: "transparent"
        border {
            width: 2
            color: Theme.rgba(Theme.primaryColor, 0.4)
        }

        Rectangle {
            x: 3
            y: 3
            height: parent.height - 6
            width: bar.soc < 0 ? 0 : Math.max(0, (parent.width - 6) * Math.min(100, bar.soc) / 100)
            radius: Theme.paddingSmall / 2
            color: Format.socColor(bar.soc, Theme.highlightColor, "#e8a317", Theme.errorColor)
            opacity: 0.85
            Behavior on width { NumberAnimation { duration: 300 } }
        }
    }

    Label {
        anchors.centerIn: parent
        text: (bar.soc < 0 ? "–" : bar.soc + " %") + (bar.charging ? "  ⚡" : "")
        font.pixelSize: Theme.fontSizeLarge
        color: Theme.primaryColor
    }
    Label {
        visible: bar.caption.length > 0
        anchors {
            right: parent.right
            rightMargin: Theme.paddingMedium
            verticalCenter: parent.verticalCenter
        }
        text: bar.caption
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
    }
}
