#!/usr/bin/env python3
"""fetch_accesskit.py - AccessKit の C バインディング (accesskit-c) の事前ビルド済み配布から、Windows x64 MSVC の共有ライブラリを
external/accesskit/ へ取り出す。

-DMITIRU_WITH_ACCESSKIT=ON のときだけ要る (docs/ACCESSIBILITY.md、ADR 0117)。上流が GitHub Release に置く zip (約 68 MB、
全 OS と ABI の分を含む) を取り、タグに固定した SHA-256 を確かめてから、Windows x64 MSVC の共有ライブラリとヘッダー、
ライセンスだけを取り出す。Rust のツールチェーンは要らない。dll と lib は .gitignore で外れているので、リポジトリには
ヘッダーとライセンスと SOURCE.txt だけが入り、dll と lib はこの script で置く。

使い方:
    python tools/fetch_accesskit.py                   # GitHub の固定タグから取得
    python tools/fetch_accesskit.py --zip <file>      # 取得済みの zip から取り出す (SHA-256 は確かめる)
    python tools/fetch_accesskit.py --dest <dir>      # external/accesskit の代わりに別の場所へ置く (試験用)
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

TAG = "0.23.2"
COMMIT = "99023c87eca49531a8e1efc4657e35779257a463"
URL = f"https://github.com/AccessKit/accesskit-c/releases/download/{TAG}/accesskit-c-{TAG}.zip"
ZIP_SHA256 = "c7fa84f8f34fca909f662375737a54f98b17cf9eae51fffd94ab3d9f662e3681"
DLL_SHA256 = "c8e2b528a2f4bbc5ee8e091ae48d55b43156518a0105e48e94c64e101bf1c29b"

DLL_DIR = "lib/windows/x86_64/msvc/shared"
WANTED = (
    "include/accesskit.h",
    f"{DLL_DIR}/accesskit.dll",
    f"{DLL_DIR}/accesskit.lib",
    "LICENSE-MIT",
    "LICENSE-APACHE",
    "LICENSE.chromium",
    "AUTHORS",
    "README.md",
    "Cargo.lock",
    "accesskit.cmake",
    "accesskit-config.cmake",
)
REQUIRED = (f"{DLL_DIR}/accesskit.dll", f"{DLL_DIR}/accesskit.lib", "include/accesskit.h", "LICENSE-MIT")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def is_complete(dest: Path) -> bool:
    dll = dest / DLL_DIR / "accesskit.dll"
    return all((dest / r).is_file() for r in REQUIRED) and sha256_of(dll) == DLL_SHA256


def download(url: str, out: Path) -> None:
    print(f"[fetch] {url}")
    with urllib.request.urlopen(url, timeout=120) as response, out.open("wb") as f:  # noqa: S310 (固定の https URL)
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            f.write(block)


def member_map(archive: zipfile.ZipFile) -> dict[str, str]:
    """zip の中の名前 -> 取り出したい相対パス。全体が 1 つの最上位ディレクトリに入っていれば、それを外して比べる"""
    names = [n for n in archive.namelist() if not n.endswith("/")]
    tops = {n.split("/", 1)[0] for n in names if "/" in n}
    prefix = f"{next(iter(tops))}/" if len(tops) == 1 and all("/" in n for n in names) else ""
    wanted = set(WANTED)
    return {n: n[len(prefix):] for n in names if n.startswith(prefix) and n[len(prefix):] in wanted}


def extract(zip_path: Path, dest: Path) -> None:
    if sha256_of(zip_path) != ZIP_SHA256:
        raise SystemExit(f"zip の SHA-256 が固定した値と違う: {sha256_of(zip_path)} (期待 {ZIP_SHA256})")
    with zipfile.ZipFile(zip_path) as archive:
        members = member_map(archive)
        missing = [r for r in REQUIRED if r not in members.values()]
        if missing:
            raise SystemExit(f"zip に必要なファイルが無い: {missing}")
        for name, rel in sorted(members.items(), key=lambda kv: kv[1]):
            target = dest / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name))
    if sha256_of(dest / DLL_DIR / "accesskit.dll") != DLL_SHA256:
        raise SystemExit("取り出した accesskit.dll の SHA-256 が固定した値と違う")


def main() -> int:
    ap = argparse.ArgumentParser(description="AccessKit の事前ビルド済みライブラリを external/accesskit/ へ取り出す")
    ap.add_argument("--zip", type=Path, help="取得済みの accesskit-c-%s.zip" % TAG)
    ap.add_argument("--dest", type=Path, help="置き場 (既定は external/accesskit)")
    args = ap.parse_args()

    dest = args.dest or Path(__file__).resolve().parents[1] / "external" / "accesskit"
    if is_complete(dest):
        print(f"[ok] already present: {dest}")
        return 0
    if args.zip is not None:
        extract(args.zip, dest)
    else:
        with tempfile.TemporaryDirectory(prefix="mitiru_accesskit_") as tmp:
            archive = Path(tmp) / f"accesskit-c-{TAG}.zip"
            download(URL, archive)
            extract(archive, dest)
    print(f"[done] {dest} (accesskit-c {TAG}, {COMMIT[:12]}): -DMITIRU_WITH_ACCESSKIT=ON で configure し直すと有効になります")
    return 0


if __name__ == "__main__":
    sys.exit(main())
