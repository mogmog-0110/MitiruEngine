"""shaders/dawn/*.slang を WGSL にして shaders/dawn/generated/ へ書く。

    python tools/gen_dawn_shaders.py [--slangc <slangc.exe>] [--check]

--check は書かずに、いまある WGSL と作り直した物が同じかだけを見る (違えば 1 で終わる)。
slangc は引数、環境変数 SLANGC、PATH の順に探す。Slang の版は ADR 0087 の 10.5 (2026.20)。
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "shaders" / "dawn"
OUT = SRC / "generated"

# 変換する .slang。entry は各ファイルの [shader("...")] で決まる
SOURCES = ["forward", "post", "ui", "batch2d", "batch2dlit", "blit", "shadow", "lines", "bloom", "sky", "fxaa", "smaa", "particle", "viewblit", "oit", "impact", "skin", "skin_morph", "colorfinish"]


def find_slangc(arg):
    for c in (arg, os.environ.get("SLANGC"), shutil.which("slangc")):
        if c and Path(c).exists():
            return c
    sys.exit("slangc が見つからない (--slangc か SLANGC で渡す)")


def compile_one(slangc, name):
    # 標準出力へ出すと entry ごとに module が連なって Tint が型の重複で落とすので、必ずファイルへ書かせる
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / f"{name}.wgsl"
        cmd = [slangc, str(SRC / f"{name}.slang"), "-matrix-layout-column-major", "-target", "wgsl", "-o", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
        if r.returncode != 0 or not out.exists():
            sys.exit(f"{name}.slang: slangc が失敗\n{r.stderr}")
        text = out.read_text(encoding="utf-8").replace("\r\n", "\n")
        # Slang は flat を @interpolate(flat) (= first) で出すが、互換レベルは first を許さず either だけを通す。either は通常のレベルでも通る
        return text.replace("@interpolate(flat)", "@interpolate(flat, either)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slangc")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    slangc = find_slangc(args.slangc)
    OUT.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name in SOURCES:
        text = compile_one(slangc, name)
        target = OUT / f"{name}.wgsl"
        if args.check:
            have = target.read_text(encoding="utf-8").replace("\r\n", "\n") if target.exists() else ""
            if have != text:
                print(f"{target.name}: 作り直した物と違う")
                bad += 1
        else:
            target.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {target} ({len(text)} bytes)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
