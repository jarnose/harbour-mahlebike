import QtQuick 2.0
import Sailfish.Silica 1.0

// What the app and the bike said to each other. The protocol is reverse engineered and varies
// between bikes and firmware, so when something looks wrong this is the thing worth attaching to a
// report.
Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Clear the log")
                onClicked: AppController.clearLog()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Log") }

            SectionHeader { text: qsTr("Events") }
            TextArea {
                width: parent.width
                readOnly: true
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeTiny
                labelVisible: false
                text: AppController.logText
            }

            SectionHeader { text: qsTr("Last frames (<- from the bike, -> to the bike)") }
            TextArea {
                width: parent.width
                readOnly: true
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeTiny
                labelVisible: false
                text: AppController.frameLogText
            }
        }

        VerticalScrollDecorator { }
    }
}
