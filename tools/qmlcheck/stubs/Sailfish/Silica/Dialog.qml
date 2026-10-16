import QtQuick 2.0
Item { property bool canAccept: true; signal accepted(); function accept() { accepted() } }
