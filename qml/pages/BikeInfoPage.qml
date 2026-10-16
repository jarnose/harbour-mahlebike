import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/Format.js" as Format

// What the bike and its parts say about themselves. The numbers that the bike broadcasts all the time
// are shown at once; the serial numbers and firmware versions have to be asked for, with the same
// diagnosis session that the official app's "eBike information" screen uses. Reading only: nothing is
// written to the bike.
Page {
    id: page

    readonly property var info: AppController.bikeInfo
    readonly property var live: AppController.live
    readonly property bool busy: info.state !== undefined && info.state !== "idle"
    readonly property bool imperial: AppController.imperial

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: qsTr("Bike information")
                description: AppController.hasBike ? AppController.bikeName : ""
            }

            SectionHeader { text: qsTr("From the bike's own broadcast") }
            DetailItem {
                label: qsTr("VIN")
                value: AppController.vin.length > 0 ? AppController.vin : "–"
            }
            DetailItem {
                label: qsTr("Protocol")
                value: AppController.protocolVersion.length > 0 ? AppController.protocolVersion : "–"
            }
            DetailItem {
                visible: page.live.nominalWh !== undefined
                label: qsTr("Battery capacity")
                value: Format.number(page.live.nominalWh, 0) + " Wh"
            }
            DetailItem {
                visible: page.live.cycles !== undefined
                label: qsTr("Charge cycles")
                value: Format.number(page.live.cycles, 0)
            }
            DetailItem {
                visible: page.live.odometerKm !== undefined
                label: qsTr("Odometer")
                value: Format.distance(page.live.odometerKm, page.imperial, 1) + " " + Format.distanceUnit(page.imperial)
            }
            DetailItem {
                visible: page.live.assistLow !== undefined && page.live.assistHigh !== undefined && AppController.linkState >= 2
                label: qsTr("Assist levels")
                value: page.live.assistLow + " – " + page.live.assistHigh
            }

            SectionHeader { text: qsTr("Components") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Serial numbers and firmware versions are asked from the bike with a short diagnosis "
                           + "session. Nothing is changed. Stop the bike first.")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: page.busy ? qsTr("Working…") : qsTr("Read from the bike")
                enabled: AppController.linkState >= 2 && !page.busy
                onClicked: AppController.readBikeInfo()
            }
            Label {
                visible: page.info.message !== undefined && page.info.message !== ""
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: page.info.message ? page.info.message : ""
            }
            Label {
                visible: page.info.readAtMs !== undefined && page.info.readAtMs > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: page.info.readAtMs ? qsTr("Last read %1").arg(Format.dateTime(page.info.readAtMs)) : ""
            }

            Repeater {
                model: page.info.devices ? page.info.devices : []
                Column {
                    width: column.width
                    SectionHeader { text: modelData.name }
                    Repeater {
                        model: modelData.items
                        DetailItem {
                            label: modelData.name
                            value: modelData.error ? "–" : modelData.text
                        }
                    }
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
