import QtQuick 2.0
import Sailfish.Silica 1.0

// How strongly each assist level helps. The bike keeps these in its own memory, so a change stays
// until it is changed back: the page reads before it writes, keeps what the bike had, and checks
// afterwards.
Page {
    id: page

    readonly property var info: AppController.motorMaps
    readonly property bool busy: info.state !== undefined && info.state !== "idle"
    readonly property bool writable: info.writable === true
    property var custom: [40, 70, 90]

    // The presets of the official app (X35 / X20 bikes): the power per assist level 1, 2 and 3.
    readonly property var presets: [
        { id: "eco", name: qsTr("Eco"), levels: [30, 42, 65] },
        { id: "dynamic", name: qsTr("Dynamic"), levels: [40, 70, 90] },
        { id: "sport", name: qsTr("Sport"), levels: [60, 80, 100] }
    ]

    function current() {
        var out = []
        if (!info.maps)
            return out
        for (var i = 0; i < info.maps.length; ++i)
            out.push(info.maps[i].percent)
        return out
    }

    function sameLevels(a, b) {
        if (a.length !== b.length || a.length === 0)
            return false
        for (var i = 0; i < a.length; ++i)
            if (a[i] !== b[i])
                return false
        return true
    }

    // The preset the bike is set to, by its numbers.
    function currentPresetName() {
        var now = current()
        for (var i = 0; i < presets.length; ++i)
            if (sameLevels(now, presets[i].levels))
                return presets[i].name
        return now.length === 3 ? qsTr("Custom") : ""
    }

    function levelsText(levels) {
        if (!levels || levels.length === 0)
            return "–"
        return levels.join(" / ") + " %"
    }

    // The sliders start from what the bike has.
    onInfoChanged: {
        var now = current()
        if (now.length === 3 && !busy)
            custom = now
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Motor maps") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("How much of its power the motor gives at assist levels 1, 2 and 3. The setting is "
                           + "stored in the bike and stays until it is changed. The app reads the bike first, "
                           + "keeps what it had, writes the levels one by one and reads them back. Stop the "
                           + "bike before writing. Bike controls must be on in Settings.")
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

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: page.busy ? qsTr("Working…") : qsTr("Read the bike's maps")
                enabled: AppController.linkState >= 2 && !page.busy
                onClicked: AppController.readMotorMaps()
            }

            SectionHeader { text: qsTr("In the bike now") }
            DetailItem {
                label: qsTr("Levels 1 / 2 / 3")
                value: page.info.known ? page.levelsText(page.current()) : "–"
            }
            DetailItem {
                visible: page.info.known === true
                label: qsTr("Matches")
                value: page.currentPresetName()
            }
            Label {
                visible: page.info.known === true && page.info.percentLayout !== true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("This bike's maps are not simple percentages. They are shown in the log, and the "
                           + "app does not write them.")
            }

            SectionHeader { text: qsTr("Presets") }
            Repeater {
                model: page.presets
                BackgroundItem {
                    id: row
                    width: column.width
                    height: Theme.itemSizeMedium
                    enabled: page.writable
                    onClicked: remorse.execute(qsTr("Writing %1").arg(modelData.name), function() {
                        AppController.writeMotorMaps(modelData.levels)
                    })
                    Label {
                        anchors {
                            left: parent.left
                            leftMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.name
                        color: row.enabled ? (row.highlighted ? Theme.highlightColor : Theme.primaryColor)
                                           : Theme.secondaryColor
                    }
                    Label {
                        anchors {
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        text: page.levelsText(modelData.levels)
                        color: Theme.secondaryColor
                    }
                }
            }

            SectionHeader { text: qsTr("Your own values") }
            Repeater {
                model: 3
                Slider {
                    width: column.width
                    label: qsTr("Level %1").arg(index + 1)
                    minimumValue: 30
                    maximumValue: 100
                    stepSize: 1
                    value: page.custom[index]
                    valueText: Math.round(value) + " %"
                    onReleased: {
                        var c = page.custom.slice()
                        c[index] = Math.round(value)
                        page.custom = c
                    }
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Write these values")
                enabled: page.writable
                onClicked: remorse.execute(qsTr("Writing"), function() {
                    AppController.writeMotorMaps(page.custom)
                })
            }
            Label {
                visible: !page.writable && page.info.whyNot !== undefined && page.info.whyNot !== ""
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: page.info.whyNot ? page.info.whyNot : ""
            }

            SectionHeader {
                visible: page.info.previous !== undefined && page.info.previous.length === 3
                text: qsTr("Go back")
            }
            DetailItem {
                visible: page.info.previous !== undefined && page.info.previous.length === 3
                label: qsTr("Before the last write")
                value: page.levelsText(page.info.previous)
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.info.previous !== undefined && page.info.previous.length === 3
                text: qsTr("Restore these")
                enabled: page.writable
                onClicked: remorse.execute(qsTr("Restoring"), function() { AppController.restoreMotorMaps("previous") })
            }
            DetailItem {
                visible: page.info.original !== undefined && page.info.original.length === 3
                label: qsTr("First seen")
                value: page.levelsText(page.info.original)
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.info.original !== undefined && page.info.original.length === 3
                text: qsTr("Restore the first seen")
                enabled: page.writable
                onClicked: remorse.execute(qsTr("Restoring"), function() { AppController.restoreMotorMaps("original") })
            }
        }

        RemorsePopup { id: remorse }

        VerticalScrollDecorator { }
    }
}
