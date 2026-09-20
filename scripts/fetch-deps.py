#!/usr/bin/env python3
"""Fetch the exact SDK revisions used to build Botmod, without global installs."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEPENDENCIES = (
    ("metamod-source-api17", "https://github.com/alliedmodders/metamod-source.git",
     "7ec0f16948ab3a0910a98b4ac10e1c0e360d5339"),
    ("hl2sdk-cs2", "https://github.com/alliedmodders/hl2sdk.git",
     "3b9adbdf39b4dead8d5d2307072cc47e9ba19112"),
    ("safetyhook", "https://github.com/cursey/safetyhook.git",
     "302d409419bf6c64b142093f44a32bc274865609"),
    ("zydis", "https://github.com/zyantific/zydis.git",
     "569320ad3c4856da13b9dbf1f0d9e20bda63870e"),
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
        if name == "zydis":
            git(path, "submodule", "update", "--init", "--recursive", "--depth", "1", "dependencies/zycore")


if __name__ == "__main__":
    main()
