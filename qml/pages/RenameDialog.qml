import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    property string name: ""

    canAccept: true
    onAccepted: name = field.text

    Column {
        width: parent.width
        DialogHeader { title: qsTr("Name the ride") }
        TextField {
            id: field
            width: parent.width
            text: dialog.name
            label: qsTr("Name")
            placeholderText: qsTr("Name")
            focus: true
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }
    }
}
