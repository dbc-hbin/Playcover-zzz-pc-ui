#!/usr/bin/env zsh
set -euo pipefail

# Stable PlayTools base plus one serialized producer for ordinary keys and
# F1-F12. Host/system modifiers, lock keys, and system keys stay passthrough.
# No release correction, KeyboardOwner, InputArbiter, or Unity OnUpdate hook.
ROOT=${0:A:h:h}
SRC=$ROOT/src/PlayTools
OUT=$ROOT/dist
ARCHIVE=/tmp/playtools-serial-keyboard.xcarchive
BUILD_DIR=/tmp/playtools-serial-keyboard-build
ZIP=$OUT/PlayTools-SerialKeyboard-nullsafe.framework.zip

[[ -d $SRC/PlayTools.xcodeproj ]] || { print -u2 'PlayTools source not found'; exit 1; }
[[ -e $ARCHIVE ]] && find "$ARCHIVE" -depth -delete
[[ -e $BUILD_DIR ]] && find "$BUILD_DIR" -depth -delete
mkdir -p "$BUILD_DIR" "$OUT"

xcodebuild archive -quiet \
  -project "$SRC/PlayTools.xcodeproj" -scheme PlayTools \
  -configuration Release \
  -destination 'generic/platform=macOS,variant=Mac Catalyst' \
  -archivePath "$ARCHIVE" -derivedDataPath "$BUILD_DIR" \
  SKIP_INSTALL=NO BUILD_LIBRARY_FOR_DISTRIBUTION=NO \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO DEVELOPMENT_TEAM=

FW=$ARCHIVE/Products/Library/Frameworks/PlayTools.framework
[[ -d $FW ]] || FW=$ARCHIVE/Products/usr/local/lib/PlayTools.framework
[[ -d $FW ]] || FW=$(find "$ARCHIVE" -maxdepth 5 -name PlayTools.framework | head -1)
[[ -n $FW && -d $FW ]] || { print -u2 "PlayTools.framework not found in $ARCHIVE"; exit 1; }

# Match the authoritative null-safe/no-city-gate UnityFramework fingerprint.
python3 - "$FW/PlayTools" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1]); data = p.read_bytes()
old = bytes.fromhex("1f2003d5e00313aafd000094681a40f9")
new = bytes.fromhex("60000034e00313aafd000094681a40f9")
if data.count(old) != 1:
    raise SystemExit(f"nullsafe profile fingerprint: expected one site, found {data.count(old)}")
p.write_bytes(data.replace(old, new, 1)); p.chmod(0o755)
PY

/usr/libexec/PlistBuddy -c 'Add :PlayToolsDiagnosticVariant string SerialKeyboard' \
  "$FW/Resources/Info.plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c 'Set :PlayToolsDiagnosticVariant SerialKeyboard' \
    "$FW/Resources/Info.plist"

AK_BUNDLE=$FW/PlugIns/AKInterface.bundle
[[ -d $AK_BUNDLE ]] && codesign --force --sign - --timestamp=none "$AK_BUNDLE"
codesign --force --sign - --timestamp=none "$FW"
codesign --verify --deep --strict "$FW"
[[ $(xcrun vtool -show-build "$FW/PlayTools" | awk '/platform/{print $2; exit}') == MACCATALYST ]] || {
  print -u2 'serial-keyboard PlayTools is not Mac Catalyst'; exit 1
}
nm -a "$FW/PlayTools" | grep -F 'PTUnityNativeMouseQueueKeyboardHidUsage' >/dev/null || {
  print -u2 'serial-keyboard queue API is missing'; exit 1
}
for forbidden in PTUnityInputArbiterSubmitHidUsage PTHookAfterUpdate; do
  nm -a "$FW/PlayTools" | grep -F "$forbidden" >/dev/null && {
    print -u2 "experimental input path was linked: $forbidden"; exit 1
  }
done

STAGE=$(mktemp -d /tmp/playtools-serial-keyboard-stage.XXXXXX)
ARCHIVE_DIR=$(mktemp -d /tmp/playtools-serial-keyboard-archive.XXXXXX)
trap 'find "$STAGE" "$ARCHIVE_DIR" -depth -delete 2>/dev/null || true' EXIT
ditto "$FW" "$STAGE/PlayTools-SerialKeyboard-nullsafe.framework"
TMP_ZIP=$ARCHIVE_DIR/PlayTools-SerialKeyboard-nullsafe.framework.zip
ditto -c -k --sequesterRsrc --keepParent \
  "$STAGE/PlayTools-SerialKeyboard-nullsafe.framework" "$TMP_ZIP"
mv "$TMP_ZIP" "$ZIP"
VERIFY_DIR=$ARCHIVE_DIR/verify
mkdir -p "$VERIFY_DIR"
ditto -x -k "$ZIP" "$VERIFY_DIR"
PACKED_FW=$VERIFY_DIR/PlayTools-SerialKeyboard-nullsafe.framework
codesign --verify --deep --strict "$PACKED_FW"
[[ $(/usr/libexec/PlistBuddy -c 'Print :PlayToolsDiagnosticVariant' \
  "$PACKED_FW/Resources/Info.plist") == SerialKeyboard ]] || {
  print -u2 'serial-keyboard archive marker is missing'; exit 1
}
echo "framework_sha256=$(shasum -a 256 "$FW/PlayTools" | awk '{print $1}')"
echo "archive_sha256=$(shasum -a 256 "$ZIP" | awk '{print $1}')"
echo "output=$ZIP"
