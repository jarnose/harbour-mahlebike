#!/bin/sh
# Runs what .github/workflows/build.yml runs before it builds the RPMs, in the same order and with
# the same commands. Run it before every push: a translation file whose line numbers went stale
# (any edit to a file with a qsTr() or tr() above it) fails CI exactly like a failing test.
#
# Needs g++, python3 and Qt's lupdate/lrelease (qttools5-dev-tools).
set -e
cd "$(dirname "$0")/.."
export PATH=/usr/lib/x86_64-linux-gnu/qt5/bin:$PATH

echo "== Qt-free test suites"
tests/run.sh >/dev/null && echo ok

echo "== QML"
python3 tools/check_qml.py

echo "== translations"
lupdate -locations absolute -no-obsolete src qml -ts translations/harbour-mahlebike.ts >/dev/null
python3 tools/fill_fi.py
if ! git diff --exit-code -- translations >/dev/null; then
    echo "translations/ was out of date and has been regenerated: commit it." >&2
    git diff --stat -- translations >&2
    exit 1
fi
lrelease translations/harbour-mahlebike-fi.ts -qm /tmp/mahlebike-fi.qm | grep "0 unfinished"
echo "all clear"
