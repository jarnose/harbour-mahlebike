import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"
import "components"

ApplicationWindow {
    id: appWindow
    initialPage: Component { MainPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    // The controller's short messages (a command was refused, a ride was saved) appear here
    // whichever page is showing.
    Connections {
        target: AppController
        onMessage: toast.show(text)
    }

    Toast {
        id: toast
    }

    // "Keep the screen on" is a setting; the module that does it is only loaded when it is on.
    Loader {
        source: AppController.keepScreenOn && Qt.application.active ? Qt.resolvedUrl("components/KeepAwake.qml") : ""
    }
}
