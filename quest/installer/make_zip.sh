#!/bin/sh
# Builds the standalone installer zip: ./make_zip.sh [version]  ->  build/TFE-VR-DFmod-Installer-<version>.zip
set -e
cd "$(dirname "$0")"
V=${1:-$(sed -n "s/^VERSION = '\(.*\)'/\1/p" install_dfmod.py)}
N=TFE-VR-DFmod-Installer-$V
B=$(mktemp -d)
mkdir -p "$B/$N/dfmod" build
cp install_dfmod.py install_dfmod.bat install_dfmod.sh dfmod_2025_final.json README.md "$B/$N/"
cp -r own_parts "$B/$N/"
for f in assemble md3 glm register combine pack_comb convert; do cp ../tools/dfmod/$f.py "$B/$N/dfmod/"; done
cp ../../LICENSE "$B/$N/LICENSE"
(cd "$B" && zip -q -r -X out.zip "$N")
mv -f "$B/out.zip" "build/$N.zip"
ls -la "build/$N.zip"
