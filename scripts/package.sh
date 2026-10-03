#!/bin/sh
# Zip the plug-in into dist/Slippy-<version>.ccx (a .ccx is a zip with the
# manifest at its root). Double-click it, or run scripts/install.sh.
set -e
cd "$(dirname "$0")/../plugin"
version=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' manifest.json | head -1)
mkdir -p ../dist
out="../dist/Slippy-$version.ccx"
rm -f "$out"
zip -qr -X "$out" . -x "icons/frog.svg" -x ".*"
echo "$(cd ../dist && pwd)/Slippy-$version.ccx"
