import QtQuick 2.0
import Sailfish.Silica 1.0
import "Format.js" as Format

// The speed as a large number inside an open ring that fills with it.
Item {
    id: gauge

    property real kmh: 0
    property bool imperial: false
    property bool active: true
    // The ring is full at this speed (km/h). 25 is where the assistance of a European pedelec ends.
    property real fullAt: 45

    implicitWidth: Theme.itemSizeHuge * 2
    implicitHeight: implicitWidth

    onKmhChanged: canvas.requestPaint()
    onActiveChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onVisibleChanged: if (visible) requestPaint()
        onAvailableChanged: if (available) requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2
            var cy = height / 2
            var line = Math.max(8, width * 0.06)
            var r = Math.min(width, height) / 2 - line
            var start = Math.PI * 0.75
            var sweep = Math.PI * 1.5
            ctx.lineCap = "round"
            ctx.lineWidth = line
            ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.15)
            ctx.beginPath()
            ctx.arc(cx, cy, r, start, start + sweep, false)
            ctx.stroke()

            var fraction = Math.max(0, Math.min(1, gauge.kmh / gauge.fullAt))
            if (gauge.active && fraction > 0.003) {
                ctx.strokeStyle = gauge.kmh > 25.5 ? Theme.secondaryHighlightColor : Theme.highlightColor
                ctx.beginPath()
                ctx.arc(cx, cy, r, start, start + sweep * fraction, false)
                ctx.stroke()
            }
        }
    }

    Label {
        anchors {
            centerIn: parent
            verticalCenterOffset: -Theme.paddingMedium
        }
        text: gauge.active ? Format.speed(gauge.kmh, gauge.imperial, 1) : "–"
        font.pixelSize: gauge.width * 0.26
        color: gauge.active ? Theme.primaryColor : Theme.secondaryColor
    }
    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: gauge.height * 0.17
        }
        text: Format.speedUnit(gauge.imperial)
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeSmall
    }
}
