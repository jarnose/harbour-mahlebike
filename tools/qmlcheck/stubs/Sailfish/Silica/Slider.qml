import QtQuick 2.0
Item { property string label; property real minimumValue: 0; property real maximumValue: 100; property real stepSize: 1
    property real value: 0; property string valueText; signal released(); width: parent ? parent.width : 0; height: 120
    Text { x: 24; color: "#b0b0b0"; text: parent.label + "  " + parent.valueText; font.pixelSize: 22 }
    Rectangle { x: 24; y: 70; width: (parent.width - 48) * (parent.value - parent.minimumValue) / (parent.maximumValue - parent.minimumValue); height: 8; color: "#3da9ff" } }
