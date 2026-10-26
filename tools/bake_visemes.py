"""WAV から口の形の時刻表 (`<名前>.visemes.json`) を焼く。ゲームは実行中に音を解析せず、この表を時刻で引く (animation/VisemeTrack.hpp)。

    python tools/bake_visemes.py voice.wav -o voice.visemes.json [--hop 0.04] [--floor-db 35]

作り方: 40 ms ごとに、音量 (口の開き) と、LPC で求めた母音の響きの山 (F1、F2) を測る。F1 / F2 が日本語の 5 母音の代表値のどれに近いかで
aa ih ou ee oh へ配り、口の開きを掛ける。音声認識ではなく、母音の近さを見るだけなので、子音や小声は粗い。出来た表は手で直してよい。
numpy が要る。
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import wave
from pathlib import Path

import numpy as np

# 日本語の母音の F1, F2 (Hz) の代表値 (成人の平均に近い値)。実際の声はずれるので、距離は広めにとる
VOWELS = {"aa": (800.0, 1300.0), "ih": (300.0, 2300.0), "ou": (350.0, 1250.0), "ee": (500.0, 1900.0), "oh": (500.0, 850.0)}


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    if width != 2:
        raise SystemExit(f"{path}: 16 bit の PCM だけ読める (今は {width * 8} bit)")
    data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if channels > 1:
        data = data.reshape(-1, channels).mean(axis=1)
    return data, rate


def lpc(frame: np.ndarray, order: int) -> np.ndarray | None:
    """自己相関法 (Levinson-Durbin)。無音で解けなければ None"""
    n = len(frame)
    r = np.array([np.dot(frame[: n - k], frame[k:]) for k in range(order + 1)])
    if r[0] < 1e-9:
        return None
    a = np.zeros(order + 1)
    a[0] = 1.0
    err = r[0] * (1 + 1e-9)
    for i in range(1, order + 1):
        acc = r[i] + np.dot(a[1:i], r[i - 1:0:-1])
        k = -acc / err
        prev = a.copy()
        for j in range(1, i):
            a[j] = prev[j] + k * prev[i - j]
        a[i] = k
        err *= 1 - k * k
        if err <= 0:
            return None
    return a


def formants(frame: np.ndarray, rate: int) -> tuple[float, float] | None:
    order = min(18, 2 + rate // 1000)
    a = lpc(frame, order)
    if a is None:
        return None
    roots = [z for z in np.roots(a) if z.imag > 0.01]
    found = []
    for z in roots:
        freq = math.atan2(z.imag, z.real) * rate / (2 * math.pi)
        bandwidth = -math.log(max(abs(z), 1e-9)) * rate / math.pi
        if 200.0 < freq < 3500.0 and bandwidth < 500.0:
            found.append(freq)
    found.sort()
    return (found[0], found[1]) if len(found) >= 2 else None


def bake(data: np.ndarray, rate: int, hop: float, floor_db: float) -> list[dict]:
    win = int(rate * 0.032)
    step = max(1, int(rate * hop))
    emph = np.append(data[0], data[1:] - 0.97 * data[:-1])
    window = np.hamming(win)
    levels, shares = [], []
    for start in range(0, max(len(data) - win, 1), step):
        seg = data[start:start + win]
        if len(seg) < win:
            seg = np.pad(seg, (0, win - len(seg)))
        rms = math.sqrt(float(np.mean(seg * seg)))
        levels.append(20 * math.log10(max(rms, 1e-6)))
        f = formants(emph[start:start + win] * window if len(emph[start:start + win]) == win else seg * window, rate)
        if f is None:
            shares.append(None)
            continue
        scores = np.array([math.exp(-(((f[0] - v[0]) / 220.0) ** 2 + ((f[1] - v[1]) / 450.0) ** 2)) for v in VOWELS.values()])
        shares.append(scores / scores.sum() if scores.sum() > 1e-9 else None)
    peak = max(levels) if levels else 0.0
    keys = []
    for i, level in enumerate(levels):
        opening = min(max((level - (peak - floor_db)) / floor_db, 0.0), 1.0) ** 0.8
        share = shares[i]
        row = {"t": round(i * hop, 3)}
        if share is not None and opening > 0.02:
            for name, s in zip(VOWELS, share):
                w = round(float(opening * s), 3)
                if w >= 0.02:
                    row[name] = w
        keys.append(row)
    return smooth(keys)


def smooth(keys: list[dict]) -> list[dict]:
    """前後の 1 キーと 1:2:1 で混ぜて、口がちらつかないようにし、続く無音のキーは両端だけ残す"""
    names = list(VOWELS)
    out = []
    for i, k in enumerate(keys):
        row = {"t": k["t"]}
        for n in names:
            prev = keys[i - 1].get(n, 0.0) if i > 0 else 0.0
            nxt = keys[i + 1].get(n, 0.0) if i + 1 < len(keys) else 0.0
            w = round((prev + 2 * k.get(n, 0.0) + nxt) / 4, 3)
            if w >= 0.02:
                row[n] = w
        out.append(row)
    pruned = []
    for i, row in enumerate(out):
        silent = len(row) == 1
        if silent and 0 < i < len(out) - 1 and len(out[i - 1]) == 1 and len(out[i + 1]) == 1:
            continue
        pruned.append(row)
    return pruned


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--hop", type=float, default=0.04, help="キーの間隔 (秒)")
    ap.add_argument("--floor-db", type=float, default=35.0, help="最大の音量からこの dB 下を口の閉じ (0) とする")
    args = ap.parse_args(argv)
    data, rate = read_wav(args.wav)
    keys = bake(data, rate, args.hop, args.floor_db)
    args.out.write_text(json.dumps({"keys": keys}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"{args.out}: {len(keys)} keys, {len(data) / rate:.2f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
