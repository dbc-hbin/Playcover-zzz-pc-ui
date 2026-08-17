#!/bin/zsh
set -euo pipefail

repo=${0:A:h:h}
zip=${PLAYTOOLS_ZIP:-$repo/dist/PlayTools-SerialKeyboard-nullsafe.framework.zip}
installer=${PLAYTOOLS_INSTALLER:-$repo/tools/install_playtools_stage_safely.sh}
unity_source=${ZZZ_UNITY_FRAMEWORK:-$repo/_work/artifacts/UnityFramework.pre-citygate-20260813-214408.backup}
launch_constraint=${ZZZ_LAUNCH_CONSTRAINT:-$repo/tools/zzz-self-launch-constraint.plist}
backup_root=${PLAYTOOLS_BACKUP_ROOT:-$repo/_work/install-backups}
game_app=$HOME/Library/Containers/io.playcover.PlayCover/Applications/com.HoYoverse.Nap.app
game_exec=$game_app/ZenlessZoneZero
unity_framework=$game_app/Frameworks/UnityFramework.framework
live_unity=$unity_framework/UnityFramework
user_framework=$HOME/Library/Frameworks/PlayTools.framework
source_ak_bundle=$user_framework/PlugIns/AKInterface.bundle
source_ak_binary=$source_ak_bundle/Contents/MacOS/AKInterface
app_ak_bundle=$game_app/PlugIns/AKInterface.bundle
app_ak_binary=$app_ak_bundle/Contents/MacOS/AKInterface
playcover_settings=$HOME/Library/Containers/io.playcover.PlayCover/App\ Settings/com.HoYoverse.Nap.plist
game_preferences=$HOME/Library/Containers/com.HoYoverse.Nap/Data/Library/Preferences/com.HoYoverse.Nap.plist
expected_sha=${PLAYTOOLS_EXPECTED_SHA:-132701254ba9a4314e53476f702917f28f9dee2928fc427f8c03d16d4a41db96}
expected_zip_sha=${PLAYTOOLS_EXPECTED_ZIP_SHA:-7ee375ddc1abc21a996251edcf74485cd4595358a273b72b70f12ae44b083df7}
stage_name=${PLAYTOOLS_STAGE_NAME:-PlayTools-SerialKeyboard-nullsafe.framework}
expected_unity_sha=${ZZZ_EXPECTED_UNITY_SHA:-a2a91fa284bb126f3bfb7c72f311c1a34bd18afe67daaad658c7bca5358c8f2f}
expected_ak_sha=${PLAYTOOLS_EXPECTED_AK_SHA:-dab26672197b44e99b7fa9b03f2cad5b73c5fe69cf0ae9ca58657a0c7553765c}

[[ -f $zip ]] || { print -u2 "patched PlayTools archive not found: $zip"; exit 65; }
[[ -x $game_exec ]] || { print -u2 "PlayCover-installed ZZZ app not found: $game_app"; exit 66; }
[[ -f $unity_source ]] || { print -u2 "patched UnityFramework not found: $unity_source"; exit 77; }
[[ -f $launch_constraint ]] || { print -u2 "launch constraint not found: $launch_constraint"; exit 78; }

zip_sha=$(shasum -a 256 "$zip" | awk '{print $1}')
[[ $zip_sha == $expected_zip_sha ]] || {
  print -u2 "patched PlayTools archive hash mismatch: expected=$expected_zip_sha actual=$zip_sha"
  exit 72
}

host_exec=/Applications/Playcover\ ZZZ\ PC\ UI.app/Contents/MacOS/PlayCover
host_pid=$(pgrep -fx "$host_exec" | head -1 || true)
if [[ -n $host_pid ]]; then
  kill "$host_pid"
  for _ in {1..10}; do
    pgrep -fx "$host_exec" >/dev/null || break
    sleep 1
  done
  if pgrep -fx "$host_exec" >/dev/null; then
    print -u2 'PlayCover did not exit; close it and retry.'
    exit 20
  fi
fi
if pgrep -fx "$game_exec" >/dev/null; then
  print -u2 'ZenlessZoneZero is already running.'
  exit 21
fi

stage_root=$(mktemp -d /tmp/zzz-playtools-stage.XXXXXX)
trap 'find "$stage_root" -depth -delete 2>/dev/null || true' EXIT
ditto -x -k "$zip" "$stage_root"
stage=$stage_root/$stage_name

[[ -f $stage/PlayTools ]] || { print -u2 'patched framework missing from archive'; exit 67; }
stage_sha=$(shasum -a 256 "$stage/PlayTools" | awk '{print $1}')
[[ $stage_sha == $expected_sha ]] || {
  print -u2 "patched framework hash mismatch: expected=$expected_sha actual=$stage_sha"
  exit 68
}
codesign --verify --deep --strict "$stage"

installed_sha=''
if [[ -f $user_framework/PlayTools ]]; then
  installed_sha=$(shasum -a 256 "$user_framework/PlayTools" | awk '{print $1}')
fi
if [[ $installed_sha != $expected_sha ]]; then
  zsh "$installer" "$stage"
else
  chflags -R nouchg "$user_framework"
fi

installed_sha=$(shasum -a 256 "$user_framework/PlayTools" | awk '{print $1}')
[[ $installed_sha == $expected_sha ]] || { print -u2 'installed PlayTools hash verification failed'; exit 69; }
[[ $(xcrun vtool -show-build "$user_framework/PlayTools" | awk '/platform/{print $2; exit}') == MACCATALYST ]] || {
  print -u2 'installed PlayTools is not Mac Catalyst'
  exit 70
}
codesign --verify --deep --strict "$user_framework"
source_ak_sha=$(shasum -a 256 "$source_ak_binary" | awk '{print $1}')
[[ $source_ak_sha == $expected_ak_sha ]] || {
  print -u2 "patched AKInterface hash mismatch: expected=$expected_ak_sha actual=$source_ak_sha"
  exit 81
}

unity_source_sha=$(shasum -a 256 "$unity_source" | awk '{print $1}')
[[ $unity_source_sha == $expected_unity_sha ]] || {
  print -u2 "patched UnityFramework hash mismatch: expected=$expected_unity_sha actual=$unity_source_sha"
  exit 79
}
live_unity_sha=$(shasum -a 256 "$live_unity" | awk '{print $1}')
app_ak_sha=''
if [[ -f $app_ak_binary ]]; then
  app_ak_sha=$(shasum -a 256 "$app_ak_binary" | awk '{print $1}')
fi
if [[ $live_unity_sha != $expected_unity_sha || $app_ak_sha != $expected_ak_sha ]]; then
  codesign -d --entitlements :- "$game_app" > "$stage_root/zzz-entitlements.plist" 2>/dev/null
  plutil -lint "$stage_root/zzz-entitlements.plist" >/dev/null
fi
if [[ $live_unity_sha != $expected_unity_sha ]]; then
  stamp=$(date +%Y%m%d-%H%M%S)
  unity_backup=$backup_root/unityframework-$stamp
  mkdir -p "$unity_backup"
  ditto "$live_unity" "$unity_backup/UnityFramework"
  ditto "$unity_source" "$live_unity"
  chmod 755 "$live_unity"
  codesign --force --sign - --timestamp=none "$unity_framework"
fi
if [[ $app_ak_sha != $expected_ak_sha ]]; then
  stamp=${stamp:-$(date +%Y%m%d-%H%M%S)}
  ak_backup=$backup_root/akinterface-$stamp
  mkdir -p "$ak_backup"
  [[ -d $app_ak_bundle ]] && ditto "$app_ak_bundle" "$ak_backup/AKInterface.bundle"
  ditto "$source_ak_bundle" "$app_ak_bundle"
  codesign --force --sign - --timestamp=none "$app_ak_bundle"
fi
if [[ -f $stage_root/zzz-entitlements.plist ]]; then
  codesign --force --sign - --timestamp=none \
    --entitlements "$stage_root/zzz-entitlements.plist" \
    --launch-constraint-self "$launch_constraint" "$game_app"
fi
live_unity_sha=$(shasum -a 256 "$live_unity" | awk '{print $1}')
[[ $live_unity_sha == $expected_unity_sha ]] || { print -u2 'installed UnityFramework hash verification failed'; exit 80; }
app_ak_sha=$(shasum -a 256 "$app_ak_binary" | awk '{print $1}')
[[ $app_ak_sha == $expected_ak_sha ]] || { print -u2 'installed AKInterface hash verification failed'; exit 82; }
codesign --verify --deep --strict "$game_app"

# The native bridge is fail-closed while PlayCover keymapping is enabled.
# PlayCover may restore that stock value whenever its UI is opened, so enforce
# the verified native-input state immediately before every direct game launch.
[[ -f $playcover_settings ]] || { print -u2 "PlayCover settings not found: $playcover_settings"; exit 75; }
[[ -f $game_preferences ]] || { print -u2 "game preferences not found: $game_preferences"; exit 76; }
plutil -replace keymapping -bool NO "$playcover_settings"
plutil -replace noKMOnInput -bool YES "$playcover_settings"
plutil -replace 'io\.playcover\.PlayTools\.experimentalUnityNativeMouse' -bool YES "$game_preferences"

otool -L "$game_exec" | grep -F "$user_framework/PlayTools" >/dev/null || {
  print -u2 'ZZZ executable does not load the verified user PlayTools framework'
  exit 73
}

open -n "$game_app"
pid=''
for _ in {1..20}; do
  pid=$(pgrep -fx "$game_exec" | head -1 || true)
  [[ -n $pid ]] && break
  sleep 1
done
[[ -n $pid ]] || { print -u2 'ZenlessZoneZero did not stay running'; exit 71; }
mapped=false
for _ in {1..60}; do
  # vmmap canonicalizes the versioned framework executable symlink to
  # PlayTools.framework/Versions/A/PlayTools, so match the framework root.
  if vmmap "$pid" 2>/dev/null | grep -F "$user_framework/" >/dev/null; then
    mapped=true
    break
  fi
  sleep 1
done
[[ $mapped == true ]] || {
  print -u2 'running ZZZ process did not map the verified PlayTools framework'
  exit 74
}
print "launched ZenlessZoneZero pid=$pid PlayTools=$installed_sha AKInterface=$app_ak_sha UnityFramework=$live_unity_sha"
