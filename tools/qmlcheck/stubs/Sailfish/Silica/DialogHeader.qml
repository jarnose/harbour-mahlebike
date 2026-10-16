import QtQuick 2.0
Item { property string title; width: parent ? parent.width : 0; height: 100; Text { x: 24; anchors.verticalCenter: parent.verticalCenter; color: "white"; text: parent.title; font.pixelSize: 36 } }
