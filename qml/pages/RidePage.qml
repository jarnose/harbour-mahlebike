import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"
import "../components/Format.js" as Format

Page {
    id: page

    property int rideId: 0
    property var d: ({})
    property string saved: ""
    readonly property bool imperial: AppController.imperial
    readonly property real tileWidth: (width - 2 * Theme.horizontalPageMargin) / (isPortrait ? 3 : 5)

    allowedOrientations: Orientation.All

    function reload() {
        d = AppController.rideDetail(rideId)
    }

    // The assist levels the ride spent time at, as a share of the whole.
    function assistShare(i) {
        var total = 0
        if (!d.assistSeconds)
            return 0
        for (var k = 0; k < d.assistSeconds.length; ++k)
            total += d.assistSeconds[k]
        return total > 0 ? d.assistSeconds[i] / total : 0
    }

    Component.onCompleted: reload()

    Timer {
        // A ride that is still being recorded grows.
        running: page.d.recording === true && page.status === PageStatus.Active
        interval: 10000
        repeat: true
        onTriggered: page.reload()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Delete")
                visible: page.d.recording !== true
                onClicked: remorse.execute(qsTr("Deleting"), function() {
                    AppController.deleteRide(page.rideId)
                    pageStack.pop()
                })
            }
            MenuItem {
                text: qsTr("Export as CSV")
                onClicked: page.saved = AppController.exportRide(page.rideId, "csv")
            }
            MenuItem {
                text: qsTr("Export as GPX")
                visible: page.d.hasTrack === true
                onClicked: page.saved = AppController.exportRide(page.rideId, "gpx")
            }
            MenuItem {
                text: qsTr("Rename")
                onClicked: {
                    var dlg = pageStack.push(Qt.resolvedUrl("RenameDialog.qml"), { name: page.d.name })
                    dlg.accepted.connect(function() {
                        AppController.renameRide(page.rideId, dlg.name)
                        page.reload()
                    })
                }
            }
        }

        RemorsePopup { id: remorse }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: page.d.name && page.d.name.length > 0 ? page.d.name : (page.d.startMs ? Format.dateTime(page.d.startMs) : "")
                description: page.d.name && page.d.name.length > 0 && page.d.startMs ? Format.dateTime(page.d.startMs) : ""
            }

            Label {
                visible: page.saved.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Saved to %1").arg(page.saved)
            }

            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                StatTile {
                    width: page.tileWidth
                    value: Format.distance(page.d.distanceKm, page.imperial, 2)
                    unit: Format.distanceUnit(page.imperial)
                    caption: qsTr("Distance")
                }
                StatTile {
                    width: page.tileWidth
                    value: Format.duration(page.d.durationS)
                    caption: qsTr("Time")
                }
                StatTile {
                    width: page.tileWidth
                    value: Format.duration(page.d.movingS)
                    caption: qsTr("Moving")
                }
                StatTile {
                    width: page.tileWidth
                    value: Format.speed(page.d.avgKmh, page.imperial, 1)
                    unit: Format.speedUnit(page.imperial)
                    caption: qsTr("Average")
                }
                StatTile {
                    width: page.tileWidth
                    value: Format.speed(page.d.maxKmh, page.imperial, 1)
                    unit: Format.speedUnit(page.imperial)
                    caption: qsTr("Top speed")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.hasTrack === true
                    value: Format.elevation(page.d.ascentM, page.imperial)
                    caption: qsTr("Climbed")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.hasEnergy === true
                    value: Format.number(page.d.energyWh, 0)
                    unit: "Wh"
                    caption: qsTr("Battery used")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.hasEnergy === true && page.d.whPerKm > 0
                    value: Format.energyPerDistance(page.d.whPerKm, page.imperial)
                    unit: Format.energyPerDistanceUnit(page.imperial)
                    caption: qsTr("Consumption")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.socStart >= 0
                    value: page.d.socStart + " → " + page.d.socEnd
                    unit: "%"
                    caption: qsTr("Charge")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.hasPower === true
                    value: Format.number(page.d.avgRiderW, 0)
                    unit: "W"
                    caption: qsTr("Your power (avg)")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.hasPower === true
                    value: Format.number(page.d.avgMotorW, 0)
                    unit: "W"
                    caption: qsTr("Motor (avg)")
                }
                StatTile {
                    width: page.tileWidth
                    visible: page.d.hasPower === true && page.d.maxRiderW > 0
                    value: Format.number(page.d.maxRiderW, 0)
                    unit: "W"
                    caption: qsTr("Your power (max)")
                }
            }

            SectionHeader {
                visible: page.d.hasTrack === true
                text: qsTr("Route")
            }
            RouteView {
                visible: page.d.hasTrack === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                lats: page.d.lats ? page.d.lats : []
                lons: page.d.lons ? page.d.lons : []
            }

            SectionHeader { text: qsTr("Speed (%1)").arg(Format.speedUnit(page.imperial)) }
            LineChart {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.d.xs ? page.d.xs : []
                ys: {
                    var out = []
                    if (!page.d.speed)
                        return out
                    for (var i = 0; i < page.d.speed.length; ++i)
                        out.push(Format.speedValue(page.d.speed[i], page.imperial))
                    return out
                }
                fixedMin: 0
                decimals: 0
            }

            SectionHeader {
                visible: page.d.hasPower === true
                text: qsTr("Your power (W)")
            }
            LineChart {
                visible: page.d.hasPower === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.d.xs ? page.d.xs : []
                ys: page.d.riderW ? page.d.riderW : []
                lineColor: Theme.secondaryHighlightColor
                fixedMin: 0
            }

            SectionHeader {
                visible: page.d.hasPower === true
                text: qsTr("Motor power (W)")
            }
            LineChart {
                visible: page.d.hasPower === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.d.xs ? page.d.xs : []
                ys: page.d.motorW ? page.d.motorW : []
                fixedMin: 0
            }

            SectionHeader { text: qsTr("Battery (%)") }
            LineChart {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.d.xs ? page.d.xs : []
                ys: page.d.soc ? page.d.soc : []
                fixedMin: 0
                fixedMax: 100
            }

            SectionHeader {
                visible: page.d.hasTrack === true
                text: qsTr("Altitude (%1)").arg(page.imperial ? "ft" : "m")
            }
            LineChart {
                visible: page.d.hasTrack === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.d.xs ? page.d.xs : []
                ys: {
                    var out = []
                    if (!page.d.alt)
                        return out
                    for (var i = 0; i < page.d.alt.length; ++i)
                        out.push(page.d.alt[i] === undefined || page.d.alt[i] === null ? null
                                 : (page.imperial ? page.d.alt[i] * 3.28084 : page.d.alt[i]))
                    return out
                }
            }

            SectionHeader { text: qsTr("Time at each assist level") }
            Repeater {
                model: 8
                Item {
                    x: Theme.horizontalPageMargin
                    width: column.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeExtraSmall
                    visible: page.assistShare(index) > 0.001
                    Label {
                        id: levelLabel
                        width: Theme.itemSizeSmall
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Level %1").arg(index)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        x: levelLabel.width + Theme.paddingMedium
                        height: Theme.paddingLarge
                        radius: height / 4
                        width: Math.max(2, (parent.width - levelLabel.width - Theme.paddingMedium - shareLabel.width - Theme.paddingMedium)
                                        * page.assistShare(index))
                        color: Theme.highlightColor
                        opacity: 0.8
                    }
                    Label {
                        id: shareLabel
                        anchors {
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                        }
                        text: Math.round(page.assistShare(index) * 100) + " %"
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
