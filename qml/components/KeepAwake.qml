import QtQuick 2.0
import Nemo.KeepAlive 1.2

// Held in its own file and loaded on demand, so that a phone without this module costs the app
// only the "keep the screen on" setting, not the whole window.
Item {
    DisplayBlanking {
        preventBlanking: true
    }
}
