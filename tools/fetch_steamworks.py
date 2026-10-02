#!/usr/bin/env python3
"""fetch_steamworks.py - Steamworks SDK を external/steamworks_sdk/ に置く手順を案内し、置けたかを確かめる。

Steamworks SDK は Steamworks のアカウントでログインしないと取れず、Valve の利用規約の下にあるので、
このスクリプトは取りに行かない (自動で落とせない)。利用者が partner.steamgames.com から
steamworks_sdk_<版>.zip を落とし、--zip で渡すと、ビルドに要る public/ と redistributable_bin/ だけを
展開する。SDK はリポジトリにも公開スナップショットにも入らない (.gitignore と release_allowlist で外してある)。

使い方:
    python tools/fetch_steamworks.py                 # 置き場所の確認と手順の表示
    python tools/fetch_steamworks.py --zip <path>    # 手元の steamworks_sdk_<版>.zip から展開する
展開した後で -DMITIRU_WITH_STEAMWORKS=ON で configure し直す。
"""

from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEST = REPO_ROOT / "external" / "steamworks_sdk"
DOWNLOAD_PAGE = "https://partner.steamgames.com/downloads/list"
KEEP_DIRS = ("public/", "redistributable_bin/")
REQUIRED = (
    "public/steam/steam_api.h",
    "redistributable_bin/win64/steam_api64.lib",
    "redistributable_bin/win64/steam_api64.dll",
)


def configure_stdout() -> None:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass


def find_prefix(names: list[str]) -> str:
    """zip 内で public/steam/steam_api.h の手前にある階層 (ふつうは "sdk/") を返す。"""
    for n in names:
        if n.endswith("public/steam/steam_api.h"):
            return n[: -len("public/steam/steam_api.h")]
    raise SystemExit("zip に public/steam/steam_api.h が無い (Steamworks SDK の zip か確かめる)")


def extract(zip_path: Path) -> int:
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        prefix = find_prefix(names)
        count = 0
        for name in names:
            if not name.startswith(prefix) or name.endswith("/"):
                continue
            rel = name[len(prefix):]
            if not rel.startswith(KEEP_DIRS):
                continue
            out = DEST / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(zf.read(name))
            count += 1
    return count


def missing_files() -> list[str]:
    return [r for r in REQUIRED if not (DEST / r).is_file()]


def print_guide() -> None:
    print("Steamworks SDK はログインが要るので、このスクリプトは取りに行かない。")
    print(f"  1. {DOWNLOAD_PAGE} から steamworks_sdk_<版>.zip (1.61 以降) を落とす")
    print("  2. python tools/fetch_steamworks.py --zip <落とした zip>")
    print("  3. cmake --preset default -DMITIRU_WITH_STEAMWORKS=ON")
    print(f"置き場所: {DEST}")


def main() -> int:
    configure_stdout()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zip", type=Path, help="手元の steamworks_sdk_<版>.zip")
    args = parser.parse_args()

    if args.zip:
        if not args.zip.is_file():
            print(f"zip が無い: {args.zip}", file=sys.stderr)
            return 2
        print(f"{extract(args.zip)} 個のファイルを {DEST} に置いた")

    missing = missing_files()
    if not missing:
        print(f"OK: Steamworks SDK は {DEST} にある。-DMITIRU_WITH_STEAMWORKS=ON で configure する")
        return 0
    print("Steamworks SDK がまだ揃っていない。足りないファイル:")
    for m in missing:
        print(f"  - {m}")
    print_guide()
    return 1


if __name__ == "__main__":
    sys.exit(main())
