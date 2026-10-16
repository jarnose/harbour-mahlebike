import QtQuick 2.0
import Sailfish.Silica 1.0

// Choosing the bike. Nothing needs pairing for the numbers to flow, so a tap simply connects.
Page {
    id: page

    Component.onCompleted: AppController.startScan()
    Component.onDestruction: AppController.stopScan()

    SilicaListView {
        id: list
        anchors.fill: parent
        model: AppController.bikes

        PullDownMenu {
            busy: AppController.scanning
            MenuItem {
                visible: AppController.hasBike
                text: qsTr("Forget this bike")
                onClicked: {
                    AppController.forgetBike()
                    pageStack.pop()
                }
            }
            MenuItem {
                text: qsTr("Scan again")
                onClicked: AppController.startScan()
            }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium
            PageHeader {
                title: qsTr("Choose a bike")
                description: AppController.hasBike ? AppController.bikeName : ""
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Switch the bike on. It appears under a name that starts with iWoc or HUS. A bike "
                           + "talks to one phone at a time, so close the My SmartBike app and any other "
                           + "device that is connected to it.")
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeMedium
            onClicked: {
                AppController.selectBike(model.address, model.name)
                pageStack.pop()
            }
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Pair (only if it will not connect)")
                    onClicked: AppController.pairBike(model.address)
                }
            }

            Label {
                id: nameLabel
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    top: parent.top
                    topMargin: Theme.paddingSmall
                }
                text: model.name
                color: model.selected ? Theme.highlightColor : (item.highlighted ? Theme.highlightColor : Theme.primaryColor)
                truncationMode: TruncationMode.Fade
            }
            Label {
                anchors {
                    left: nameLabel.left
                    right: nameLabel.right
                    top: nameLabel.bottom
                }
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                truncationMode: TruncationMode.Fade
                text: model.address
                      + (model.rssi !== 0 ? " · " + model.rssi + " dBm" : "")
                      + (model.paired ? " · " + qsTr("paired") : "")
                      + (model.connected ? " · " + qsTr("connected") : "")
                      + (model.selected ? " · " + qsTr("selected") : "")
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: AppController.scanning ? qsTr("Looking for bikes…") : qsTr("No bike found")
            hintText: qsTr("Switch the bike on and keep the phone next to it")
        }

        VerticalScrollDecorator { }
    }
}
