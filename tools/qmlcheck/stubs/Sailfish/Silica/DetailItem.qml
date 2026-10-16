import QtQuick 2.0
Item { property string label; property string value; width: parent ? parent.width : 0; height: 70
    Text { x: 24; anchors.verticalCenter: parent.verticalCenter; color: "#b0b0b0"; text: parent.label; font.pixelSize: 22 }
    Text { anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter } color: "white"; text: parent.value; font.pixelSize: 22 } }
