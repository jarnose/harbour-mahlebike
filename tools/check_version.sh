#!/bin/sh
# Holds a release tag to the version in the spec and to a CHANGELOG section.
#   tools/check_version.sh v0.1.0
set -e
cd "$(dirname "$0")/.."
tag=${1#v}
spec=$(sed -n 's/^Version:[[:space:]]*//p' rpm/harbour-mahlebike.spec)
if [ "$tag" != "$spec" ]; then
    echo "tag $1 does not match Version: $spec in rpm/harbour-mahlebike.spec" >&2
    exit 1
fi
grep -q "^## $spec\$" CHANGELOG.md || { echo "CHANGELOG.md has no section '## $spec'" >&2; exit 1; }
echo "version $spec ok"
