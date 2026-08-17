#!/usr/bin/env zsh
set -euo pipefail

# Build the opt-in gameplay-key owner variant. Only WASD, left Shift, Q, E, and
# Space are synchronously owned; menu/interaction keys remain on the normal path.
ROOT=${0:A:h:h}
SRC=$ROOT/src/PlayTools
OUT=$ROOT/dist
ARCHIVE=/tmp/playtools-keyboard-owner.xcarchive
BUILD_DIR=/tmp/playtools-keyboard-owner-build
ZIP=$OUT/PlayTools-GameplayOwner-nullsafe.framework.zip

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
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO DEVELOPMENT_TEAM= \
  SWIFT_ACTIVE_COMPILATION_CONDITIONS=PLAYTOOLS_KEYBOARD_OWNER \
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) PLAYTOOLS_KEYBOARD_OWNER=1'

FW=$ARCHIVE/Products/Library/Frameworks/PlayTools.framework
[[ -d $FW ]] || FW=$ARCHIVE/Products/usr/local/lib/PlayTools.framework
[[ -d $FW ]] || FW=$(find "$ARCHIVE" -maxdepth 5 -name PlayTools.framework | head -1)
[[ -n $FW && -d $FW ]] || { print -u2 "PlayTools.framework not found in $ARCHIVE"; exit 1; }

# Retarget the same null-safe/no-city-gate profile fingerprint as the trace
# build; this is a one-site, size-preserving diagnostic profile adjustment.
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

/usr/libexec/PlistBuddy -c 'Add :PlayToolsDiagnosticVariant string GameplayOwner' \
  "$FW/Resources/Info.plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c 'Set :PlayToolsDiagnosticVariant GameplayOwner' \
    "$FW/Resources/Info.plist"

AK_BUNDLE=$FW/PlugIns/AKInterface.bundle
[[ -d $AK_BUNDLE ]] && codesign --force --sign - --timestamp=none "$AK_BUNDLE"
codesign --force --sign - --timestamp=none "$FW"
codesign --verify --deep --strict "$FW"
[[ $(xcrun vtool -show-build "$FW/PlayTools" | awk '/platform/{print $2; exit}') == MACCATALYST ]] || {
  print -u2 'gameplay-owner PlayTools is not Mac Catalyst'; exit 1
}
for symbol in \
  PTUnityKeyboardOwnerTryInitialize \
  PTUnityKeyboardOwnerHandleHidUsage \
  PTUnityKeyboardOwnerReset; do
  nm -a "$FW/PlayTools" | grep -F "$symbol" >/dev/null || {
    print -u2 "gameplay-owner API is missing: $symbol"; exit 1
  }
done
python3 - "$FW/PlayTools" <<'PY'
from pathlib import Path
import sys

binary = Path(sys.argv[1]).read_bytes()
fixed_time_fingerprint = bytes.fromhex("e923be6dfd7b01a9fd43009185951194")
if binary.count(fixed_time_fingerprint) != 1:
    raise SystemExit("gameplay-owner fixed-time fingerprint is missing or ambiguous")
PY
grep -F 'invokeAfterUpdateCallbackRva' "$SRC/PlayTools/Controls/NativeMouse/PTUnityNativeMouseProfile.h" >/dev/null || {
  print -u2 'gameplay-owner InvokeAfterUpdate profile field is missing'; exit 1
}
grep -F 'inputSystemUpdateStateRva' "$SRC/PlayTools/Controls/NativeMouse/PTUnityNativeMouseProfile.h" >/dev/null || {
  print -u2 'gameplay-owner UpdateState profile field is missing'; exit 1
}
strings "$FW/PlayTools" | grep -F 'zzz-global-3.1.0-layout2-nullsafe-citygate' >/dev/null || {
  print -u2 'gameplay-owner profile identifier is missing'; exit 1
}

STAGE=$(mktemp -d /tmp/playtools-keyboard-owner-stage.XXXXXX)
ARCHIVE_DIR=$(mktemp -d /tmp/playtools-keyboard-owner-archive.XXXXXX)
trap 'find "$STAGE" "$ARCHIVE_DIR" -depth -delete 2>/dev/null || true' EXIT
ditto "$FW" "$STAGE/PlayTools-GameplayOwner-nullsafe.framework"
TMP_ZIP=$ARCHIVE_DIR/PlayTools-GameplayOwner-nullsafe.framework.zip
ditto -c -k --sequesterRsrc --keepParent \
  "$STAGE/PlayTools-GameplayOwner-nullsafe.framework" "$TMP_ZIP"
mv "$TMP_ZIP" "$ZIP"
VERIFY_DIR=$ARCHIVE_DIR/verify
mkdir -p "$VERIFY_DIR"
ditto -x -k "$ZIP" "$VERIFY_DIR"
PACKED_FW=$VERIFY_DIR/PlayTools-GameplayOwner-nullsafe.framework
[[ -d $PACKED_FW && -x $PACKED_FW/PlayTools ]] || {
  print -u2 'keyboard-owner archive framework layout is invalid'; exit 1
}
codesign --verify --deep --strict "$PACKED_FW"
[[ $(/usr/libexec/PlistBuddy -c 'Print :PlayToolsDiagnosticVariant' \
  "$PACKED_FW/Resources/Info.plist") == GameplayOwner ]] || {
  print -u2 'gameplay-owner archive variant marker is missing'; exit 1
}
echo "framework_sha256=$(shasum -a 256 "$FW/PlayTools" | awk '{print $1}')"
echo "archive_sha256=$(shasum -a 256 "$ZIP" | awk '{print $1}')"
echo "output=$ZIP"
