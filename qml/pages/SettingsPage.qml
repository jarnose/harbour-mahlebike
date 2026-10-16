import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Display") }
            TextSwitch {
                text: qsTr("Miles instead of kilometres")
                checked: AppController.imperial
                automaticCheck: false
                onClicked: AppController.imperial = !AppController.imperial
            }
            TextSwitch {
                text: qsTr("Keep the screen on")
                description: qsTr("While the app is in front. A bike display is only useful if it stays lit.")
                checked: AppController.keepScreenOn
                automaticCheck: false
                onClicked: AppController.keepScreenOn = !AppController.keepScreenOn
            }

            SectionHeader { text: qsTr("Connection") }
            TextSwitch {
                text: qsTr("Connect automatically")
                description: qsTr("Connects to the chosen bike when the app starts and again whenever it "
                                  + "drops. Switching the bike on is enough.")
                checked: AppController.autoConnect
                automaticCheck: false
                onClicked: AppController.autoConnect = !AppController.autoConnect
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: qsTr("A bike that has been left switched on with nobody connected turns itself off "
                           + "after about five minutes. Disconnecting here starts that timer too.")
            }

            SectionHeader { text: qsTr("Rides") }
            TextSwitch {
                text: qsTr("Record rides automatically")
                description: qsTr("Starts when you set off, stops after five minutes of standing still.")
                checked: AppController.autoRecord
                automaticCheck: false
                onClicked: AppController.autoRecord = !AppController.autoRecord
            }
            TextSwitch {
                text: qsTr("Record the route with GPS")
                description: qsTr("Adds the phone's position to a ride, so that it can be exported as GPX. "
                                  + "It is only used while a ride is being recorded.")
                checked: AppController.useGps
                automaticCheck: false
                onClicked: AppController.useGps = !AppController.useGps
            }

            SectionHeader { text: qsTr("Bike controls") }
            TextSwitch {
                text: qsTr("Allow the app to control the bike")
                description: qsTr("Lets the app change the assist level, reset the trip and set the bike's "
                                  + "clock. Off by default: these commands come from the official app's "
                                  + "software but have not been tried on every bike.")
                checked: AppController.controlEnabled
                automaticCheck: false
                onClicked: AppController.controlEnabled = !AppController.controlEnabled
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Reset trip")
                enabled: AppController.controlEnabled && AppController.linkState >= 2
                onClicked: resetRemorse.execute(qsTr("Resetting the trip"), function() { AppController.resetTrip() })
            }
            Item { width: 1; height: Theme.paddingMedium }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Set the bike's clock")
                enabled: AppController.controlEnabled && AppController.linkState >= 2
                onClicked: AppController.syncClock()
            }

            SectionHeader { text: qsTr("Bike") }
            DetailItem {
                label: qsTr("Name")
                value: AppController.hasBike ? AppController.bikeName : "–"
            }
            DetailItem {
                label: qsTr("Address")
                value: AppController.hasBike ? AppController.bikeAddress : "–"
            }
            DetailItem {
                label: qsTr("Serial number")
                value: AppController.vin.length > 0 ? AppController.vin : "–"
            }
            DetailItem {
                label: qsTr("Protocol")
                value: AppController.protocolVersion.length > 0 ? AppController.protocolVersion : "–"
            }
            Item { width: 1; height: Theme.paddingLarge }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: AppController.hasBike
                text: qsTr("Forget this bike")
                onClicked: forgetRemorse.execute(qsTr("Forgetting the bike"), function() { AppController.forgetBike() })
            }
        }

        RemorsePopup { id: resetRemorse }
        RemorsePopup { id: forgetRemorse }

        VerticalScrollDecorator { }
    }
}
