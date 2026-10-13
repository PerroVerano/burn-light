#!/bin/zsh
set -euo pipefail
GUI_ROOT="${0:A:h:h}"
GUI_SDK="${BURNLIGHT_SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk}"
BURNLIGHT_ENABLE_WRITES=0 BURNLIGHT_USE_SWIFTPM=0 "$GUI_ROOT/scripts/build.sh"
GUI_BIN_DIR="${BURNLIGHT_BUILD_ROOT:-$GUI_ROOT}/.native-build"
/usr/bin/xcrun swiftc -sdk "$GUI_SDK" -target arm64-apple-macosx26.0 -g -Onone \
  -module-cache-path "$GUI_BIN_DIR/module-cache" -parse-as-library \
  -I "$GUI_BIN_DIR" -L "$GUI_BIN_DIR" -lBurnLightCore \
  "$GUI_ROOT/Sources/BurnLightApp/AppModel.swift" "$GUI_ROOT/Validation/ValidationMain.swift" \
  -o "$GUI_BIN_DIR/BurnLightValidation"
GUI_EVIDENCE="${BURNLIGHT_EVIDENCE_DIR:-$GUI_ROOT/evidence}"
mkdir -p "$GUI_EVIDENCE"
BURNLIGHT_TEST_APP="${BURNLIGHT_TEST_APP:-${BURNLIGHT_BUILD_ROOT:-$GUI_ROOT}/output/BurnLight.app}" "$GUI_BIN_DIR/BurnLightValidation" "$GUI_EVIDENCE/validation.json" "${GUI_ROOT:h}"
