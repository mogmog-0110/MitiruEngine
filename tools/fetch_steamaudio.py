#!/usr/bin/env python3
"""fetch_steamaudio.py - Steam Audio SDK (HRTF) を external/steamaudio/ へ取得する。

Steam Audio (ValveSoftware/steam-audio、Apache-2.0) は -DMITIRU_WITH_STEAMAUDIO=ON のときだけ
使う opt-in の HRTF backend。公式の SDK zip は 170MB を超えるので、リポジトリにも公開
スナップショットにも入れず、使う人がこのスクリプトで一度だけ取得する (SDL2 / DXC と同じ方式)。

zip からは include/ と lib/ (各 OS のバイナリ) と LICENSE だけを取り出す。
実行後に -DMITIRU_WITH_STEAMAUDIO=ON で configure し直せば有効になる。

使い方:
    python tools/fetch_steamaudio.py              # GitHub Releases から取得
    python tools/fetch_steamaudio.py --zip <path> # 手元の steamaudio_<ver>.zip から展開
"""

from __future__ import annotations

import argparse
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

STEAMAUDIO_VERSION = "4.8.1"
STEAMAUDIO_URL = (
    "https://github.com/ValveSoftware/steam-audio/releases/download/"
    f"v{STEAMAUDIO_VERSION}/steamaudio_{STEAMAUDIO_VERSION}.zip"
)
KEEP_DIRS = ("include/", "lib/")


def find_prefix(names: list[str]) -> str:
    """zip 内で include/phonon.h の手前にある階層 (例 "steamaudio/") を返す。"""
    for n in names:
        if n.endswith("include/phonon.h"):
            return n[: -len("include/phonon.h")]
    raise SystemExit("zip に include/phonon.h が無い (パッケージ構造が変わった?)")


def wanted(rel: str) -> bool:
    return rel.startswith(KEEP_DIRS) or rel.upper().startswith("LICENSE")


def extract(zf: zipfile.ZipFile, dest: Path) -> int:
    names = zf.namelist()
    prefix = find_prefix(names)
    count = 0
    for name in names:
        if not name.startswith(prefix) or name.endswith("/"):
            continue
        rel = name[len(prefix):]
        if not wanted(rel):
            continue
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(zf.read(name))
        count += 1
    return count


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    dest = repo_root / "external" / "steamaudio"

    ap = argparse.ArgumentParser(description="Steam Audio SDK を external/steamaudio/ へ取得する")
    ap.add_argument("--zip", type=Path, default=None,
                    help="ダウンロードせず、手元の steamaudio_<ver>.zip を展開する")
    args = ap.parse_args()

    if (dest / "include" / "phonon.h").is_file():
        print(f"[ok] already present: {dest}")
        return 0

    if args.zip is not None:
        data = args.zip.read_bytes()
    else:
        print(f"[fetch] {STEAMAUDIO_URL}")
        with urllib.request.urlopen(STEAMAUDIO_URL) as resp:
            data = resp.read()
        print(f"[fetch] {len(data) // 1024 // 1024} MB downloaded")

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        n = extract(zf, dest)
    if not (dest / "include" / "phonon.h").is_file():
        raise SystemExit("展開後に include/phonon.h が見つからない")
    print(f"[done] {n} files -> {dest}")
    print("       -DMITIRU_WITH_STEAMAUDIO=ON で configure し直すと HRTF が有効になります")
    return 0


if __name__ == "__main__":
    sys.exit(main())
