.pragma library

// Formatting helpers shared by the pages. QML's JavaScript is ES5 (Qt 5.6): no let/const, arrow
// functions or template strings in this file.

var KM_PER_MILE = 1.609344
var FEET_PER_METRE = 3.28084

function pad2(n) {
    return n < 10 ? "0" + n : "" + n
}

function known(v) {
    return v !== undefined && v !== null && !isNaN(v)
}

function number(value, decimals) {
    if (!known(value))
        return "–"
    return Number(value).toFixed(decimals === undefined ? 0 : decimals)
}

function speedValue(kmh, imperial) {
    return imperial ? kmh / KM_PER_MILE : kmh
}

function speed(kmh, imperial, decimals) {
    if (!known(kmh))
        return "–"
    return Number(speedValue(kmh, imperial)).toFixed(decimals === undefined ? 1 : decimals)
}

function speedUnit(imperial) {
    return imperial ? "mph" : "km/h"
}

function distance(km, imperial, decimals) {
    if (!known(km))
        return "–"
    return Number(imperial ? km / KM_PER_MILE : km).toFixed(decimals === undefined ? 1 : decimals)
}

function distanceUnit(imperial) {
    return imperial ? "mi" : "km"
}

function elevation(metres, imperial) {
    if (!known(metres))
        return "–"
    return Math.round(imperial ? metres * FEET_PER_METRE : metres) + (imperial ? " ft" : " m")
}

function energyPerDistance(whPerKm, imperial) {
    if (!known(whPerKm) || whPerKm <= 0)
        return "–"
    return (imperial ? whPerKm * KM_PER_MILE : whPerKm).toFixed(1)
}

function energyPerDistanceUnit(imperial) {
    return imperial ? "Wh/mi" : "Wh/km"
}

function temperature(celsius) {
    if (!known(celsius))
        return "–"
    return Math.round(celsius)
}

// 1:05:09 or 5:09
function duration(seconds) {
    if (!known(seconds))
        return "–"
    var total = Math.max(0, Math.round(seconds))
    var h = Math.floor(total / 3600)
    var m = Math.floor((total % 3600) / 60)
    var s = total % 60
    return h > 0 ? h + ":" + pad2(m) + ":" + pad2(s) : m + ":" + pad2(s)
}

// "Thu 16 Oct, 08:15"
function dateTime(ms) {
    return Qt.formatDateTime(new Date(ms), "ddd d.M.yyyy, hh:mm")
}

function monthLabel(section) {
    // section is "yyyy-MM"
    var p = section.split("-")
    return Qt.formatDate(new Date(parseInt(p[0], 10), parseInt(p[1], 10) - 1, 1), "MMMM yyyy")
}

// Colour of a battery gauge by charge.
function socColor(soc, okColor, lowColor, criticalColor) {
    if (!known(soc))
        return okColor
    if (soc < 10)
        return criticalColor
    if (soc < 25)
        return lowColor
    return okColor
}
