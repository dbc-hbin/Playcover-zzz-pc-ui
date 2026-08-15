#!/usr/bin/env python3
"""Retarget the verified PlayTools Y-fix profile to the null-safe, no-city-gate ZZZ build."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import subprocess
import tempfile


EXPECTED_INPUT_SHA256 = "70cea52da6a5c862727dbac42cb32798465ca8f13755883706f5c7946281c0c7"

# This is a profile fingerprint stored in PlayTools, not a game-code patch.
# The null-safe UnityFramework predates the discarded city-gate experiment, so
# only the expected city-gate bytes differ from the verified Y-fix profile.
FINGERPRINT_PATCHES = (
    (
        bytes.fromhex("1f2003d5e00313aafd000094681a40f9"),
        bytes.fromhex("60000034e00313aafd000094681a40f9"),
        "discarded city-gate site",
    ),
)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_zip", type=pathlib.Path)
    parser.add_argument("output_zip", type=pathlib.Path)
    args = parser.parse_args()

    input_zip = args.input_zip.resolve()
    output_zip = args.output_zip.resolve()
    if not input_zip.is_file():
        raise SystemExit(f"input archive not found: {input_zip}")

    work = pathlib.Path(tempfile.mkdtemp(prefix="playtools-canonical-profile-"))
    try:
        run("ditto", "-x", "-k", str(input_zip), str(work))
        frameworks = list(work.glob("*.framework"))
        if len(frameworks) != 1:
            raise SystemExit(f"expected one framework, found {len(frameworks)}")
        framework = frameworks[0]
        binary = framework / "PlayTools"
        before_sha = sha256(binary)
        if before_sha != EXPECTED_INPUT_SHA256:
            raise SystemExit(
                f"input PlayTools hash mismatch: expected={EXPECTED_INPUT_SHA256} actual={before_sha}"
            )

        data = binary.read_bytes()
        patched = data
        for original, replacement, label in FINGERPRINT_PATCHES:
            count = patched.count(original)
            if count != 1:
                raise SystemExit(f"{label}: expected one fingerprint, found {count}")
            patched = patched.replace(original, replacement, 1)
            print(f"patched profile fingerprint: {label}")
        if len(patched) != len(data):
            raise SystemExit("PlayTools binary size changed")
        binary.write_bytes(patched)
        binary.chmod(0o755)

        plugin = framework / "PlugIns" / "AKInterface.bundle"
        if plugin.is_dir():
            run("codesign", "--force", "--sign", "-", "--timestamp=none", str(plugin))
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(framework))
        run("codesign", "--verify", "--deep", "--strict", str(framework))

        output_zip.parent.mkdir(parents=True, exist_ok=True)
        if output_zip.exists():
            output_zip.unlink()
        run(
            "ditto",
            "-c",
            "-k",
            "--sequesterRsrc",
            "--keepParent",
            str(framework),
            str(output_zip),
        )
        print(f"PlayTools SHA256={sha256(binary)}")
        print(f"archive SHA256={sha256(output_zip)}")
        print(f"output={output_zip}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
