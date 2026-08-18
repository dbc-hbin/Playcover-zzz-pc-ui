#!/usr/bin/env zsh
set -euo pipefail

# Opt-in experiment: use Option as the camera/cursor toggle and as a latched
# Unity modifier in UI mode. Entering UI latches Option before a short delay so
# Unity has time to observe the modifier. Mouse button, scroll, and motion
# handling remain on the stable AppKit/native path.
ROOT=${0:A:h:h}
SRC=$ROOT/src/PlayTools
OUT=${EXPERIMENTAL_OUT:-$ROOT/_work/experimental-artifacts}
ARCHIVE=/tmp/playtools-option-ui-latch-experimental.xcarchive
BUILD_DIR=/tmp/playtools-option-ui-latch-experimental-build
ZIP=$OUT/PlayTools-OptionUILatch-experimental.framework.zip

[[ -d $SRC/PlayTools.xcodeproj ]] || { print -u2 'PlayTools source not found'; exit 1; }
[[ -e $ARCHIVE ]] && find "$ARCHIVE" -depth -delete
[[ -e $BUILD_DIR ]] && find "$BUILD_DIR" -depth -delete
mkdir -p "$BUILD_DIR" "$OUT"

xcodebuild archive -quiet -project "$SRC/PlayTools.xcodeproj" -scheme PlayTools \
  -configuration Release -destination 'generic/platform=macOS,variant=Mac Catalyst' \
  -archivePath "$ARCHIVE" -derivedDataPath "$BUILD_DIR" SKIP_INSTALL=NO \
  BUILD_LIBRARY_FOR_DISTRIBUTION=NO CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO \
  DEVELOPMENT_TEAM= \
  SWIFT_ACTIVE_COMPILATION_CONDITIONS='$(inherited) PLAYTOOLS_OPTION_UI_LATCH_EXPERIMENT' \
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) PLAYTOOLS_OPTION_UI_LATCH_EXPERIMENT=1'

FW=$ARCHIVE/Products/Library/Frameworks/PlayTools.framework
[[ -d $FW ]] || FW=$(find "$ARCHIVE" -maxdepth 5 -name PlayTools.framework | head -1)
[[ -n $FW && -d $FW ]] || { print -u2 "PlayTools.framework not found in $ARCHIVE"; exit 1; }
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

/usr/libexec/PlistBuddy -c 'Add :PlayToolsDiagnosticVariant string OptionUILatchExperimental' \
  "$FW/Resources/Info.plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c 'Set :PlayToolsDiagnosticVariant OptionUILatchExperimental' \
  "$FW/Resources/Info.plist"
AK_BUNDLE=$FW/PlugIns/AKInterface.bundle
[[ -d $AK_BUNDLE ]] && codesign --force --sign - --timestamp=none "$AK_BUNDLE"
codesign --force --sign - --timestamp=none "$FW"
codesign --verify --deep --strict "$FW"
[[ $(xcrun vtool -show-build "$FW/PlayTools" | awk '/platform/{print $2; exit}') == MACCATALYST ]] || {
  print -u2 'command-serialization PlayTools is not Mac Catalyst'; exit 1
}
strings "$FW/PlayTools" | grep -F '[PlayTools][OptionUILatch] mode=option-toggle-ui-latch' >/dev/null
strings "$FW/PlayTools" | grep -F '[PlayTools][OptionUILatch] activation-delay-ms=80' >/dev/null
strings "$FW/PlayTools" | grep -F '[PlayTools][OptionUILatch] option-queue=enabled' >/dev/null
strings "$AK_BUNDLE/Contents/MacOS/AKInterface" | grep -F '[AKInterface][OptionUILatch] flags-changed-routing=enabled' >/dev/null
if strings "$FW/PlayTools" | grep -F '[PlayTools][MouseSerialization] mode=exclusive-button-scroll' >/dev/null; then
  print -u2 'mouse serialization marker unexpectedly linked'; exit 1
fi

STAGE=$(mktemp -d /tmp/playtools-option-ui-latch-stage.XXXXXX)
ARCHIVE_DIR=$(mktemp -d /tmp/playtools-option-ui-latch-archive.XXXXXX)
trap 'find "$STAGE" "$ARCHIVE_DIR" -depth -delete 2>/dev/null || true' EXIT
ditto "$FW" "$STAGE/PlayTools-OptionUILatch-experimental.framework"
TMP_ZIP=$ARCHIVE_DIR/PlayTools-OptionUILatch-experimental.framework.zip
ditto -c -k --sequesterRsrc --keepParent "$STAGE/PlayTools-OptionUILatch-experimental.framework" "$TMP_ZIP"
mv "$TMP_ZIP" "$ZIP"
VERIFY_DIR=$ARCHIVE_DIR/verify; mkdir -p "$VERIFY_DIR"; ditto -x -k "$ZIP" "$VERIFY_DIR"
PACKED_FW=$VERIFY_DIR/PlayTools-OptionUILatch-experimental.framework
codesign --verify --deep --strict "$PACKED_FW"
[[ $(/usr/libexec/PlistBuddy -c 'Print :PlayToolsDiagnosticVariant' "$PACKED_FW/Resources/Info.plist") == OptionUILatchExperimental ]]
strings "$PACKED_FW/PlayTools" | grep -F '[PlayTools][OptionUILatch] mode=option-toggle-ui-latch' >/dev/null
strings "$PACKED_FW/PlayTools" | grep -F '[PlayTools][OptionUILatch] activation-delay-ms=80' >/dev/null
strings "$PACKED_FW/PlayTools" | grep -F '[PlayTools][OptionUILatch] option-queue=enabled' >/dev/null
strings "$PACKED_FW/PlugIns/AKInterface.bundle/Contents/MacOS/AKInterface" | grep -F '[AKInterface][OptionUILatch] flags-changed-routing=enabled' >/dev/null
echo "framework_sha256=$(shasum -a 256 "$FW/PlayTools" | awk '{print $1}')"
echo "archive_sha256=$(shasum -a 256 "$ZIP" | awk '{print $1}')"
echo "output=$ZIP"
