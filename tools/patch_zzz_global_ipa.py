#!/usr/bin/env python3
"""
ZZZ Global 3.1.0 IPA 패치 - GOAL_ZZZ_UI_LAYOUT.md 권위 패치 2건만 적용

  0xEC72310  e00313aa (mov x0,x19) -> 40008052 (mov w0,#2)   PC UI layout=2
  0xB3943B8  74a60139 (strb w20,[x19,#0x69]) -> 7fa60139 (strb wzr,[x19,#0x69])
              MouseInputEnhancement disabled

폐기된 패치 절대 포함 금지: C(0x13685CD8), F(0x13686110), AppUtils IsPC, IL2CPP IsPC,
SwitchUILayoutPlatform/ConfirmUILayout, OSPROD, shared getter/store (0x16584/0x16808/0x16850)

IPA 해제 -> 실행 권한 0755 강제/entitlement 보존 -> UnityFramework 바이트 검증 -> 2곳 patch
-> SC_Info 제거 -> codesign (framework -> app 순서) -> --verify -> 원자적 재패키징
-> IPA 내부 패치/실행 권한/심볼릭 링크 재검증
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import plistlib
import shutil
import stat
import subprocess
import tempfile
import zipfile

PATCHES = [
    (0xEC72310, bytes.fromhex("e00313aa"), bytes.fromhex("40008052"), "PC UI layout 2 (mov x0,x19 -> mov w0,#2)"),
    (0xB3943B8, bytes.fromhex("74a60139"), bytes.fromhex("7fa60139"), "MouseInputEnhancement disabled (strb w20 -> strb wzr)"),
]

# PlayCover 기준값 (IPA는 크기가 다를 수 있음 - 바이트 검증이 권위)
EXPECTED_SHA_PLAYCOVER = "0539ccadf353a40d83d23e15bc669f1fa8d8bcdbbbed12293fcb36abe9afed96"
EXPECTED_SIZE_PLAYCOVER = 477908736


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_checked(command: list[str], label: str) -> subprocess.CompletedProcess[str]:
    print(f"[{label}] {' '.join(command)}")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.stdout.strip():
        print(result.stdout.strip())
    if result.stderr.strip():
        print(result.stderr.strip())
    if result.returncode != 0:
        raise SystemExit(f"{label} failed ({result.returncode}): {result.stderr.strip()}")
    return result


def require_within(path: pathlib.Path, root: pathlib.Path, label: str) -> pathlib.Path:
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(root.resolve(strict=True))
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        raise SystemExit(f"{label} escapes or is missing from extraction root: {path}") from error
    return resolved


def require_directory(path: pathlib.Path, root: pathlib.Path, label: str) -> pathlib.Path:
    require_within(path, root, label)
    if path.is_symlink() or not stat.S_ISDIR(path.lstat().st_mode):
        raise SystemExit(f"{label} must be a real directory, not a symlink: {path}")
    return path


def require_regular_file(path: pathlib.Path, root: pathlib.Path, label: str) -> pathlib.Path:
    require_within(path, root, label)
    if path.is_symlink() or not stat.S_ISREG(path.lstat().st_mode):
        raise SystemExit(f"{label} must be a regular file, not a symlink: {path}")
    return path


def validate_zip_paths(input_ipa: pathlib.Path) -> None:
    with zipfile.ZipFile(input_ipa) as archive:
        for info in archive.infolist():
            member = pathlib.PurePosixPath(info.filename)
            if member.is_absolute() or ".." in member.parts:
                raise SystemExit(f"unsafe IPA member path: {info.filename}")


def extract_ipa(input_ipa: pathlib.Path, destination: pathlib.Path) -> None:
    if shutil.which("ditto") is None:
        raise SystemExit("ditto not found (macOS is required to preserve IPA metadata)")
    validate_zip_paths(input_ipa)
    run_checked(["ditto", "-x", "-k", "--norsrc", str(input_ipa), str(destination)], "unzip")


def find_unityframework(root: pathlib.Path) -> pathlib.Path:
    payload = root / "Payload"
    require_directory(payload, root, "Payload")
    apps = sorted(payload.glob("*.app"))
    if not apps:
        raise SystemExit(f"*.app not found under {payload}")
    candidates: list[pathlib.Path] = []
    for app in apps:
        require_directory(app, root, "app bundle")
        candidate = app / "Frameworks" / "UnityFramework.framework" / "UnityFramework"
        if os.path.lexists(candidate):
            candidates.append(require_regular_file(candidate, root, "UnityFramework"))
    if len(candidates) != 1:
        raise SystemExit(f"expected exactly one UnityFramework under Payload, found {len(candidates)}")
    return candidates[0]


def find_main_executable(app_dir: pathlib.Path, extraction_root: pathlib.Path) -> pathlib.Path:
    require_directory(app_dir, extraction_root, "app bundle")
    info_path = app_dir / "Info.plist"
    require_regular_file(info_path, extraction_root, "Info.plist")
    with info_path.open("rb") as stream:
        info = plistlib.load(stream)
    executable = info.get("CFBundleExecutable")
    if not isinstance(executable, str) or not executable:
        raise SystemExit(f"CFBundleExecutable missing in {info_path}")
    if pathlib.PurePosixPath(executable).name != executable or executable in {".", ".."}:
        raise SystemExit(f"unsafe CFBundleExecutable in {info_path}: {executable}")
    path = app_dir / executable
    return require_regular_file(path, extraction_root, "app executable")


def patch_binary(path: pathlib.Path) -> dict[str, object]:
    size = path.stat().st_size
    sha_before = sha256_file(path)
    print(f"[patch] target: {path}")
    print(f"[patch] size: {size}  sha256: {sha_before}")
    if size == EXPECTED_SIZE_PLAYCOVER and sha_before == EXPECTED_SHA_PLAYCOVER:
        print("[patch] PlayCover 기준 빌드 일치 (size/sha OK)")
    elif sha_before != EXPECTED_SHA_PLAYCOVER:
        print(f"[patch] NOTE: sha != PlayCover 기준 ({EXPECTED_SHA_PLAYCOVER[:8]}...), but offset 바이트 검증으로 진행")
        print("[patch]       IPA und3fined 빌드는 크기가 다를 수 있음 (IPA 489106224 vs PlayCover 477908736)")

    originals: list[tuple[int, bytes, bytes, str]] = []
    with path.open("r+b") as stream:
        for offset, original, replacement, description in PATCHES:
            stream.seek(offset)
            actual = stream.read(len(original))
            if actual == replacement:
                raise SystemExit(f"ABORT: already patched at 0x{offset:x}: {description}")
            if actual != original:
                raise SystemExit(
                    f"ABORT: {description} mismatch at 0x{offset:x}: "
                    f"expected {original.hex()} got {actual.hex()} (빌드 불일치 - 패치 중단)"
                )
            originals.append((offset, original, replacement, description))

        for offset, original, replacement, description in originals:
            stream.seek(offset)
            stream.write(replacement)
            print(f"[patch] 0x{offset:08x}  {original.hex()} -> {replacement.hex()}  {description}")
        stream.flush()
        os.fsync(stream.fileno())

    path.chmod(0o755)
    if path.stat().st_size != size:
        raise SystemExit("ABORT: file size changed")
    with path.open("rb") as stream:
        for offset, _, replacement, description in PATCHES:
            stream.seek(offset)
            if stream.read(len(replacement)) != replacement:
                raise SystemExit(f"ABORT: post-patch verify failed at 0x{offset:x} {description}")

    sha_after = sha256_file(path)
    changed_bytes = sum(
        left != right
        for _, original, replacement, _ in PATCHES
        for left, right in zip(original, replacement)
    )
    print(f"[patch] patched sha256: {sha_after}")
    print(f"[patch] verify OK ({changed_bytes} changed bytes, size preserved)")
    return {"before_sha": sha_before, "after_sha": sha_after, "size": size}


def preserve_entitlements(app_dir: pathlib.Path, destination: pathlib.Path) -> None:
    result = run_checked(
        ["codesign", "--display", "--xml", "--entitlements", "-", str(app_dir)],
        "entitlements",
    )
    try:
        entitlements = plistlib.loads(result.stdout.encode())
    except Exception as error:
        raise SystemExit(f"could not parse existing app entitlements: {error}") from error
    if not isinstance(entitlements, dict) or not entitlements:
        raise SystemExit("existing app entitlements are empty; refusing to strip them during re-sign")
    with destination.open("wb") as stream:
        plistlib.dump(entitlements, stream, fmt=plistlib.FMT_XML, sort_keys=True)
    print(f"[entitlements] preserved {len(entitlements)} keys")


def remove_sc_info(payload_root: pathlib.Path) -> None:
    removed: list[str] = []
    for path in payload_root.rglob("SC_Info"):
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink()
        removed.append(str(path))
    print(f"[sign] removed {len(removed)} SC_Info entries")
    for item in removed[:10]:
        print(f"       - {item}")


def codesign(path: pathlib.Path, identity: str, entitlements: pathlib.Path | None = None) -> None:
    command = ["codesign", "--force", "--sign", identity, "--timestamp=none"]
    if entitlements is not None:
        command.extend(["--entitlements", str(entitlements)])
    command.append(str(path))
    run_checked(command, "sign")


def verify_signature(path: pathlib.Path) -> None:
    run_checked(["codesign", "--verify", "--deep", "--strict", str(path)], "verify")
    print("[verify] OK")


def collect_symlinks(root: pathlib.Path) -> set[str]:
    links: set[str] = set()
    for current, directories, files in os.walk(root, followlinks=False):
        for name in directories + files:
            path = pathlib.Path(current) / name
            if path.is_symlink():
                require_within(path, root, "symlink target")
                links.add(path.relative_to(root).as_posix())
    return links


def repack_payload(payload_dir: pathlib.Path, output_ipa: pathlib.Path) -> None:
    run_checked(
        ["ditto", "-c", "-k", "--norsrc", "--keepParent", str(payload_dir), str(output_ipa)],
        "repack",
    )


def verify_repacked_ipa(
    output_ipa: pathlib.Path,
    app_relative: pathlib.Path,
    executable_relative: pathlib.Path,
    expected_symlinks: set[str],
    require_no_sc_info: bool,
) -> None:
    with zipfile.ZipFile(output_ipa, "r") as archive:
        bad_entry = archive.testzip()
        if bad_entry is not None:
            raise SystemExit(f"ABORT: CRC verification failed for {bad_entry}")
        names = archive.namelist()
        unity_names = [name for name in names if name.endswith("UnityFramework.framework/UnityFramework")]
        if len(unity_names) != 1:
            raise SystemExit(f"ABORT: expected exactly one UnityFramework in IPA, found {len(unity_names)}")
        with archive.open(unity_names[0]) as stream:
            for offset, _, replacement, description in PATCHES:
                stream.seek(offset)
                actual = stream.read(len(replacement))
                if actual != replacement:
                    raise SystemExit(
                        f"ABORT: repacked IPA mismatch at 0x{offset:x}: "
                        f"expected {replacement.hex()} got {actual.hex()} ({description})"
                    )

        executable_name = executable_relative.as_posix()
        try:
            executable_info = archive.getinfo(executable_name)
        except KeyError as error:
            raise SystemExit(f"ABORT: app executable missing from IPA: {executable_name}") from error
        executable_mode = executable_info.external_attr >> 16
        if not executable_mode & 0o111:
            raise SystemExit(f"ABORT: app executable lost execute permission: mode={executable_mode:o}")

        archive_symlinks = {
            info.filename
            for info in archive.infolist()
            if stat.S_ISLNK(info.external_attr >> 16)
        }
        if archive_symlinks != expected_symlinks:
            raise SystemExit(
                "ABORT: symlink set changed during repack: "
                f"expected={sorted(expected_symlinks)} actual={sorted(archive_symlinks)}"
            )

        sc_info = [name for name in names if "SC_Info" in pathlib.PurePosixPath(name).parts]
        if require_no_sc_info and sc_info:
            raise SystemExit(f"ABORT: SC_Info still in IPA ({len(sc_info)} entries)")

        app_prefix = app_relative.as_posix().rstrip("/") + "/"
        if not any(name.startswith(app_prefix) for name in names):
            raise SystemExit(f"ABORT: app bundle missing from IPA: {app_relative}")

    print("[final] IPA CRC/patch bytes/app executable/symlink verification OK")
    if require_no_sc_info:
        print("[final] SC_Info 제거 확인")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_ipa", type=pathlib.Path, help="입력 IPA (ex: com.HoYoverse.Nap_3.1.0_und3fined.ipa)")
    parser.add_argument("output_ipa", type=pathlib.Path, nargs="?", help="출력 IPA (default: <input>-pcui.ipa)")
    parser.add_argument("--identity", default="-", help="codesign identity (default: '-' ad-hoc, TrollStore/사이로드용). Apple Developer는 'Apple Development: ...' 또는 Team ID")
    parser.add_argument("--no-sign", action="store_true", help="서명 생략 (패치만, 테스트용)")
    parser.add_argument("--keep-workdir", action="store_true", help="임시 작업 디렉터리 유지 (디버그용)")
    options = parser.parse_args()

    input_ipa = options.input_ipa.resolve()
    if not input_ipa.is_file():
        raise SystemExit(f"input not found: {input_ipa}")
    output_ipa = options.output_ipa.resolve() if options.output_ipa else input_ipa.with_name(input_ipa.stem + "-pcui.ipa")
    if output_ipa == input_ipa:
        raise SystemExit("input and output IPA must be different (the source IPA is never overwritten)")

    output_ipa.parent.mkdir(parents=True, exist_ok=True)
    print(f"[ipa] input : {input_ipa} ({input_ipa.stat().st_size} bytes)")
    print(f"[ipa] output: {output_ipa}")
    print(f"[ipa] identity: {options.identity} {'(skip)' if options.no_sign else ''}")

    work = pathlib.Path(tempfile.mkdtemp(prefix="zzz_ipa_"))
    unzip_dir = work / "unzip"
    unzip_dir.mkdir()
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_ipa.name}.",
        suffix=".tmp",
        dir=output_ipa.parent,
    )
    os.close(descriptor)
    temporary_output = pathlib.Path(temporary_name)
    temporary_output.unlink()

    try:
        extract_ipa(input_ipa, unzip_dir)
        unity_framework = find_unityframework(unzip_dir)
        app_dir = unity_framework.parents[2]
        payload_dir = unzip_dir / "Payload"
        main_executable = find_main_executable(app_dir, unzip_dir)

        # IPA launchability is metadata-sensitive. Make both known executables
        # explicit even if the source ZIP came from a tool that dropped modes.
        main_executable.chmod(0o755)
        unity_framework.chmod(0o755)
        expected_symlinks = collect_symlinks(unzip_dir)
        patch_info = patch_binary(unity_framework)

        if not options.no_sign:
            entitlements_path = work / "app-entitlements.plist"
            preserve_entitlements(app_dir, entitlements_path)
            remove_sc_info(payload_dir)
            codesign(unity_framework.parent, options.identity)
            codesign(app_dir, options.identity, entitlements_path)
            verify_signature(app_dir)
        else:
            print("[sign] --no-sign: 서명 및 SC_Info 제거 단계 생략")

        print(f"[repack] creating temporary archive for {output_ipa} ...")
        repack_payload(payload_dir, temporary_output)
        verify_repacked_ipa(
            temporary_output,
            app_dir.relative_to(unzip_dir),
            main_executable.relative_to(unzip_dir),
            expected_symlinks,
            require_no_sc_info=not options.no_sign,
        )
        os.replace(temporary_output, output_ipa)
        print(f"[repack] committed atomically: {output_ipa} ({output_ipa.stat().st_size} bytes)")
        print(
            "[done] patched UnityFramework "
            f"before={str(patch_info['before_sha'])[:12]} "
            f"after={str(patch_info['after_sha'])[:12]} size={patch_info['size']}"
        )
    finally:
        if temporary_output.exists():
            temporary_output.unlink()
        if options.keep_workdir:
            print(f"[workdir] kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
