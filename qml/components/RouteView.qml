import QtQuick 2.0
import Sailfish.Silica 1.0

// The route of a ride drawn as a line, without a map: positions are projected so that east-west and
// north-south keep their proportions at this latitude.
Item {
    id: view

    property var lats: []
    property var lons: []
    property color lineColor: Theme.highlightColor

    implicitHeight: Theme.itemSizeHuge * 2
    height: implicitHeight

    onLatsChanged: canvas.requestPaint()
    onLonsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onVisibleChanged: if (visible) requestPaint()
        onAvailableChanged: if (available) requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var n = Math.min(view.lats.length, view.lons.length)
            if (n < 2)
                return
            var minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9
            var i
            for (i = 0; i < n; ++i) {
                minLat = Math.min(minLat, view.lats[i]); maxLat = Math.max(maxLat, view.lats[i])
                minLon = Math.min(minLon, view.lons[i]); maxLon = Math.max(maxLon, view.lons[i])
            }
            var k = Math.cos((minLat + maxLat) / 2 * Math.PI / 180)
            var w = Math.max((maxLon - minLon) * k, 1e-7)
            var h = Math.max(maxLat - minLat, 1e-7)
            var pad = Theme.paddingLarge
            var scale = Math.min((width - 2 * pad) / w, (height - 2 * pad) / h)
            var ox = (width - w * scale) / 2
            var oy = (height - h * scale) / 2
            function px(j) { return ox + (view.lons[j] - minLon) * k * scale }
            function py(j) { return height - oy - (view.lats[j] - minLat) * scale }

            ctx.lineWidth = Theme.paddingSmall / 2
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            ctx.strokeStyle = view.lineColor
            ctx.beginPath()
            ctx.moveTo(px(0), py(0))
            for (i = 1; i < n; ++i)
                ctx.lineTo(px(i), py(i))
            ctx.stroke()

            ctx.fillStyle = Theme.primaryColor
            ctx.beginPath()
            ctx.arc(px(0), py(0), Theme.paddingSmall, 0, Math.PI * 2)
            ctx.fill()
            ctx.fillStyle = Theme.secondaryHighlightColor
            ctx.beginPath()
            ctx.arc(px(n - 1), py(n - 1), Theme.paddingSmall, 0, Math.PI * 2)
            ctx.fill()
        }
    }
}
