#!/usr/bin/env zsh
set -euo pipefail
# Build PlayTools YFix from src/PlayTools -> dist/PlayTools-camera-yfix.framework.zip
# Mirrors the Aug 15 02:23 artifact (70cea5, fsub@0x9f20 Y invert)
# Requires Xcode + xcodebuild
SRC=${0:A:h:h}/src/PlayTools
OUT=${0:A:h:h}/dist
ARCHIVE=/tmp/playtools-yfix.xcarchive
BUILD_DIR=/tmp/playtools-yfix-build

[[ -d $SRC/PlayTools.xcodeproj ]] || { echo "src/PlayTools not found"; exit 1 }

rm -rf "$ARCHIVE" "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Use xcodebuild to build PlayTools framework for MACCATALYST
xcodebuild archive \
  -project "$SRC/PlayTools.xcodeproj" \
  -scheme PlayTools \
  -configuration Release \
  -destination "generic/platform=macOS,variant=Mac Catalyst" \
  -archivePath "$ARCHIVE" \
  -derivedDataPath "$BUILD_DIR" \
  SKIP_INSTALL=NO \
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
  2>&1 | tail -20

# Framework is in archive Products
FW="$ARCHIVE/Products/Library/Frameworks/PlayTools.framework"
[[ -d $FW ]] || FW="$ARCHIVE/Products/usr/local/lib/PlayTools.framework"
if [[ ! -d $FW ]]; then
  # Fallback: find it
  FW=$(find "$ARCHIVE" -name "PlayTools.framework" -maxdepth 5 | head -1)
fi
[[ -n $FW && -d $FW ]] || { echo "PlayTools.framework not found in $ARCHIVE"; find "$ARCHIVE" -type d | head -20; exit 1 }

echo "Built: $FW"
ls -lh "$FW/PlayTools" 2>&1 | head -3
shasum -a 256 "$FW/PlayTools" 2>&1 | head -1
xcrun vtool -show-build "$FW/PlayTools" 2>&1 | grep platform | head -2
codesign --verify --deep --strict "$FW" 2>&1 | head -2 || echo "unsigned (build produces ad-hoc) - will re-sign on install"

# Export to dist
ZIP="$OUT/PlayTools-camera-yfix.framework.zip"
rm -f "$ZIP"
mkdir -p "$OUT"
ditto "$FW" "$OUT/PlayTools-camera-yfix.framework"
# Create zip with framework at root (no dist/ prefix)
TMPZIP=$(mktemp -d)
ditto "$FW" "$TMPZIP/PlayTools-camera-yfix.framework"
(cd "$TMPZIP" && zip -r "$ZIP" PlayTools-camera-yfix.framework 2>&1 | tail -5)
echo "dist: $ZIP $(du -h "$ZIP" | cut -f1)"
rm -rf "$TMPZIP" "$OUT/PlayTools-camera-yfix.framework"
# Also keep the canonical stripped zip
echo "Done. Verify:"
shasum -a 256 "$ZIP" | cut -c1-12
echo "Install: zsh tools/install_playtools_stage_safely.sh _work/artifacts/PlayTools-camera-yfix.framework"
echo "  or unzip dist/PlayTools-camera-yfix.framework.zip -d /tmp && zsh tools/install_playtools_stage_safely.sh /tmp/PlayTools-camera-yfix.framework"
