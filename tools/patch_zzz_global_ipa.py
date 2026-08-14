#!/usr/bin/env python3
"""
ZZZ Global 3.1.0 IPA 패치 - GOAL_ZZZ_UI_LAYOUT.md 권위 패치 2건만 적용

  0xEC72310  e00313aa (mov x0,x19) -> 40008052 (mov w0,#2)   PC UI layout=2
  0xB3943B8  74a60139 (strb w20,[x19,#0x69]) -> 7fa60139 (strb wzr,[])  MouseInputEnhancement disabled

폐기된 패치 절대 포함 금지: C(0x13685CD8), F(0x13686110), AppUtils IsPC, IL2CPP IsPC,
SwitchUILayoutPlatform/ConfirmUILayout, OSPROD, shared getter/store (0x16584/0x16808/0x16850)

IPA 해제 -> UnityFramework 바이트 검증 -> 2곳 patch -> chmod 755
-> SC_Info 제거 -> codesign (framework -> app 순서) -> --verify -> zip 재패키징
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

PATCHES = [
    (0xEC72310, bytes.fromhex("e00313aa"), bytes.fromhex("40008052"), "PC UI layout 2 (mov x0,x19 -> mov w0,#2)"),
    (0xB3943B8, bytes.fromhex("74a60139"), bytes.fromhex("7fa60139"), "MouseInputEnhancement disabled (strb w20 -> strb wzr)"),
]

# PlayCover 기준값 (IPA는 크기가 다를 수 있음 - 바이트 검증이 권위)
EXPECTED_SHA_PLAYCOVER = "0539ccadf353a40d83d23e15bc669f1fa8d8bcdbbbed12293fcb36abe9afed96"
EXPECTED_SIZE_PLAYCOVER = 477908736

def sha256_file(p: pathlib.Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def find_unityframework(root: pathlib.Path) -> pathlib.Path:
    # Payload/*.app/Frameworks/UnityFramework.framework/UnityFramework
    payload = root / "Payload"
    if not payload.is_dir():
        raise SystemExit(f"Payload not found under {root}")
    apps = list(payload.glob("*.app"))
    if not apps:
        raise SystemExit(f"*.app not found under {payload}")
    # pick the one that has Frameworks/UnityFramework.framework
    for app in apps:
        cand = app / "Frameworks" / "UnityFramework.framework" / "UnityFramework"
        if cand.is_file():
            return cand
    # fallback: recursive search
    for cand in payload.rglob("UnityFramework.framework/UnityFramework"):
        if cand.is_file():
            return cand
    raise SystemExit("UnityFramework not found under Payload")

def patch_binary(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    size = len(data)
    sha_before = hashlib.sha256(data).hexdigest()
    print(f"[patch] target: {path}")
    print(f"[patch] size: {size}  sha256: {sha_before}")
    if size == EXPECTED_SIZE_PLAYCOVER and sha_before == EXPECTED_SHA_PLAYCOVER:
        print(f"[patch] PlayCover 기준 빌드 일치 (size/sha OK)")
    elif sha_before != EXPECTED_SHA_PLAYCOVER:
        print(f"[patch] NOTE: sha != PlayCover 기준 ({EXPECTED_SHA_PLAYCOVER[:8]}...), but offset 바이트 검증으로 진행")
        print(f"[patch]       IPA und3fined 빌드는 크기가 다를 수 있음 (IPA 489106224 vs PlayCover 477908736)")

    patched = bytearray(data)
    for off, orig, repl, desc in PATCHES:
        actual = data[off:off+4]
        if actual != orig:
            raise SystemExit(f"ABORT: {desc} mismatch at 0x{off:x}: expected {orig.hex()} got {actual.hex()} (빌드 불일치 - 패치 중단)")
        patched[off:off+4] = repl
        print(f"[patch] 0x{off:08x}  {orig.hex()} -> {repl.hex()}  {desc}")

    # 바깥 영역 불변 검증
    # 실제 변경 바이트는 5개: 0xEC72310 4바이트 + 0xB3943B8 1바이트(74->7f)
    expected_diff = sum(1 for off, orig, repl, _ in PATCHES for a, b in zip(orig, repl) if a != b)
    diff_idx = [i for i in range(len(data)) if data[i] != patched[i]]
    if len(diff_idx) != expected_diff:
        raise SystemExit(f"ABORT: unexpected diff count {len(diff_idx)} (expected {expected_diff})")
    allowed = set()
    for off, _, _, _ in PATCHES:
        allowed.update(range(off, off+4))
    if not set(diff_idx).issubset(allowed):
        raise SystemExit(f"ABORT: diff outside allowed offsets: {diff_idx[:20]}")

    path.write_bytes(patched)
    # preserve executable bit
    path.chmod(0o755)
    sha_after = sha256_file(path)
    print(f"[patch] patched sha256: {sha_after}")
    if len(patched) != size:
        raise SystemExit("ABORT: file size changed")
    # 재검증
    data2 = path.read_bytes()
    for off, _, repl, desc in PATCHES:
        if data2[off:off+4] != repl:
            raise SystemExit(f"ABORT: post-patch verify failed at 0x{off:x} {desc}")
    print(f"[patch] verify OK (8 bytes, size preserved)")
    return {"before_sha": sha_before, "after_sha": sha_after, "size": size}

def remove_sc_info(app_root: pathlib.Path):
    # App Store DRM SC_Info 제거 (재서명 시 불필요/충돌)
    removed = []
    for p in app_root.rglob("SC_Info"):
        if p.is_dir():
            shutil.rmtree(p)
            removed.append(str(p))
    for fw in app_root.rglob("_CodeSignature"):
        if fw.is_dir():
            shutil.rmtree(fw)
            removed.append(str(fw))
    if removed:
        print(f"[sign] removed {len(removed)} SC_Info/_CodeSignature dirs")
        for r in removed[:10]:
            print(f"       - {r}")

def codesign_many(paths: list[pathlib.Path], identity: str) -> None:
    for p in paths:
        if not p.exists():
            print(f"[sign] skip (not found): {p}")
            continue
        cmd = ["codesign", "--force", "--sign", identity, "--timestamp=none", str(p)]
        # framework / app 모두 ad-hoc이면 --timestamp=none 필요
        print(f"[sign] {' '.join(cmd)}")
        r = subprocess.run(cmd, capture_output=True, text=True)
        print(r.stdout.strip())
        if r.stderr:
            print(r.stderr.strip())
        if r.returncode != 0:
            raise SystemExit(f"codesign failed for {p}: {r.stderr}")

def verify(path: pathlib.Path) -> None:
    cmd = ["codesign", "--verify", "--deep", "--strict", str(path)]
    print(f"[verify] {' '.join(cmd)}")
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout.strip())
    if r.stderr:
        print(r.stderr.strip())
    if r.returncode != 0:
        raise SystemExit(f"codesign --verify failed: {r.stderr}")
    print("[verify] OK")

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input_ipa", type=pathlib.Path, help="입력 IPA (ex: com.HoYoverse.Nap_3.1.0_und3fined.ipa)")
    ap.add_argument("output_ipa", type=pathlib.Path, nargs="?", help="출력 IPA (default: <input>-pcui.ipa)")
    ap.add_argument("--identity", default="-", help="codesign identity (default: '-' ad-hoc, TrollStore/사이로드용). Apple Developer는 'Apple Development: ...' 또는 Team ID")
    ap.add_argument("--no-sign", action="store_true", help="서명 생략 (패치만, 테스트용)")
    ap.add_argument("--keep-workdir", action="store_true", help="임시 작업 디렉터리 유지 (디버그용)")
    ns = ap.parse_args()

    inp = ns.input_ipa.resolve()
    if not inp.is_file():
        raise SystemExit(f"input not found: {inp}")
    out = (ns.output_ipa.resolve() if ns.output_ipa else inp.with_name(inp.stem + "-pcui.ipa"))
    identity = ns.identity

    print(f"[ipa] input : {inp} ({inp.stat().st_size} bytes)")
    print(f"[ipa] output: {out}")
    print(f"[ipa] identity: {identity} {'(skip)' if ns.no_sign else ''}")

    work = pathlib.Path(tempfile.mkdtemp(prefix="zzz_ipa_"))
    unzip_dir = work / "unzip"
    unzip_dir.mkdir(parents=True)

    try:
        print(f"[unzip] extracting to {unzip_dir} ...")
        with zipfile.ZipFile(inp, 'r') as z:
            z.extractall(unzip_dir)
        print(f"[unzip] done")

        uf = find_unityframework(unzip_dir)
        app_dir = uf.parents[2]  # .../Payload/<App>.app
        # Payload 경로 찾기
        payload_dir = unzip_dir / "Payload"

        info = patch_binary(uf)

        if not ns.no_sign:
            remove_sc_info(payload_dir)
            fw_dir = uf.parent  # UnityFramework.framework
            # 서명 순서: framework -> app (GOAL 문서와 동일)
            codesign_many([fw_dir, app_dir], identity)
            verify(app_dir)
        else:
            print("[sign] --no-sign: 서명 단계 생략")

        # IPA 재패키징: Payload를 루트로 zip
        # ditto가 있으면 ditto 사용 (macOS 권장), 없으면 zipfile
        print(f"[repack] creating {out} ...")
        out.parent.mkdir(parents=True, exist_ok=True)
        if out.exists():
            out.unlink()
        # codesign 후 파일 권한/시간 보정
        # zipfile로 재패키징 (STORE vs DEFLATED 선택: IPA는 보통 DEFLATED, UnityFramework는 이미 압축 효율 낮음)
        # 원본과 동일하게: Payload/ 전체를 DEFLATED로
        with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
            for root, dirs, files in os.walk(unzip_dir):
                for fname in files:
                    fpath = pathlib.Path(root) / fname
                    arc = fpath.relative_to(unzip_dir)
                    # symlink 처리 (있으면)
                    if fpath.is_symlink():
                        # zipfile은 symlink를 지원하지 않아 readlink를 텍스트로 저장하지 않음
                        # IPA에는 symlink가 거의 없으므로 경고만
                        print(f"[repack] WARN symlink skipped: {arc} -> {os.readlink(fpath)}")
                        continue
                    zout.write(fpath, arc)
                # 빈 디렉터리도 필요하면 추가
                for d in dirs:
                    dpath = pathlib.Path(root) / d
                    if not any(dpath.iterdir()):
                        arc = dpath.relative_to(unzip_dir).as_posix() + "/"
                        zout.writestr(arc, b"")

        print(f"[repack] done: {out} ({out.stat().st_size} bytes)")
        print(f"[done] patched UnityFramework before={info['before_sha'][:12]} after={info['after_sha'][:12]} size={info['size']}")

        # 최종 바이트 재검증 (IPA 내부)
        with zipfile.ZipFile(out, 'r') as z:
            # UnityFramework 경로 찾기
            uf_names = [n for n in z.namelist() if n.endswith("UnityFramework.framework/UnityFramework")]
            if uf_names:
                with z.open(uf_names[0]) as f:
                    f.seek(0xEC72310); b1 = f.read(4).hex()
                    f.seek(0xB3943B8); b2 = f.read(4).hex()
                print(f"[final] IPA 내부 0xEC72310={b1} (expect 40008052)  0xB3943B8={b2} (expect 7fa60139)")
                if b1 != "40008052" or b2 != "7fa60139":
                    raise SystemExit("ABORT: 재패키징된 IPA 내부 바이트 검증 실패")
                print("[final] IPA 내부 바이트 검증 OK")
            else:
                print("[final] WARN: UnityFramework not found in repacked IPA (unexpected)")

        # SC_Info 없는지 확인
        with zipfile.ZipFile(out, 'r') as z:
            sc = [n for n in z.namelist() if "SC_Info" in n]
            if sc:
                print(f"[final] WARN: SC_Info still in IPA ({len(sc)} entries) - DRM 잔재")
            else:
                print("[final] SC_Info 제거 확인")

    finally:
        if ns.keep_workdir:
            print(f"[workdir] kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)

if __name__ == "__main__":
    main()
