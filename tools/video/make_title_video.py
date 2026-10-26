#!/usr/bin/env python3
"""examples/title_video の映像 (title.mpg) と音 (title_video.ogg) を作る。ffmpeg と Pillow と numpy が要る。

    python tools/video/make_title_video.py [--out examples/title_video/assets]

素材はこの script が全部描く (星、月、丘、流れ星、ロゴ)。ベタ塗りの形と大きな文字だけで描くのは、MPEG-1 がなめらかな
グラデーションと細かい文字を苦手とするため (docs/VIDEO.md)。音は同じ script が合成する (風の音、流れ星の風切り、ロゴの鐘)。
映像は 640x360、30 fps、8 秒。最後の絵を 1 枚足して書き出す (pl_mpeg は最後の 1 枚を返さない)。
"""

import argparse
import math
import random
import shutil
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, FPS, SECONDS = 640, 360, 30, 8
REPO = Path(__file__).resolve().parents[2]
BG = (11, 16, 32)
FONT_BLACK = REPO / "assets" / "fonts" / "MPLUSRounded1c-Black.ttf"
FONT_BOLD = REPO / "assets" / "fonts" / "MPLUSRounded1c-Bold.ttf"
LOGO = "MITIRU"
TAGLINE = "必要なものしか画面に出さない"


def ease_out(x: float) -> float:
    x = min(max(x, 0.0), 1.0)
    return 1.0 - (1.0 - x) ** 3


def make_stars(rng: random.Random):
    # (x, y, 半径, 速さ px/秒, 色)
    layers = [(70, 1, 6.0, (120, 130, 170)), (40, 2, 14.0, (190, 200, 235)), (14, 2, 30.0, (245, 247, 255))]
    stars = []
    for count, radius, speed, color in layers:
        for _ in range(count):
            stars.append((rng.uniform(0, W), rng.uniform(0, H * 0.7), radius, speed, color))
    return stars


def hill(draw, t: float, base: float, amp: float, wave_len: float, drift: float, color):
    points = [(0, H)]
    for x in range(0, W + 8, 8):
        y = base + amp * math.sin((x + t * drift) / wave_len) + amp * 0.4 * math.sin((x + t * drift) / (wave_len * 0.37))
        points.append((x, y))
    points.append((W, H))
    draw.polygon(points, fill=color)


def render(t: float, stars, logo_font, tag_font) -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    for x, y, r, speed, color in stars:
        sx = (x - speed * t) % W
        d.ellipse((sx - r, y - r, sx + r, y + r), fill=color)
    # 月は 8 秒かけて右の端を昇る (ロゴにかからない)
    mx, my = 592, 270 - 190 * t / SECONDS
    d.ellipse((mx - 34, my - 34, mx + 34, my + 34), fill=(236, 232, 208))
    d.ellipse((mx - 14, my - 22, mx + 16, my + 4), fill=(224, 220, 194))
    # 流れ星は 1.0 秒から 2.6 秒
    if 1.0 <= t <= 2.6:
        p = ease_out((t - 1.0) / 1.6)
        head = (-40 + p * (W + 120), 40 + p * 150)
        for k in range(10):
            f = k / 10.0
            tail = (head[0] - 36 * k, head[1] - 12 * k)
            r = max(1, int(4 * (1.0 - f)))
            shade = int(255 - 150 * f)
            d.ellipse((tail[0] - r, tail[1] - r, tail[0] + r, tail[1] + r), fill=(shade, shade, 255))
    hill(d, t, 302, 14, 70.0, 10.0, (18, 26, 52))
    hill(d, t, 330, 10, 45.0, 22.0, (9, 13, 28))
    # ロゴは 3.5 秒から 1 文字ずつ現れる
    letter_w = [logo_font.getlength(c) for c in LOGO]
    total = sum(letter_w)
    x = (W - total) / 2.0
    for i, c in enumerate(LOGO):
        p = ease_out((t - (3.5 + 0.18 * i)) / 0.5)
        if p > 0.0:
            offset = (1.0 - p) * 36.0
            d.text((x, 55 + offset), c, font=logo_font, fill=(242, 244, 255))
        x += letter_w[i]
    bar = ease_out((t - 4.9) / 0.7)
    if bar > 0.0:
        half = total / 2.0 * bar
        d.rectangle((W / 2 - half, 212, W / 2 + half, 218), fill=(78, 201, 176))
    tag = ease_out((t - 5.6) / 0.8)
    if tag > 0.0:
        mix = tuple(int(BG[k] + (c - BG[k]) * tag) for k, c in enumerate((200, 208, 230)))
        width = tag_font.getlength(TAGLINE)
        d.text(((W - width) / 2.0, 234), TAGLINE, font=tag_font, fill=mix)
    return img


def synth_audio(path: Path) -> None:
    rate = 44100
    n = rate * SECONDS
    t = np.arange(n) / rate
    rng = np.random.default_rng(7)
    out = np.zeros(n)
    # 低い和音を風のようにゆっくり満ちさせる
    swell = np.minimum(t / 2.5, 1.0) * np.clip((SECONDS - t) / 1.2, 0.0, 1.0)
    for freq, gain in ((110.0, 0.20), (164.81, 0.14), (220.0, 0.10), (261.63, 0.07)):
        out += gain * np.sin(2 * np.pi * freq * t + 0.4 * np.sin(2 * np.pi * 0.2 * t)) * swell
    # 流れ星の風切り: 1.0 秒から 2.6 秒の帯域ノイズ
    noise = rng.standard_normal(n)
    kernel = np.ones(24) / 24.0
    shaped = np.convolve(noise, kernel, mode="same")
    env = np.clip(1.0 - np.abs((t - 1.8) / 0.8), 0.0, 1.0) ** 2
    out += 0.35 * shaped * env
    # ロゴの鐘: 3.5 秒から。文字が現れる拍に 1 つずつ小さな音を重ねる
    for i, freq in enumerate((880.0, 1108.7, 1318.5, 1568.0, 1760.0, 2093.0)):
        start = 3.5 + 0.18 * i
        age = np.clip(t - start, 0.0, None)
        bell = np.where(t >= start, np.exp(-age * 3.2), 0.0)
        out += 0.10 * bell * (np.sin(2 * np.pi * freq * t) + 0.35 * np.sin(2 * np.pi * freq * 2.01 * t))
    out /= max(1.0, np.abs(out).max() / 0.85)
    pcm = (np.stack([out, out], axis=1) * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(REPO / "examples" / "title_video" / "assets"))
    args = ap.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        print("ffmpeg が見つからない (PATH に通してください)", file=sys.stderr)
        return 1
    out = Path(args.out)
    (out / "video").mkdir(parents=True, exist_ok=True)
    (out / "audio").mkdir(parents=True, exist_ok=True)
    stars = make_stars(random.Random(7))
    logo_font = ImageFont.truetype(str(FONT_BLACK), 128)
    tag_font = ImageFont.truetype(str(FONT_BOLD), 28)
    frames = FPS * SECONDS
    raw = bytearray()
    for i in range(frames):
        raw += render(i / FPS, stars, logo_font, tag_font).tobytes()
    raw += render((frames - 1) / FPS, stars, logo_font, tag_font).tobytes()   # 最後の絵をもう 1 枚
    mpg = out / "video" / "title.mpg"
    cmd = [ffmpeg, "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-framerate", str(FPS), "-i", "-",
           "-c:v", "mpeg1video", "-q:v", "3", "-g", "15", "-bf", "2", "-pix_fmt", "yuv420p", "-an", "-f", "mpeg", str(mpg)]
    code = subprocess.run(cmd, input=bytes(raw), check=False).returncode
    if code != 0:
        return code
    wav = out / "audio" / "_title_video.wav"
    synth_audio(wav)
    ogg = out / "audio" / "title_video.ogg"
    code = subprocess.run([ffmpeg, "-y", "-v", "error", "-i", str(wav), "-c:a", "libvorbis", "-q:a", "4", str(ogg)], check=False).returncode
    wav.unlink()
    print(f"wrote {mpg} ({mpg.stat().st_size} bytes) and {ogg} ({ogg.stat().st_size} bytes)")
    return code


if __name__ == "__main__":
    sys.exit(main())
