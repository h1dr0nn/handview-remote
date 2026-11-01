#!/usr/bin/env python3
"""Assemble the Handview Godot addon release zip.

Builds nothing itself. It takes already built files (release DLL, signed APK)
and packs them with the addon sources from godot-addon/addons/handview.

    python tools/package/package.py --dll handview.dll --apk handview.apk \
        --version 0.1.0 --version-code 1 --out dist

Standard library only. Run `--self-test` to check the zip layout on dummy files.
"""

import argparse
import hashlib
import os
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ADDON_DIR = REPO_ROOT / "godot-addon"

ADDON_PREFIX = "addons/handview"
DLL_RELEASE_NAME = "handview.windows.template_release.x86_64.dll"
DLL_DEBUG_NAME = "handview.windows.template_debug.x86_64.dll"
APK_NAME = "handview.apk"
VERSION_FILE_NAME = "handview.version"

SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")
# Fixed timestamp so the same inputs always give the same zip bytes.
ZIP_EPOCH = (2020, 1, 1, 0, 0, 0)


class PackageError(Exception):
    pass


def _is_excluded(rel: Path) -> bool:
    """rel is relative to addons/handview. Skip generated, local and debug files."""
    parts = rel.parts
    name = parts[-1]
    if parts[0] == "bin":
        return True  # the DLLs are supplied explicitly, never scanned
    if parts[0] == "android" and (name.endswith(".apk") or name == VERSION_FILE_NAME):
        return True  # the APK and its version come from the arguments
    if name == ".gitkeep" or name.startswith("~") or name.endswith((".pdb", ".tmp")):
        return True
    if any(part in (".godot", "__pycache__") for part in parts):
        return True
    return False


def collect_addon_files(addon_root: Path) -> dict:
    """Return {zip name: source path} for the addon sources."""
    plugin_dir = addon_root / ADDON_PREFIX
    if not plugin_dir.is_dir():
        raise PackageError(f"addon directory not found: {plugin_dir}")
    files = {}
    for path in sorted(plugin_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(plugin_dir)
        if _is_excluded(rel):
            continue
        files[f"{ADDON_PREFIX}/{rel.as_posix()}"] = path
    return files


def _check_dll(path: Path, label: str) -> None:
    if not path.is_file():
        raise PackageError(f"{label} not found: {path}")
    with path.open("rb") as handle:
        if handle.read(2) != b"MZ":
            raise PackageError(f"{label} is not a Windows PE file: {path}")


def _plugin_cfg_version(plugin_cfg: Path):
    match = re.search(r'^version\s*=\s*"([^"]*)"', plugin_cfg.read_text(encoding="utf-8"), re.M)
    return match.group(1) if match else None


def _write_entry(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, ZIP_EPOCH)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    archive.writestr(info, data)


def build_zip(addon_root, dll, apk, version, version_code, out_dir, debug_dll=None) -> tuple:
    """Create the zip and its checksum file. Returns (zip path, checksum path)."""
    addon_root, dll, apk, out_dir = Path(addon_root), Path(dll), Path(apk), Path(out_dir)
    if not SEMVER_RE.match(version):
        raise PackageError(f"version is not semver: {version!r}")
    if not isinstance(version_code, int) or version_code < 1:
        raise PackageError(f"version code must be a positive integer: {version_code!r}")

    _check_dll(dll, "release DLL")
    if debug_dll is not None:
        _check_dll(Path(debug_dll), "debug DLL")
    if not apk.is_file():
        raise PackageError(f"APK not found: {apk}")

    files = collect_addon_files(addon_root)

    plugin_cfg = addon_root / ADDON_PREFIX / "plugin.cfg"
    if plugin_cfg.is_file():
        cfg_version = _plugin_cfg_version(plugin_cfg)
        if cfg_version != version:
            raise PackageError(
                f"plugin.cfg version {cfg_version!r} does not match release version {version!r}; "
                "update plugin.cfg first"
            )

    # Explicit entries are added after the scan so it can never shadow them.
    # The editor loads the debug entry, so the release build ships under both
    # names unless a separate debug DLL is given.
    files[f"{ADDON_PREFIX}/bin/{DLL_RELEASE_NAME}"] = dll
    files[f"{ADDON_PREFIX}/bin/{DLL_DEBUG_NAME}"] = Path(debug_dll) if debug_dll else dll
    files[f"{ADDON_PREFIX}/android/{APK_NAME}"] = apk
    # MIT requires the notice to travel with the code.
    license_file = addon_root.parent / "LICENSE"
    if license_file.is_file():
        files[f"{ADDON_PREFIX}/LICENSE"] = license_file

    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"handview-godot-addon-{version}.zip"
    tmp_path = zip_path.with_name(zip_path.name + ".tmp")
    with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            _write_entry(archive, name, files[name].read_bytes())
        _write_entry(archive, f"{ADDON_PREFIX}/android/{VERSION_FILE_NAME}", f"{version_code}\n".encode("ascii"))
    os.replace(tmp_path, zip_path)

    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    checksum_path = zip_path.with_name(zip_path.name + ".sha256")
    # sha256sum format: "<hex>  <name>", so `sha256sum -c` works.
    checksum_path.write_text(f"{digest}  {zip_path.name}\n", encoding="ascii", newline="\n")
    return zip_path, checksum_path


def self_test() -> None:
    def check(condition, message):
        if not condition:
            raise AssertionError(message)

    root = Path(tempfile.mkdtemp(prefix="handview-package-test-"))
    try:
        plugin = root / "godot-addon" / ADDON_PREFIX
        dummy_tree = {
            "plugin.cfg": b'[plugin]\n\nname="Handview"\nversion="1.2.3"\nscript="plugin.gd"\n',
            "plugin.gd": b"extends EditorPlugin\n",
            "plugin.gd.uid": b"uid://abc\n",
            "handview.gdextension": b"[configuration]\n",
            "editor/dock.gd": b"extends Control\n",
            "editor/dock.gd.uid": b"uid://def\n",
            "bin/.gitkeep": b"",
            "bin/stale.pdb": b"pdb",
            "bin/~locked.dll": b"MZ",
            "bin/" + DLL_DEBUG_NAME: b"MZ-stale-debug",
            "android/.gitkeep": b"",
            "android/old.apk": b"old",
            "android/" + VERSION_FILE_NAME: b"99\n",
            ".godot/cache.bin": b"x",
        }
        for rel, data in dummy_tree.items():
            target = plugin / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (root / "godot-addon" / "tests").mkdir()
        (root / "godot-addon" / "tests" / "test_x.gd").write_bytes(b"extends RefCounted\n")

        release_dll = root / "release.dll"
        release_dll.write_bytes(b"MZ-release")
        debug_dll = root / "debug.dll"
        debug_dll.write_bytes(b"MZ-debug")
        apk = root / "app.apk"
        apk.write_bytes(b"PK-apk")
        addon_root = root / "godot-addon"

        expected = {
            "addons/handview/plugin.cfg",
            "addons/handview/plugin.gd",
            "addons/handview/plugin.gd.uid",
            "addons/handview/handview.gdextension",
            "addons/handview/editor/dock.gd",
            "addons/handview/editor/dock.gd.uid",
            "addons/handview/bin/" + DLL_RELEASE_NAME,
            "addons/handview/bin/" + DLL_DEBUG_NAME,
            "addons/handview/android/" + APK_NAME,
            "addons/handview/android/" + VERSION_FILE_NAME,
        }

        # Release DLL under both names.
        out = root / "out"
        zip_path, checksum_path = build_zip(addon_root, release_dll, apk, "1.2.3", 7, out)
        check(zip_path.name == "handview-godot-addon-1.2.3.zip", "zip name")
        with zipfile.ZipFile(zip_path) as archive:
            check(archive.testzip() is None, "zip integrity")
            check(set(archive.namelist()) == expected, f"layout: {sorted(archive.namelist())}")
            check(archive.read("addons/handview/bin/" + DLL_RELEASE_NAME) == b"MZ-release", "release dll")
            check(archive.read("addons/handview/bin/" + DLL_DEBUG_NAME) == b"MZ-release",
                  "debug name holds the release dll")
            check(archive.read("addons/handview/android/" + APK_NAME) == b"PK-apk", "apk")
            check(archive.read("addons/handview/android/" + VERSION_FILE_NAME) == b"7\n", "version code")
        digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
        check(checksum_path.read_text() == f"{digest}  {zip_path.name}\n", "checksum file")

        # Reproducible output.
        first = zip_path.read_bytes()
        build_zip(addon_root, release_dll, apk, "1.2.3", 7, out)
        check(zip_path.read_bytes() == first, "zip is not reproducible")

        # Separate debug DLL.
        zip_path2, _ = build_zip(addon_root, release_dll, apk, "1.2.3", 7, root / "out2", debug_dll=debug_dll)
        with zipfile.ZipFile(zip_path2) as archive:
            check(set(archive.namelist()) == expected, "layout with --debug-dll")
            check(archive.read("addons/handview/bin/" + DLL_DEBUG_NAME) == b"MZ-debug", "separate debug dll")
            check(archive.read("addons/handview/bin/" + DLL_RELEASE_NAME) == b"MZ-release", "release dll kept")

        # Bad inputs are rejected and leave no zip behind.
        def rejects(message, **overrides):
            args = dict(addon_root=addon_root, dll=release_dll, apk=apk, version="1.2.3",
                        version_code=7, out_dir=root / "out3")
            args.update(overrides)
            try:
                build_zip(**args)
            except PackageError:
                return
            raise AssertionError(f"accepted: {message}")

        rejects("bad semver", version="1.2")
        rejects("version code 0", version_code=0)
        rejects("plugin.cfg mismatch", version="1.2.4")
        rejects("missing APK", apk=root / "missing.apk")
        rejects("non-PE dll", dll=apk)
        check(not (root / "out3").exists(), "output written for rejected input")
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("package.py self-test passed")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Assemble the Handview Godot addon release zip.")
    parser.add_argument("--dll", help="release GDExtension DLL (template_release)")
    parser.add_argument("--apk", help="signed release APK")
    parser.add_argument("--version", help="semantic version, e.g. 0.1.0 (no leading v)")
    parser.add_argument("--version-code", type=int, help="Android versionCode, written to handview.version")
    parser.add_argument("--out", help="output directory")
    parser.add_argument("--debug-dll", help="ship this DLL as the template_debug entry instead of the release DLL")
    parser.add_argument("--addon-dir", default=str(DEFAULT_ADDON_DIR),
                        help="Godot project holding addons/handview (default: repo godot-addon)")
    parser.add_argument("--self-test", action="store_true", help="build a zip from dummy files and verify it")
    args = parser.parse_args(argv)

    if args.self_test:
        self_test()
        return 0
    missing = [name for name in ("dll", "apk", "version", "version_code", "out") if getattr(args, name) is None]
    if missing:
        parser.error("missing: " + ", ".join("--" + name.replace("_", "-") for name in missing))
    try:
        zip_path, checksum_path = build_zip(
            args.addon_dir, args.dll, args.apk, args.version, args.version_code, args.out, args.debug_dll
        )
    except PackageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(zip_path)
    print(checksum_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
