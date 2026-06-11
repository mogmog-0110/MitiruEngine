#!/usr/bin/env python3
"""fetch_sdl2.py - SDL2 (VC devel) を external/sdl2/ へ取得する。

SDL2 は DualShock 4 等の DirectInput 系ゲームパッド対応に必須
(XInput だけでは箱コン系しか動かない)。CEF と同じ on-demand 方式:
リポジトリには同梱せず、このスクリプトで一度だけ取得する。

エンジンの CMake は external/sdl2/SDL2-*/cmake を自動検出するので、
実行後は普通に configure するだけで SDL2 backend が有効になる。

使い方:
    python tools/fetch_sdl2.py            # GitHub から取得
    python tools/fetch_sdl2.py --from <展開済み SDL2 ディレクトリ>   # ローカルコピー
"""

from __future__ import annotations

import argparse
import io
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

SDL2_VERSION = "2.30.9"
SDL2_URL = (
    "https://github.com/libsdl-org/SDL/releases/download/"
    f"release-{SDL2_VERSION}/SDL2-devel-{SDL2_VERSION}-VC.zip"
)


def normalize_include(sdl_dir: Path) -> None:
    """include/SDL2/SDL.h で include できるようヘッダを複製する。

    VC devel パッケージは include/ 直下にヘッダを置くが、エンジンは
    `<SDL2/SDL.h>` 形式で include する。ジャンクションではなく実コピーで
    そろえる (ジャンクションは copytree や zip 化を壊す)。
    """
    inc = sdl_dir / "include"
    sub = inc / "SDL2"
    if not inc.is_dir() or (sub / "SDL.h").is_file():
        return
    sub.mkdir(exist_ok=True)
    for h in inc.glob("*.h"):
        shutil.copy2(h, sub / h.name)
    print(f"[normalize] include/SDL2/ に {len(list(sub.glob('*.h')))} headers")


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    dest_root = repo_root / "external" / "sdl2"

    ap = argparse.ArgumentParser(description="SDL2 (VC devel) を external/sdl2/ へ取得する")
    ap.add_argument("--from", dest="local", type=Path, default=None,
                    help="ダウンロードせず、展開済み SDL2 ディレクトリ (SDL2-x.y.z) をコピーする")
    args = ap.parse_args()

    existing = sorted(dest_root.glob("SDL2-*/cmake"))
    if existing:
        print(f"[ok] already present: {existing[0].parent}")
        return 0

    dest_root.mkdir(parents=True, exist_ok=True)

    if args.local is not None:
        src = args.local.resolve()
        if not (src / "cmake").is_dir():
            raise SystemExit(f"--from {src} に cmake/ が無い (SDL2-devel VC 展開ディレクトリを指定)")
        dst = dest_root / src.name
        print(f"[copy] {src} -> {dst}")
        shutil.copytree(src, dst, ignore_dangling_symlinks=True,
                        ignore=shutil.ignore_patterns("SDL2"))  # 自己ジャンクション除け
        normalize_include(dst)
        print("[done] configure し直せば SDL2 backend が有効になります")
        return 0

    print(f"[fetch] {SDL2_URL}")
    with urllib.request.urlopen(SDL2_URL) as resp:
        data = resp.read()
    print(f"[fetch] {len(data) // 1024 // 1024} MB downloaded")
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        zf.extractall(dest_root)
    got = sorted(dest_root.glob("SDL2-*/cmake"))
    if not got:
        raise SystemExit("展開結果に SDL2-*/cmake が見つからない (パッケージ構造が変わった?)")
    normalize_include(got[0].parent)
    print(f"[done] {got[0].parent} — configure し直せば SDL2 backend が有効になります")
    return 0


if __name__ == "__main__":
    sys.exit(main())
