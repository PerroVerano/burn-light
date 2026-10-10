#!/bin/zsh
set -euo pipefail
GUI_ROOT="${0:A:h:h}"
[[ "${1:-}" == "--no-build" ]] || "$GUI_ROOT/scripts/build.sh"
[[ ! -e "/private/tmp/burnlight-physical-$(id -u)/active.json" ]] || { print -u2 "Physical task active/unknown; release refused."; exit 1; }
GUI_APP="$GUI_ROOT/output/BurnLight.app"
/usr/bin/codesign --verify --deep --strict "$GUI_APP"
GUI_VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$GUI_APP/Contents/Info.plist")
GUI_ZIP="$GUI_ROOT/output/BurnLight-$GUI_VERSION-local-arm64.zip"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$GUI_APP" "$GUI_ZIP"
/usr/bin/shasum -a 256 "$GUI_ZIP" > "$GUI_ZIP.sha256"
print "Local release: $GUI_ZIP"
