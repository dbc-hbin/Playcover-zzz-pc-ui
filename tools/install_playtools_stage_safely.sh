#!/bin/zsh
set -euo pipefail

if (( $# != 1 )); then
  print -u2 "usage: $0 /absolute/path/to/PlayTools.framework"
  exit 64
fi

stage=${1:A}
internal=/Applications/PlayCover.app/Contents/Frameworks/PlayTools.framework
user_framework=$HOME/Library/Frameworks/PlayTools.framework
backup_root=/Users/hanbinnoh/Documents/ZZZ-ipa-keyboard/_work/install-backups
stamp=$(date +%Y%m%d-%H%M%S)
backup=$backup_root/playtools-$stamp

[[ -d $stage && -f $stage/PlayTools ]] || {
  print -u2 "invalid stage: $stage"
  exit 65
}

# Never replace code that is currently mapped by either process. This is the
# hard safety boundary after the prior kernel panic.
if pgrep -f '/Applications/PlayCover.app/Contents/MacOS/PlayCover' >/dev/null ||
   pgrep -f '/Library/Containers/io.playcover.PlayCover/Applications/com.HoYoverse.Nap.app/ZenlessZoneZero' >/dev/null; then
  print -u2 'refusing install: PlayCover or ZenlessZoneZero is still running'
  exit 20
fi

codesign --verify --deep --strict "$stage"
[[ $(xcrun vtool -show-build "$stage/PlayTools" | awk '/platform/{print $2; exit}') == MACCATALYST ]] || {
  print -u2 'refusing install: stage is not Mac Catalyst'
  exit 66
}

mkdir -p "$backup"
ditto "$internal" "$backup/internal.framework"
ditto "$user_framework" "$backup/user.framework"

internal_tmp=${internal}.codex-new-$stamp
user_tmp=${user_framework}.codex-new-$stamp
internal_old=${internal}.codex-old-$stamp
user_old=${user_framework}.codex-old-$stamp

restore() {
  local status=$?
  if (( status != 0 )); then
    if [[ -d $internal_old ]]; then
      [[ -e $internal ]] && mv "$internal" "${internal}.codex-failed-$stamp"
      mv "$internal_old" "$internal"
    fi
    if [[ -d $user_old ]]; then
      [[ -e $user_framework ]] && mv "$user_framework" "${user_framework}.codex-failed-$stamp"
      mv "$user_old" "$user_framework"
    fi
    print -u2 "install failed; rollback attempted (status=$status)"
  fi
  return $status
}
trap restore EXIT

ditto "$stage" "$internal_tmp"
ditto "$stage" "$user_tmp"
codesign --verify --deep --strict "$internal_tmp"
codesign --verify --deep --strict "$user_tmp"

mv "$internal" "$internal_old"
mv "$internal_tmp" "$internal"
mv "$user_framework" "$user_old"
mv "$user_tmp" "$user_framework"

codesign --force --deep --sign - --timestamp=none /Applications/PlayCover.app
codesign --verify --deep --strict /Applications/PlayCover.app

stage_hash=$(shasum -a 256 "$stage/PlayTools" | awk '{print $1}')
internal_hash=$(shasum -a 256 "$internal/PlayTools" | awk '{print $1}')
user_hash=$(shasum -a 256 "$user_framework/PlayTools" | awk '{print $1}')
[[ $stage_hash == $internal_hash && $stage_hash == $user_hash ]] || {
  print -u2 'installed framework hash mismatch'
  exit 67
}

trap - EXIT
print "installed PlayTools SHA256=$stage_hash"
print "backup=$backup"
print "old internal=$internal_old"
print "old user=$user_old"
