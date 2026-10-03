#!/bin/sh
# Package and install into Photoshop with Adobe's plug-in installer (restart
# Photoshop afterwards; the panel is under Plugins > Slippy).
set -e
ccx=$("$(dirname "$0")/package.sh")
upia="/Library/Application Support/Adobe/Adobe Desktop Common/RemoteComponents/UPI/UnifiedPluginInstallerAgent/UnifiedPluginInstallerAgent.app/Contents/MacOS/UnifiedPluginInstallerAgent"
[ -x "$upia" ] || { echo "Adobe's plug-in installer isn't here - double-click $ccx instead."; exit 1; }
# The installer won't replace a plug-in of the same version, so take the old
# copy out first (it's fine if there isn't one).
"$upia" --remove "Slippy" >/dev/null 2>&1 || true
"$upia" --install "$ccx"
