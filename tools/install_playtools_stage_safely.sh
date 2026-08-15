#!/bin/zsh
set -euo pipefail

if (( $# != 1 )); then
  print -u2 "usage: $0 /absolute/path/to/PlayTools.framework"
  exit 64
fi

stage=${1:A}
playcover_app=/Applications/PlayCover.app
playcover_exec=$playcover_app/Contents/MacOS/PlayCover
user_framework=$HOME/Library/Frameworks/PlayTools.framework
backup_root=${0:A:h}/../_work/install-backups
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
codesign --verify --deep --strict "$playcover_app"
playcover_hash_before=$(shasum -a 256 "$playcover_exec" | awk '{print $1}')

mkdir -p "$backup"
chflags -R nouchg "$user_framework"
ditto "$user_framework" "$backup/user.framework"

user_tmp=${user_framework}.codex-new-$stamp
user_old=${user_framework}.codex-old-$stamp

restore() {
  local status=$?
  if (( status != 0 )); then
    if [[ -d $user_old ]]; then
      [[ -e $user_framework ]] && chflags -R nouchg "$user_framework"
      [[ -e $user_framework ]] && mv "$user_framework" "${user_framework}.codex-failed-$stamp"
      mv "$user_old" "$user_framework"
      chflags -R uchg "$user_framework"
    fi
    print -u2 "install failed; rollback attempted (status=$status)"
  fi
  return $status
}
trap restore EXIT

ditto "$stage" "$user_tmp"
codesign --verify --deep --strict "$user_tmp"

mv "$user_framework" "$user_old"
mv "$user_tmp" "$user_framework"
chflags -R uchg "$user_framework"

stage_hash=$(shasum -a 256 "$stage/PlayTools" | awk '{print $1}')
user_hash=$(shasum -a 256 "$user_framework/PlayTools" | awk '{print $1}')
[[ $stage_hash == $user_hash ]] || {
  print -u2 'installed framework hash mismatch'
  exit 67
}
playcover_hash_after=$(shasum -a 256 "$playcover_exec" | awk '{print $1}')
[[ $playcover_hash_before == $playcover_hash_after ]] || {
  print -u2 'PlayCover changed during installation'
  exit 68
}
codesign --verify --deep --strict "$playcover_app"

trap - EXIT
print "installed PlayTools SHA256=$stage_hash"
print "backup=$backup"
print "old user=$user_old"
print "PlayCover preserved SHA256=$playcover_hash_after"
