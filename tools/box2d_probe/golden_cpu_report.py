"""同梱の golden を --replay-test と --synctest で流し、機械ごとに比べられる 1 枚の報告を書く。

    python golden_cpu_report.py <host の exe> <golden のディレクトリ> [--extra name=dll=mtrr ...]

同じ exe と同じ .mtrr を別の PC で流し、出力の hash (最終 state の sha256) を並べて差を見る。
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import winreg
from pathlib import Path

GOLDENS = [
    ("rewind", "rewind/rewind.dll", "rewind_small.mtrr"),
    ("beko_run", "beko_run/beko_run.dll", "beko_run_small.mtrr"),
    ("neon_asteroids", "neon_asteroids/neon_asteroids.dll", "neon_asteroids_small.mtrr"),
    ("objects_kitchen", "objects_kitchen/objects_kitchen.dll", "objects_kitchen_small.mtrr"),
]


def cpu_name() -> str:
    key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
    return winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()


def feature(code: int) -> bool:
    return bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(code))


def run(cmd: list[str], env: dict[str, str], timeout: int = 300) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, env=env)


def replay_test(host: Path, dll: Path, mtrr: Path, env: dict[str, str]) -> dict:
    p = run([str(host), str(dll), "--replay-test", str(mtrr), "--json", "--verbose"], env)
    verdict = {}
    for line in p.stdout.splitlines():
        if line.startswith('{"framesCompared"'):
            verdict = json.loads(line)
    final = next((l for l in p.stdout.splitlines() if l.startswith("replay final:")), "")
    envline = next((l for l in p.stderr.splitlines() + p.stdout.splitlines() if "数学関数の環境" in l), "")
    return {
        "rc": p.returncode,
        "verdict": verdict.get("verdict"),
        "frames": verdict.get("framesCompared"),
        "finalSha256": hashlib.sha256(final.encode("utf-8")).hexdigest()[:16],
        "env": envline.strip(),
    }


def synctest(host: Path, dll: Path, frames: int, script: Path | None, env: dict[str, str]) -> dict:
    cmd = [str(host), str(dll), "--headless", "--synctest", "1", "--max-frames", str(frames)]
    if script and script.is_file():
        cmd += ["--input-script", str(script)]
    p = run(cmd, env)
    bad = re.search(r"diverge|desync|非決定|不一致", p.stdout + p.stderr)
    return {"rc": p.returncode, "frames": frames, "divergedMessage": bool(bad)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("golden_dir")
    ap.add_argument("--fma3", default="", help="MITIRU_FMA3 に渡す値 (auto で固定を外す)")
    ap.add_argument("--synctest-frames", type=int, default=300)
    ap.add_argument("--extra", action="append", default=[], help="name=dll=mtrr")
    a = ap.parse_args()
    host = Path(a.host).resolve()
    env = dict(os.environ)
    if a.fma3:
        env["MITIRU_FMA3"] = a.fma3
    out = {
        "machine": platform.node(),
        "cpu": cpu_name(),
        "avx2": feature(40),
        "avx512f": feature(41),
        "windows": platform.version(),
        "fma3Env": a.fma3 or "(host 既定 = 切る)",
        "games": {},
    }
    script = Path(a.golden_dir).parent / "e2e" / "input_scripts" / "rewind_basic.txt"
    items = [(n, d, Path(a.golden_dir) / m) for n, d, m in GOLDENS]
    for e in a.extra:
        n, d, m = e.split("=", 2)
        items.append((n, d, Path(m)))
    for name, dll, mtrr in items:
        dllp = host.parent / dll if not Path(dll).is_absolute() else Path(dll)
        out["games"][name] = {
            "replayTest": replay_test(host, dllp, mtrr, env),
            "synctest": synctest(host, dllp, a.synctest_frames, script if name == "rewind" else None, env),
        }
    print(json.dumps(out, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
