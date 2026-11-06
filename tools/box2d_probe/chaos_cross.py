"""混沌写像の DLL (double の std::sin など) を、環境を変えて録画し、置いてある録画を全部再生して表にする。

    python chaos_cross.py <host の exe> <混沌写像の dll> <録画の置き場> <この機械の名前> [--no-record]

録画は FMA3 を切った host (pin) と、CRT の既定のまま (auto = MITIRU_FMA3=auto) の 2 通り。
置き場に別の PC の録画を置いてから流せば、CPU をまたいだ再生の表になる。
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

MODES = {"pin": None, "auto": "auto"}
FRAMES = 400


def host_run(host: Path, args: list[str], mode: str) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    env.pop("MITIRU_FMA3", None)
    if MODES[mode]:
        env["MITIRU_FMA3"] = MODES[mode]
    return subprocess.run([str(host), *args], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300, env=env)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("dll")
    ap.add_argument("recdir")
    ap.add_argument("tag")
    ap.add_argument("--no-record", action="store_true")
    a = ap.parse_args()
    host, dll, recdir = Path(a.host), Path(a.dll), Path(a.recdir)
    recdir.mkdir(parents=True, exist_ok=True)
    if not a.no_record:
        for mode in MODES:
            out = recdir / f"rec_{a.tag}_{mode}.mtrr"
            out.unlink(missing_ok=True)
            p = host_run(host, [str(dll), "--headless", "--record", str(out), "--max-frames", str(FRAMES)], mode)
            if p.returncode != 0 or not out.is_file():
                print(f"record {a.tag}/{mode} failed rc={p.returncode} {p.stderr[:200]}")
                return 1
    print(f"replayer={a.tag}")
    for rec in sorted(recdir.glob("rec_*.mtrr")):
        for mode in MODES:
            p = host_run(host, [str(dll), "--replay-test", str(rec), "--json"], mode)
            v = next((json.loads(l) for l in p.stdout.splitlines() if l.startswith("{") and '"verdict"' in l), {})
            at = f" diverged@{v['divergedAtFrame']}" if "divergedAtFrame" in v else ""
            print(f"  {rec.stem:16} replayed with {mode:4}: {v.get('verdict', 'no-verdict')} frames={v.get('framesCompared', '?')}{at}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
