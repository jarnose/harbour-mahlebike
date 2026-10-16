import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"
import "../components/Format.js" as Format

Page {
    id: page

    readonly property var live: AppController.live
    readonly property bool imperial: AppController.imperial
    readonly property bool hasData: live.speedKmh !== undefined || live.soc !== undefined
    readonly property bool isLive: AppController.linkState === 3
    readonly property real tileWidth: (width - 2 * Theme.horizontalPageMargin) / (isPortrait ? 3 : 5)

    allowedOrientations: Orientation.All

    function elapsedText(info) {
        return info.elapsedS === undefined ? "" : Format.duration(info.elapsedS)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Log")
                onClicked: pageStack.push(Qt.resolvedUrl("LogPage.qml"))
            }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Rides")
                onClicked: pageStack.push(Qt.resolvedUrl("RidesPage.qml"))
            }
            MenuItem {
                visible: AppController.hasBike && AppController.linkState === 0
                text: qsTr("Connect")
                onClicked: AppController.connectNow()
            }
            MenuItem {
                visible: AppController.linkState !== 0
                text: qsTr("Disconnect")
                onClicked: AppController.disconnectNow()
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: AppController.hasBike ? AppController.bikeName : qsTr("Mahle Bike")
                description: AppController.status.split("\n")[0]
                MouseArea {
                    anchors.fill: parent
                    onClicked: pageStack.push(Qt.resolvedUrl("BikesPage.qml"))
                }
            }

            // No bike yet
            Column {
                visible: !AppController.hasBike
                width: parent.width
                spacing: Theme.paddingLarge
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.secondaryColor
                    text: qsTr("Switch the bike on and choose it. The bike talks to one phone at a time, so "
                               + "close the My SmartBike app first.")
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Choose a bike")
                    onClicked: pageStack.push(Qt.resolvedUrl("BikesPage.qml"))
                }
            }

            // Retry hint while the bike is not there
            Label {
                visible: AppController.hasBike && AppController.linkState <= 1
                         && AppController.status.indexOf("\n") >= 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: AppController.status.split("\n").slice(1).join("\n")
            }

            SpeedGauge {
                visible: AppController.hasBike
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(page.width * (page.isPortrait ? 0.72 : 0.34), Theme.itemSizeHuge * 3)
                height: width
                kmh: page.live.speedKmh !== undefined ? page.live.speedKmh : 0
                imperial: page.imperial
                active: page.isLive && page.live.speedKmh !== undefined
            }

            // Assist level
            Item {
                visible: AppController.hasBike
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: assistColumn.height
                Column {
                    id: assistColumn
                    width: parent.width
                    Label {
                        text: qsTr("Assist level")
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeExtraSmall
                    }
                    AssistBar {
                        width: parent.width
                        known: page.isLive && page.live.assist !== undefined
                        level: page.live.assist !== undefined ? page.live.assist : 0
                        low: page.live.assistLow !== undefined ? page.live.assistLow : 0
                        high: page.live.assistHigh !== undefined ? page.live.assistHigh : 5
                        controllable: AppController.controlEnabled && AppController.linkState >= 2
                        onDecrease: AppController.changeAssistLevel(-1)
                        onIncrease: AppController.changeAssistLevel(1)
                    }
                }
            }

            // Battery
            Item {
                visible: AppController.hasBike
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: batteryColumn.height
                Column {
                    id: batteryColumn
                    width: parent.width
                    spacing: Theme.paddingSmall
                    Label {
                        text: qsTr("Battery")
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeExtraSmall
                    }
                    BatteryBar {
                        width: parent.width
                        soc: page.live.soc !== undefined ? page.live.soc : -1
                        charging: page.live.charging === true
                        caption: page.live.remainingWh !== undefined
                                 ? Math.round(page.live.remainingWh) + " / " + Math.round(page.live.nominalWh) + " Wh"
                                 : ""
                    }
                    Label {
                        visible: page.live.battery2 !== undefined
                        text: qsTr("Second battery") + ": " + (page.live.battery2 ? page.live.battery2.soc + " %" : "")
                              + (page.live.battery2 ? " · " + Math.round(page.live.battery2.remainingWh) + " Wh" : "")
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            // Fault
            Rectangle {
                visible: page.live.errorCode !== undefined && page.live.errorCode > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: faultLabel.height + 2 * Theme.paddingMedium
                radius: Theme.paddingSmall
                color: Theme.rgba(Theme.errorColor, 0.25)
                border {
                    width: 1
                    color: Theme.errorColor
                }
                Label {
                    id: faultLabel
                    anchors {
                        left: parent.left
                        right: parent.right
                        margins: Theme.paddingMedium
                        verticalCenter: parent.verticalCenter
                    }
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    text: qsTr("The bike reports fault %1").arg(page.live.errorCode)
                          + (page.live.errorDevice ? " (" + page.live.errorDevice + ")" : "")
                          + ". " + qsTr("Ask your dealer; the number identifies the fault.")
                }
            }

            // The numbers
            Flow {
                visible: AppController.hasBike
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin

                StatTile {
                    width: page.tileWidth
                    visible: page.live.riderPowerW !== undefined
                    value: Format.number(page.live.riderPowerW, 0)
                    unit: "W"
                    caption: qsTr("You")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.motorPowerW !== undefined
                    value: Format.number(page.live.motorPowerW, 0)
                    unit: "W"
                    caption: qsTr("Motor")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.motorCurrentA !== undefined
                    value: Format.number(page.live.motorCurrentA, 1)
                    unit: "A"
                    caption: qsTr("Motor current")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.torquePct !== undefined
                    value: Format.number(page.live.torquePct, 0)
                    unit: "%"
                    caption: qsTr("Torque")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.rangeKm !== undefined
                    value: Format.distance(page.live.rangeKm, page.imperial, 0)
                    unit: Format.distanceUnit(page.imperial)
                    caption: qsTr("Range")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.tripKm !== undefined
                    value: Format.distance(page.live.tripKm, page.imperial, 1)
                    unit: Format.distanceUnit(page.imperial)
                    caption: qsTr("Trip")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.odometerKm !== undefined
                    value: Format.distance(page.live.odometerKm, page.imperial, 0)
                    unit: Format.distanceUnit(page.imperial)
                    caption: qsTr("Odometer")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.motorTemp !== undefined
                    value: Format.temperature(page.live.motorTemp)
                    unit: "°C"
                    caption: qsTr("Motor")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.batteryTemp !== undefined
                    value: Format.temperature(page.live.batteryTemp)
                    unit: "°C"
                    caption: qsTr("Battery")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.voltage !== undefined
                    value: Format.number(page.live.voltage, 1)
                    unit: "V"
                    caption: qsTr("Voltage")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.batteryCurrent !== undefined
                    value: Format.number(page.live.batteryCurrent, 1)
                    unit: "A"
                    caption: qsTr("Battery current")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.cycles !== undefined
                    value: Format.number(page.live.cycles, 0)
                    caption: qsTr("Charge cycles")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.light !== undefined
                    value: page.live.light ? qsTr("On") : qsTr("Off")
                    caption: qsTr("Light")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.live.remoteSoc !== undefined
                    value: Format.number(page.live.remoteSoc, 0)
                    unit: "%"
                    caption: qsTr("Remote")
                }
            }

            // Recording
            Item {
                visible: AppController.hasBike
                width: parent.width
                height: recordRow.height + Theme.paddingLarge

                Row {
                    id: recordRow
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingLarge

                    Button {
                        id: recordButton
                        anchors.verticalCenter: parent.verticalCenter
                        text: AppController.recording ? qsTr("Stop ride") : qsTr("Start ride")
                        enabled: AppController.recording || AppController.linkState >= 2
                        onClicked: AppController.recording ? AppController.stopRecording() : AppController.startRecording()
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - recordButton.width - parent.spacing
                        Label {
                            visible: AppController.recording
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            text: page.elapsedText(AppController.recordingInfo) + "  ·  "
                                  + Format.distance(AppController.recordingInfo.distanceKm, page.imperial, 2) + " "
                                  + Format.distanceUnit(page.imperial)
                        }
                        Label {
                            visible: AppController.recording && AppController.gpsStatus.length > 0
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                            text: AppController.gpsStatus
                        }
                        Label {
                            visible: !AppController.recording
                            width: parent.width
                            wrapMode: Text.Wrap
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                            text: AppController.autoRecord ? qsTr("Rides start by themselves when you set off")
                                                           : qsTr("Records speed, power and battery")
                        }
                    }
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
