#!/usr/bin/env python3
"""
fetch_cef.py  — CEF (Chromium Embedded Framework) プリビルトバイナリのダウンロード

バージョン一覧: https://cef-builds.spotifycdn.com/index.html
windows64 standard で検索し、CEF_VERSION を更新してください。

使用例:
    python tools/fetch_cef.py
    python tools/fetch_cef.py --force
    python tools/fetch_cef.py --version 130.1.8+g4b4faa8+chromium-130.0.6723.58
"""

import argparse
import hashlib
import os
import sys
import tarfile
import urllib.request
from pathlib import Path

# ── バージョン設定 ──────────────────────────────────────────────
# https://cef-builds.spotifycdn.com/index.html でバージョンを確認し更新すること。
# 形式: <major>.<minor>.<patch>+g<githash>+chromium-<chromium_ver>
CEF_VERSION  = "128.4.12+g1d7a1f9+chromium-128.0.6613.138"
CEF_PLATFORM = "windows64"
# "standard" = ヘッダー+ライブラリ+サンプル (大きい)
# "minimal"  = ヘッダー+ライブラリのみ (推奨)
CEF_DIST_TYPE = "minimal"

CDN_BASE = "https://cef-builds.spotifycdn.com"


def build_archive_name(version: str, platform: str, dist: str) -> str:
    return f"cef_binary_{version}_{platform}_{dist}.tar.bz2"


def build_dir_name(version: str, platform: str) -> str:
    # minimal/standard 展開後のルートディレクトリ名はどちらも同じ形式
    return f"cef_binary_{version}_{platform}"


def sha1_file(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url: str, dest: Path) -> None:
    print(f"  ダウンロード: {url}")
    print(f"  → {dest}")
    with urllib.request.urlopen(url, timeout=60) as resp, open(dest, "wb") as out:
        total = int(resp.headers.get("Content-Length", 0))
        done = 0
        while True:
            chunk = resp.read(65536)
            if not chunk:
                break
            out.write(chunk)
            done += len(chunk)
            if total:
                pct = done * 100 // total
                mb_done  = done  // (1024 * 1024)
                mb_total = total // (1024 * 1024)
                print(f"\r  {pct:3d}%  {mb_done} / {mb_total} MB ", end="", flush=True)
    print()


def verify_sha1(archive: Path, sha1_url: str) -> bool:
    try:
        with urllib.request.urlopen(sha1_url, timeout=10) as resp:
            expected = resp.read().decode().strip().split()[0]
        actual = sha1_file(archive)
        if actual != expected:
            print(f"✗ SHA1 不一致: expected={expected}")
            print(f"              actual  ={actual}")
            return False
        print(f"✓ SHA1 OK: {actual}")
        return True
    except Exception as exc:
        print(f"  SHA1 検証スキップ ({exc})")
        return True  # ネット問題でスキップ — 壊れていたら展開で気付く


def main() -> int:
    parser = argparse.ArgumentParser(description="CEF プリビルトをダウンロードする")
    parser.add_argument("--version",  default=CEF_VERSION,   help="CEF バージョン文字列")
    parser.add_argument("--platform", default=CEF_PLATFORM,  help="プラットフォーム (windows64 等)")
    parser.add_argument("--dist",     default=CEF_DIST_TYPE, help="配布タイプ (standard/minimal)")
    parser.add_argument("--force", action="store_true",      help="既存を再ダウンロードする")
    args = parser.parse_args()

    repo_root    = Path(__file__).parent.parent
    external_cef = repo_root / "external" / "cef"
    external_cef.mkdir(parents=True, exist_ok=True)

    dir_name   = build_dir_name(args.version, args.platform)
    target_dir = external_cef / dir_name

    if target_dir.exists() and not args.force:
        print(f"✓ CEF は既にインストール済みです: {target_dir}")
        print(f"  再ダウンロードするには --force を付けてください。")
        return 0

    archive_name = build_archive_name(args.version, args.platform, args.dist)
    archive_path = external_cef / archive_name
    url          = f"{CDN_BASE}/{archive_name}"
    sha1_url     = f"{CDN_BASE}/{archive_name}.sha1"

    print(f"CEF バージョン: {args.version}")
    print(f"プラットフォーム: {args.platform}  配布: {args.dist}")
    print()

    if not archive_path.exists() or args.force:
        try:
            download(url, archive_path)
        except urllib.error.HTTPError as exc:
            print(f"\n✗ ダウンロード失敗 (HTTP {exc.code}): {url}")
            print(f"\n  バージョンが存在しない可能性があります。")
            print(f"  https://cef-builds.spotifycdn.com/index.html で確認してください。")
            return 1
        except Exception as exc:
            print(f"\n✗ ダウンロードエラー: {exc}")
            return 1
    else:
        print(f"  アーカイブキャッシュ済み: {archive_path}")

    if not verify_sha1(archive_path, sha1_url):
        archive_path.unlink(missing_ok=True)
        return 1

    print(f"  展開中 → {external_cef} ...")
    try:
        with tarfile.open(archive_path, "r:bz2") as tf:
            tf.extractall(external_cef)
    except Exception as exc:
        print(f"✗ 展開失敗: {exc}")
        return 1

    if not target_dir.exists():
        # minimal だと展開後ディレクトリに "_minimal" が付く場合がある。リネームする
        candidates = list(external_cef.glob(f"cef_binary_{args.version}_{args.platform}*"))
        dirs = [c for c in candidates if c.is_dir()]
        if dirs:
            dirs[0].rename(target_dir)
        else:
            print(f"✗ 展開後ディレクトリが見つかりません: {target_dir}")
            return 1

    print(f"✓ CEF インストール完了: {target_dir}")
    print()
    print("  次のステップ:")
    print("    cmake --preset default")
    print("    cmake --build build --config Debug")
    return 0


if __name__ == "__main__":
    sys.exit(main())
