"""box2d_probe を条件を変えて流し、条件ごとの hash を 1 行ずつ出す。

    python probe_matrix.py <box2d_probe_<版>_<構成>/box2d_probe.exe の親ディレクトリ>

出力は `<版> <構成> <条件> <final の hash> <時間>` の表。別の PC で同じ exe を流して表を diff する。
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPEAT = 3


def run(exe: Path, *args: str) -> tuple[str, list[str], float]:
    p = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=600)
    final = re.search(r"final ([0-9a-f]{16})", p.stdout)
    steps = re.findall(r"step\d+ ([0-9a-f]{16})", p.stdout)
    sec = re.search(r"time=([0-9.]+)s", p.stdout)
    if p.returncode != 0 or not final or not sec:
        raise RuntimeError(f"{exe} {args}: rc={p.returncode} {p.stderr[:200]}")
    return final.group(1), steps, float(sec.group(1))


def conditions(v32: bool) -> list[tuple[str, list[str]]]:
    conds = [("default", []), ("nudge", ["--nudge"]), ("bridge", ["--bridge"]), ("rows40", ["--rows", "40"])]
    if v32:
        conds += [("width4", ["--width", "4"]), ("bridge-width4", ["--bridge", "--width", "4"])]
        conds += [("rows40-width4", ["--rows", "40", "--width", "4"]), ("rows40-workers8", ["--rows", "40", "--workers", "8"])]
        for w in (4, 8):
            conds += [(f"workers{w}", ["--workers", str(w)]), (f"workers{w}-width4", ["--workers", str(w), "--width", "4"])]
    return conds


def main() -> int:
    root = Path(sys.argv[1])
    for ver in ("311", "320"):
        for cfg in ("Debug", "Release"):
            exe = root / f"box2d32_probe_{ver}_{cfg}" / "box2d_probe.exe"
            for name, args in conditions(ver == "320"):
                hashes, times = set(), []
                for _ in range(REPEAT):
                    final, _, sec = run(exe, *args)
                    hashes.add(final)
                    times.append(sec)
                shown = next(iter(hashes)) if len(hashes) == 1 else "UNSTABLE:" + ",".join(sorted(hashes))
                print(f"{ver} {cfg:7} {name:15} {shown} {min(times):.3f}s", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
