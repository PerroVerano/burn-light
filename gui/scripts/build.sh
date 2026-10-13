#!/bin/zsh
set -euo pipefail
GUI_ROOT="${0:A:h:h}"
PROJECT_ROOT="${GUI_ROOT:h}"
GUI_SDK="${BURNLIGHT_SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk}"
[[ -d "$GUI_SDK" ]] || { print -u2 "SDK missing: $GUI_SDK"; exit 1; }
[[ ! -e "/private/tmp/burnlight-physical-$(id -u)/active.json" ]] || { print -u2 "A physical worker is active or unknown; do not rebuild the app."; exit 1; }
GUI_BUILD_ROOT="${BURNLIGHT_BUILD_ROOT:-$GUI_ROOT}"
GUI_OUTPUT="$GUI_BUILD_ROOT/output"
export BURNLIGHT_ADAPTER_BUILD="${BURNLIGHT_ADAPTER_BUILD:-$GUI_BUILD_ROOT/.adapter-build}"
mkdir -p "$GUI_OUTPUT"
cmake -S "$GUI_ROOT/Adapter" -B "$BURNLIGHT_ADAPTER_BUILD" \
  -DCMAKE_OSX_SYSROOT="$GUI_SDK" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_BUILD_TYPE=Release
cmake --build "$BURNLIGHT_ADAPTER_BUILD" -j 4
if [[ "${BURNLIGHT_USE_SWIFTPM:-0}" == 1 ]]; then
  swift build --package-path "$GUI_ROOT" --scratch-path "$GUI_ROOT/.build" -c debug \
    --sdk "$GUI_SDK" --triple arm64-apple-macosx26.0
  GUI_BIN_DIR="$(swift build --package-path "$GUI_ROOT" --scratch-path "$GUI_ROOT/.build" -c debug \
    --sdk "$GUI_SDK" --triple arm64-apple-macosx26.0 --show-bin-path)"
else
  # 此机器 SwiftPM 自身的动态链接失败，使用同一份源码直接编译/链接。
  GUI_BIN_DIR="$GUI_BUILD_ROOT/.native-build"
  mkdir -p "$GUI_BIN_DIR"
  /usr/bin/xcrun swiftc -sdk "$GUI_SDK" -target arm64-apple-macosx26.0 -g -O -module-cache-path "$GUI_BIN_DIR/module-cache" \
    -parse-as-library -emit-library -static -module-name BurnLightCore -emit-module \
    -emit-module-path "$GUI_BIN_DIR/BurnLightCore.swiftmodule" -o "$GUI_BIN_DIR/libBurnLightCore.a" \
    "$GUI_ROOT"/Sources/BurnLightCore/*.swift
  /usr/bin/xcrun swiftc -sdk "$GUI_SDK" -target arm64-apple-macosx26.0 -g -O -module-cache-path "$GUI_BIN_DIR/module-cache" \
    -parse-as-library -I "$GUI_BIN_DIR" -L "$GUI_BIN_DIR" -lBurnLightCore \
    "$GUI_ROOT"/Sources/BurnLightApp/*.swift -o "$GUI_BIN_DIR/BurnLight"
fi
GUI_APP="$GUI_OUTPUT/BurnLight.app"
mkdir -p "$GUI_APP/Contents/MacOS" "$GUI_APP/Contents/Resources"
cp "$GUI_BIN_DIR/BurnLight" "$GUI_APP/Contents/MacOS/BurnLight"
/usr/bin/xcrun swift -sdk "$GUI_SDK" -module-cache-path "$GUI_BIN_DIR/module-cache" \
  "$GUI_ROOT/scripts/icon.swift" "$GUI_BIN_DIR/BurnLight.iconset"
/usr/bin/python3 "$GUI_ROOT/scripts/icon-pack.py" "$GUI_BIN_DIR/BurnLight.iconset" "$GUI_APP/Contents/Resources/BurnLight.icns"
/usr/bin/python3 "$GUI_ROOT/scripts/package.py" "$PROJECT_ROOT" "$GUI_APP"
cat > "$GUI_APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>BurnLight</string>
<key>CFBundleIdentifier</key><string>local.burnlight.gui</string>
<key>CFBundleName</key><string>BurnLight</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.2.1</string>
<key>CFBundleVersion</key><string>5</string>
<key>LSMinimumSystemVersion</key><string>26.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>CFBundleIconFile</key><string>BurnLight</string>
</dict></plist>
PLIST
/usr/bin/codesign --force --sign - "$GUI_APP"
/usr/bin/codesign --verify --deep --strict "$GUI_APP"
print "Built: $GUI_APP"
print "SDK: $GUI_SDK"
