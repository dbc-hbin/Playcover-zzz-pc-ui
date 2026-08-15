#!/bin/zsh
set -euo pipefail

repo=${0:A:h:h}
source_dir=${PLAYCOVER_SOURCE_DIR:-$repo/_work/PlayCover-src-build}
build_dir=${PLAYCOVER_BUILD_DIR:-/tmp/playcover-zzz-build}
spm_dir=${PLAYCOVER_SPM_DIR:-/tmp/playcover-zzz-spm}
playtools_zip=${PLAYTOOLS_ZIP:-$repo/dist/PlayTools-camera-yfix-nullsafe-nocity.framework.zip}
patch_file=$repo/patches/playcover-3.1.0-combined.patch
output=$repo/dist/Playcover-ZZZ-PC-UI-3.1.0.app.zip

playcover_commit=a103786e03b7f6257b6b489dfc49ca6712423a2e
playtools_zip_sha=d8acecd8ea94c8f2a785b46623f779e0aae25e3a86cb7725e5b9c1924459ac2a
playtools_raw_sha=28a0a3e48935b2612bd0b5541d5e7c1a5751516a51a325fd0d0d48ae95713dbb

[[ -f $playtools_zip ]] || { print -u2 "missing patched PlayTools: $playtools_zip"; exit 65; }
[[ -f $patch_file ]] || { print -u2 "missing PlayCover patch: $patch_file"; exit 66; }
[[ $(shasum -a 256 "$playtools_zip" | awk '{print $1}') == $playtools_zip_sha ]] || {
  print -u2 'patched PlayTools archive hash mismatch'
  exit 67
}

if [[ ! -d $source_dir/.git ]]; then
  git clone --depth 1 --branch 3.1.0 https://github.com/PlayCover/PlayCover.git "$source_dir"
fi
[[ $(git -C "$source_dir" rev-parse HEAD) == $playcover_commit ]] || {
  print -u2 "unexpected PlayCover source commit: $(git -C "$source_dir" rev-parse HEAD)"
  exit 68
}

if git -C "$source_dir" apply --check "$patch_file" 2>/dev/null; then
  git -C "$source_dir" apply "$patch_file"
elif ! git -C "$source_dir" apply --reverse --check "$patch_file" 2>/dev/null; then
  print -u2 'PlayCover source has changes that do not match the authoritative patch'
  exit 69
fi

stage=$(mktemp -d /tmp/playcover-playtools.XXXXXX)
trap 'find "$stage" -depth -delete 2>/dev/null || true' EXIT
ditto -x -k "$playtools_zip" "$stage"
frameworks=($stage/*.framework(N))
(( ${#frameworks} == 1 )) || { print -u2 'expected one PlayTools framework'; exit 70; }
fw=$frameworks[1]
[[ $(shasum -a 256 "$fw/PlayTools" | awk '{print $1}') == $playtools_raw_sha ]] || {
  print -u2 'patched PlayTools binary hash mismatch'
  exit 71
}

# Xcode 27 validates embedded macOS frameworks as versioned bundles. Convert
# the verified Catalyst framework without rebuilding its feature-bearing code.
[[ -d $fw/_CodeSignature ]] && find "$fw/_CodeSignature" -depth -delete
mkdir -p "$fw/Versions/A/Resources"
mv "$fw/PlayTools" "$fw/Versions/A/PlayTools"
mv "$fw/Headers" "$fw/Versions/A/Headers"
mv "$fw/Modules" "$fw/Versions/A/Modules"
mv "$fw/PlugIns" "$fw/Versions/A/PlugIns"
mv "$fw/Info.plist" "$fw/Versions/A/Resources/Info.plist"
for locale in "$fw"/*.lproj(N); do
  mv "$locale" "$fw/Versions/A/Resources/${locale:t}"
done
ln -s A "$fw/Versions/Current"
ln -s Versions/Current/PlayTools "$fw/PlayTools"
ln -s Versions/Current/Headers "$fw/Headers"
ln -s Versions/Current/Modules "$fw/Modules"
ln -s Versions/Current/PlugIns "$fw/PlugIns"
ln -s Versions/Current/Resources "$fw/Resources"
codesign --force --sign - --timestamp=none "$fw/PlugIns/AKInterface.bundle"
codesign --force --sign - --timestamp=none "$fw"
codesign --verify --deep --strict "$fw"

python3 - "$fw/PlayTools" <<'PY'
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
replacement = bytes.fromhex("60000034e00313aafd000094681a40f9")
discarded = bytes.fromhex("1f2003d5e00313aafd000094681a40f9")
if data.count(replacement) != 1 or data.count(discarded) != 0:
    raise SystemExit("PlayTools profile fingerprint verification failed")
PY

carthage_fw=$source_dir/Carthage/Build/PlayTools.xcframework/ios-arm64/PlayTools.framework
mkdir -p "${carthage_fw:h}"
[[ -e $carthage_fw ]] && find "$carthage_fw" -depth -delete
ditto "$fw" "$carthage_fw"

FASTLANE=1 xcodebuild -resolvePackageDependencies \
  -project "$source_dir/PlayCover.xcodeproj" \
  -clonedSourcePackagesDirPath "$spm_dir"

FASTLANE=1 xcodebuild \
  -project "$source_dir/PlayCover.xcodeproj" \
  -scheme PlayCover \
  -configuration Release \
  -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath "$build_dir" \
  -clonedSourcePackagesDirPath "$spm_dir" \
  ARCHS=arm64 \
  ONLY_ACTIVE_ARCH=YES \
  CODE_SIGN_STYLE=Manual \
  CODE_SIGN_IDENTITY=- \
  DEVELOPMENT_TEAM= \
  CODE_SIGNING_ALLOWED=YES \
  CODE_SIGNING_REQUIRED=NO \
  build

app=$build_dir/Build/Products/Release/PlayCover.app
codesign --verify --deep --strict "$app"
package_root=$(mktemp -d /tmp/playcover-zzz-package.XXXXXX)
named_app=$package_root/Playcover\ ZZZ\ PC\ UI.app
ditto "$app" "$named_app"
codesign --verify --deep --strict "$named_app"
[[ -e $output ]] && unlink "$output"
ditto -c -k --sequesterRsrc --keepParent "$named_app" "$output"
unzip -tq "$output" >/dev/null

print "built=$app"
print "named=$named_app"
print "archive=$output"
print "PlayCover SHA256=$(shasum -a 256 "$named_app/Contents/MacOS/PlayCover" | awk '{print $1}')"
print "PlayTools SHA256=$(shasum -a 256 "$named_app/Contents/Frameworks/PlayTools.framework/PlayTools" | awk '{print $1}')"
find "$package_root" -depth -delete
