# Repository Guidelines

## Project Structure & Module Organization

- `tools/` contains the Python IPA patcher and zsh build, install, and launch helpers.
- `src/PlayTools/` vendors the patched PlayTools Xcode project. Swift, Objective-C, and C sources live under `src/PlayTools/PlayTools/`; native Unity input code is in `Controls/NativeMouse/`.
- `tests/` contains Python `unittest` regression tests. Name new modules `test_*.py` and test methods `test_*`.
- `patches/` holds the reproducible PlayCover source patch. `dist/` contains release artifacts; `_work/` is for local analysis, backups, and temporary build evidence.
- Treat `STABLE_BUILD.md`, `GOAL_ZZZ_UI_LAYOUT.md`, and the patch table in `README.md` as the authority for supported UnityFramework changes.

## Build, Test, and Development Commands

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
zsh -n tools/*.sh
python3 -m py_compile tools/*.py
zsh tools/build_playtools.sh
zsh tools/build_patched_playcover.sh
```

The first three commands run regressions and syntax checks. The build scripts require macOS, Xcode command-line tools, and create signed or ad-hoc artifacts under `dist/`. For a patch-only smoke test, run `python3 tools/patch_zzz_global_ipa.py input.ipa output.ipa --no-sign` against a user-owned 3.1.0 IPA.

## Coding Style & Naming Conventions

Use four-space indentation in Python and follow the existing Swift/Xcode formatting. Keep C bridge changes consistent with nearby declarations and fail-closed validation. Shell scripts target zsh and should retain `set -euo pipefail`, quoted paths, and explicit hash/signature checks. SwiftLint configuration is in `src/PlayTools/.swiftlint.yml`; do not reformat vendored code unrelated to the change.

## Testing Guidelines

Add a focused regression for every patching, archive-integrity, path-safety, or input-routing bug. Tests must not require a real IPA or mutate installed applications. When binaries change, also report architecture, code-sign verification, and SHA-256 results; runtime claims require an actual device or PlayCover test.

## Commit & Pull Request Guidelines

Recent history uses concise Conventional Commit prefixes such as `feat:`, `fix:`, and `chore:`. Keep commits narrowly scoped. Pull requests should describe the exact game/build target, list commands run, identify changed offsets or fingerprints, and include screenshots or logs for UI/runtime behavior. Link relevant issues when available.

## Security & Artifact Hygiene

Never commit IPAs, DMGs, credentials, signing identities, absolute user paths, or container data. Do not add unverified RVAs or broaden the documented two-site game patch without exact-version byte evidence and rollback checks.
