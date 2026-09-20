#!/usr/bin/env python3
"""Fetch the exact SDK revisions used to build Botmod, without global installs."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPENDENCIES = (
    ("metamod-source", "https://github.com/alliedmodders/metamod-source.git",
     "fa6f80e4662e5b96cc2e97722d812f374581dfd8"),
    ("hl2sdk-cs2", "https://github.com/alliedmodders/hl2sdk.git",
     "3b9adbdf39b4dead8d5d2307072cc47e9ba19112"),
)


def git(path, *args):
    subprocess.run(["git", "-C", str(path), *args], check=True)


def main():
    for name, url, revision in DEPENDENCIES:
        path = ROOT / ".deps" / name
        if not path.exists():
            path.mkdir(parents=True)
            git(path, "init")
            git(path, "remote", "add", "origin", url)
        current = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"],
                                 capture_output=True, text=True)
        if current.stdout.strip() != revision:
            dirty = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True)
            if dirty.strip():
                raise SystemExit(f"Refusing to overwrite changes in {path}")
            git(path, "fetch", "--depth", "1", "origin", revision)
            git(path, "checkout", "--detach", revision)
        if name == "metamod-source":
            # KHook is provided by Metamod at runtime; only its headers are needed.
            git(path, "submodule", "update", "--init", "--depth", "1", "third_party/khook")


if __name__ == "__main__":
    main()

