import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("About Mahle Bike")
                description: qsTr("Version %1").arg(AppController.version)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("Mahle Bike shows what an e-bike with a Mahle drive system reports over Bluetooth LE "
                           + "and records your rides. It is an independent project: it is not made, endorsed "
                           + "or supported by Mahle or any bike brand, and it comes without any warranty. "
                           + "Do not let it distract you in traffic.")
            }
            SectionHeader { text: qsTr("Which bikes") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("Bikes that the My SmartBike or ebikemotion app connects to: the X20 (HUS...), X25 and "
                           + "X35+ (iWoc...) systems, sold under brands such as Orbea, Scott, Bianchi, "
                           + "Pinarello and Schindelhauer. The bike's protocol version decides how its "
                           + "numbers are read; it is shown in Settings.")
            }
            SectionHeader { text: qsTr("Privacy") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("The app has no network permission and needs no account. Rides are kept in a "
                           + "database in the app's own folder on this phone. The GPS is used only while a ride "
                           + "is being recorded. Exports go to Documents/MahleBike.")
            }
            SectionHeader { text: qsTr("Credits") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("The frame layouts come from René Nulsch's Home Assistant integration "
                           + "ha-mysmartbike_ble (MIT licence), and were checked against the Bluetooth code "
                           + "of the official My SmartBike app, version 3.0.5.")
            }
            SectionHeader { text: qsTr("Data folder") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: AppController.exportDirectory
            }
        }
        VerticalScrollDecorator { }
    }
}
