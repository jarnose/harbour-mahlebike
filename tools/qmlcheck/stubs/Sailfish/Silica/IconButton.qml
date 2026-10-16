import QtQuick 2.0
Rectangle { property IconSource icon: IconSource { }; signal clicked(); width: 80; height: 80; color: enabled ? "#335" : "#222"; radius: 40
    Text { anchors.centerIn: parent; text: icon.source.indexOf("add") >= 0 ? "+" : "−"; color: "white"; font.pixelSize: 40 } }
