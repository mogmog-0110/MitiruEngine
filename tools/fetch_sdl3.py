#!/usr/bin/env python3
"""fetch_sdl3.py - SDL3 (公式の VC 開発パッケージ) を external/sdl3/ へ置く。

SDL3 はパッドの唯一の経路 (Xbox / DualShock 4 / DualSense / Switch Pro、ADR 0048)。リポジトリには
同梱しない。Windows の configure は、無ければ自分で同じものを取りに行く (cmake/MitiruSdl3.cmake、
MITIRU_FETCH_SDL3)。このスクリプトは、configure の前に置いておきたい時と、ネットにつながらない機械で
展開済みのものを写す時 (--from) に使う。

版と SHA-256 は cmake/MitiruSdl3.cmake の MITIRU_SDL3_VERSION / MITIRU_SDL3_VC_SHA256 から読む。

使い方:
    python tools/fetch_sdl3.py                       # GitHub の公式リリースから取る
    python tools/fetch_sdl3.py --from <SDL3-x.y.z>   # 展開済みのディレクトリを写す
"""

from __future__ import annotations

import argparse
import hashlib
import io
import re
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PIN_FILE = REPO / "cmake" / "MitiruSdl3.cmake"
DEST_ROOT = REPO / "external" / "sdl3"


def read_pin() -> tuple[str, str]:
    text = PIN_FILE.read_text(encoding="utf-8")
    version = re.search(r'set\(MITIRU_SDL3_VERSION "([^"]+)"\)', text)
    sha = re.search(r'set\(MITIRU_SDL3_VC_SHA256 "([0-9a-f]{64})"\)', text)
    if not version or not sha:
        raise SystemExit(f"{PIN_FILE} から MITIRU_SDL3_VERSION / MITIRU_SDL3_VC_SHA256 を読めない")
    return version.group(1), sha.group(1)


def copy_local(src: Path, version: str) -> int:
    src = src.resolve()
    if not (src / "cmake" / "SDL3Config.cmake").is_file():
        raise SystemExit(f"--from {src} に cmake/SDL3Config.cmake が無い (SDL3-devel-*-VC.zip を展開したディレクトリを指定)")
    dst = DEST_ROOT / f"SDL3-{version}"
    print(f"[copy] {src} -> {dst}")
    shutil.copytree(src, dst)
    print("[done] configure し直せば SDL3 のパッドが有効になる")
    return 0


def download(version: str, sha256: str) -> int:
    url = ("https://github.com/libsdl-org/SDL/releases/download/"
           f"release-{version}/SDL3-devel-{version}-VC.zip")
    print(f"[fetch] {url}")
    with urllib.request.urlopen(url) as resp:
        data = resp.read()
    got = hashlib.sha256(data).hexdigest()
    if got != sha256:
        raise SystemExit(f"SHA-256 が合わない: {got} (期待 {sha256})。取得物を使わずに止める")
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        zf.extractall(DEST_ROOT)
    print(f"[done] {DEST_ROOT / f'SDL3-{version}'} ({len(data) // 1024 // 1024} MB)。configure し直せば SDL3 のパッドが有効になる")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="SDL3 (VC 開発パッケージ) を external/sdl3/ へ置く")
    ap.add_argument("--from", dest="local", type=Path, default=None,
                    help="ダウンロードせず、展開済みの SDL3 ディレクトリ (SDL3-x.y.z) を写す")
    args = ap.parse_args()

    version, sha256 = read_pin()
    if (DEST_ROOT / f"SDL3-{version}" / "cmake" / "SDL3Config.cmake").is_file():
        print(f"[ok] already present: {DEST_ROOT / f'SDL3-{version}'}")
        return 0
    DEST_ROOT.mkdir(parents=True, exist_ok=True)
    if args.local is not None:
        return copy_local(args.local, version)
    return download(version, sha256)


if __name__ == "__main__":
    sys.exit(main())
