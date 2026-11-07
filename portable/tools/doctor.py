#!/usr/bin/env python3
"""Read-only PGR1 portable prerequisites check. Never handles game bytes beyond SHA-1."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

RELEASES = {
    "4878c3bdbd44663b64ce64986b313e8c6b7f8917": "Europe 1.1",
    "ba7763e6e946ab40aed6706f2c9bc1df3a990fa0": "Europe/Australia 1.0",
    "0d55fff29ceb34ab4dfcfc4820e823d967ca1469": "USA 1.0",
}


def git(root: Path, *args: str) -> str | None:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=root, stderr=subprocess.DEVNULL, text=True, timeout=8
        ).strip()
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None


def sha1(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="PGR1 Portable environment doctor")
    parser.add_argument("--require-sdk", action="store_true")
    parser.add_argument("--require-assets", action="store_true")
    parser.add_argument("--require-linux", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    problems: list[str] = []

    def line(label: str, value: str) -> None:
        print(f"  {label:<19} {value}")

    print("PGR1 Portable — environment check (read-only)")
    line("Repository", str(root))
    line("System", f"{platform.system()} / {platform.machine()}")
    branch = git(root, "branch", "--show-current")
    line("Git branch", branch or "not a git checkout / detached HEAD")
    line("Git revision", (git(root, "rev-parse", "--short", "HEAD") or "unknown"))
    if branch == "main":
        problems.append("Current branch is main: switch to portable before editing.")
    if args.require_linux and platform.system() != "Linux":
        problems.append("Full Linux harness requires a Linux environment (not native PowerShell/Windows).")

    for command in ("git", "cmake", "ninja", "clang", "clang++", "python3", "ccache"):
        found = shutil.which(command)
        line(command, "OK" if found else ("optional" if command == "ccache" else "missing"))
        if args.require_sdk and command in ("git", "cmake", "ninja", "clang", "clang++") and not found:
            problems.append(f"Missing required program: {command}")

    sdk = root / "sdk" / "cmake" / "XbeRecomp.cmake"
    line("SDK checkout", "present" if sdk.is_file() else "missing (run git submodule update --init)")
    submodule = git(root, "submodule", "status", "sdk")
    if submodule:
        line("SDK revision", submodule[:80])
    if args.require_sdk and not sdk.is_file():
        problems.append("XboxXBERecomp SDK not checked out.")

    xbe = root / "assets" / "default.xbe"
    if xbe.is_file():
        digest = sha1(xbe)
        name = RELEASES.get(digest)
        line("default.xbe", name or "unsupported release")
        line("XBE SHA-1", digest)
        if args.require_assets and name is None:
            problems.append("Unsupported default.xbe SHA-1; use one of the three supported disc versions.")
    else:
        line("default.xbe", "missing (host API tests do not require it)")
        if args.require_assets:
            problems.append("Place the original default.xbe in assets/ (never commit game files).")

    generated = root / "generated" / "game" / "x86_func_mapping.cpp"
    line("Generated game", "present" if generated.is_file() else "not generated")
    dirty = git(root, "status", "--porcelain")
    line("Working tree", "changed" if dirty else ("clean" if dirty == "" else "unknown"))

    print("\n" + ("RESULT: prerequisites OK" if not problems else "RESULT: action required"))
    for item in problems:
        print("  - " + item)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
