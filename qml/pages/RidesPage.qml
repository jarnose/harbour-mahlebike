import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/Format.js" as Format

Page {
    id: page

    SilicaListView {
        id: list
        anchors.fill: parent
        model: AppController.rides

        header: PageHeader {
            title: qsTr("Rides")
            description: list.count > 0 ? qsTr("%n ride(s)", "", list.count) : ""
        }

        section {
            property: "section"
            delegate: SectionHeader {
                text: Format.monthLabel(section)
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeLarge
            onClicked: pageStack.push(Qt.resolvedUrl("RidePage.qml"), { rideId: model.rideId })

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Delete")
                    enabled: !model.recording
                    onClicked: item.remorseAction(qsTr("Deleting"), function() {
                        AppController.deleteRide(model.rideId)
                    })
                }
            }

            Label {
                id: title
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: distanceLabel.left
                    rightMargin: Theme.paddingMedium
                    top: parent.top
                    topMargin: Theme.paddingMedium
                }
                text: model.name.length > 0 ? model.name : Format.dateTime(model.startMs)
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                truncationMode: TruncationMode.Fade
            }
            Label {
                id: distanceLabel
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: title.verticalCenter
                }
                text: model.recording ? qsTr("recording")
                      : Format.distance(model.distanceKm, AppController.imperial, 1) + " "
                        + Format.distanceUnit(AppController.imperial)
                color: model.recording ? Theme.highlightColor : Theme.primaryColor
            }
            Label {
                anchors {
                    left: title.left
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    top: title.bottom
                }
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                truncationMode: TruncationMode.Fade
                text: (model.name.length > 0 ? Format.dateTime(model.startMs) + " · " : "")
                      + Format.duration(model.durationS) + " · "
                      + Format.speed(model.avgKmh, AppController.imperial, 1) + " "
                      + Format.speedUnit(AppController.imperial)
                      + (model.hasEnergy ? " · " + Math.round(model.energyWh) + " Wh" : "")
                      + (model.hasTrack ? " · GPS" : "")
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: qsTr("No rides yet")
            hintText: qsTr("Press Start ride on the main page, or turn on automatic recording in Settings")
        }

        VerticalScrollDecorator { }
    }
}
