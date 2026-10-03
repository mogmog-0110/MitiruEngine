#!/usr/bin/env python3
"""RML の <img src="glyph:..."/> が使う、キーとパッドのボタンの絵 (assets/glyphs/*.png) を描く。

絵柄の名前は include/mitiru/input/PadGlyphs.hpp の inputGlyph が返すもの ("xbox_a"、"ps_cross"、
"nintendo_b"、"key_space"、"mouse_left"、"dpad_up"、"axis_leftsticky_neg" など) と同じにする。
絵はこのスクリプトが図形と同梱の書体 (M PLUS Rounded 1c Bold、SIL OFL 1.1) で描くので、外から持ち込んだ
素材は入っていない。描いた PNG は CC0 で配る (assets/glyphs/LICENSE.txt)。

使い方:
    python tools/gen_input_glyphs.py            # assets/glyphs/ に書く
    python tools/gen_input_glyphs.py --check    # 書いてある PNG の名前の組が最新か調べる (差があれば 1)
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "assets" / "glyphs"
FONT = REPO / "assets" / "fonts" / "MPLUSRounded1c-Bold.ttf"
SIZE = 64
SCALE = 4  # 4 倍で描いて縮め、縁を滑らかにする
S = SIZE * SCALE

DARK = (43, 43, 46, 255)
RING = (90, 90, 96, 255)
LIGHT = (242, 242, 242, 255)
KEY_EDGE = (110, 110, 116, 255)
KEY_SHADE = (196, 196, 202, 255)
INK = (34, 34, 38, 255)
ACCENT = (229, 72, 77, 255)

XBOX_FACE = {"a": (106, 191, 75), "b": (226, 74, 59), "x": (58, 142, 230), "y": (242, 193, 46)}

NAMED_KEYS = {
    "back": "BS", "tab": "Tab", "enter": "Enter", "shift": "Shift", "ctrl": "Ctrl", "alt": "Alt",
    "pause": "Pause", "capslock": "Caps", "escape": "Esc", "space": "Space", "pageup": "PgUp",
    "pagedown": "PgDn", "end": "End", "home": "Home", "insert": "Ins", "delete": "Del",
    "lshift": "Shift", "rshift": "Shift", "lctrl": "Ctrl", "rctrl": "Ctrl", "lalt": "Alt", "ralt": "Alt",
    "semicolon": ";", "comma": ",", "minus": "-", "period": ".", "slash": "/", "backquote": "`",
}
ARROW_KEYS = {"up": 0, "right": 90, "down": 180, "left": 270}


def font(px: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONT), px * SCALE)


def canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def text_center(d: ImageDraw.ImageDraw, cx: float, cy: float, text: str, px: int, fill, max_w: float) -> None:
    while px > 8:
        f = font(px)
        box = d.textbbox((0, 0), text, font=f)
        if box[2] - box[0] <= max_w:
            break
        px -= 1
    d.text((cx - (box[0] + box[2]) / 2, cy - (box[1] + box[3]) / 2), text, font=f, fill=fill)


def arrow(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, angle: int, fill) -> None:
    a = math.radians(angle)
    tip = (cx + r * math.sin(a), cy - r * math.cos(a))
    left = (cx + r * 0.8 * math.sin(a - 2.4), cy - r * 0.8 * math.cos(a - 2.4))
    right = (cx + r * 0.8 * math.sin(a + 2.4), cy - r * 0.8 * math.cos(a + 2.4))
    d.polygon([tip, left, right], fill=fill)


def keycap(label: str | None, angle: int | None = None) -> Image.Image:
    img, d = canvas()
    m = 4 * SCALE
    d.rounded_rectangle((m, m + 2 * SCALE, S - m, S - m), radius=10 * SCALE, fill=KEY_SHADE, outline=KEY_EDGE, width=2 * SCALE)
    d.rounded_rectangle((m, m, S - m, S - m - 5 * SCALE), radius=10 * SCALE, fill=LIGHT, outline=KEY_EDGE, width=2 * SCALE)
    cy = (m + S - m - 5 * SCALE) / 2
    if angle is not None:
        arrow(d, S / 2, cy, 16 * SCALE, angle, INK)
    else:
        text_center(d, S / 2, cy, label, 30 if len(label) <= 2 else 20, INK, S - 2 * m - 8 * SCALE)
    return img


def face(label: str | None, color, shape: str | None = None) -> Image.Image:
    img, d = canvas()
    m = 4 * SCALE
    d.ellipse((m, m, S - m, S - m), fill=DARK, outline=RING, width=2 * SCALE)
    c, r, w = S / 2, 15 * SCALE, 5 * SCALE
    if shape == "cross":
        d.line((c - r, c - r, c + r, c + r), fill=color, width=w)
        d.line((c - r, c + r, c + r, c - r), fill=color, width=w)
    elif shape == "circle":
        d.ellipse((c - r, c - r, c + r, c + r), outline=color, width=w)
    elif shape == "square":
        d.rectangle((c - r * 0.9, c - r * 0.9, c + r * 0.9, c + r * 0.9), outline=color, width=w)
    elif shape == "triangle":
        d.polygon([(c, c - r * 1.05), (c - r * 1.05, c + r * 0.8), (c + r * 1.05, c + r * 0.8)], outline=color, width=w)
    else:
        text_center(d, c, c, label, 34, color, S - 2 * m)
    return img


def shoulder(label: str, trigger: bool) -> Image.Image:
    img, d = canvas()
    top = 10 * SCALE if trigger else 16 * SCALE
    radius_top = 22 * SCALE if trigger else 10 * SCALE
    d.rounded_rectangle((4 * SCALE, top, S - 4 * SCALE, S - 12 * SCALE), radius=radius_top, fill=DARK, outline=RING, width=2 * SCALE)
    text_center(d, S / 2, (top + S - 12 * SCALE) / 2, label, 24, LIGHT, S - 14 * SCALE)
    return img


def pill(label: str) -> Image.Image:
    img, d = canvas()
    d.rounded_rectangle((4 * SCALE, 18 * SCALE, S - 4 * SCALE, S - 18 * SCALE), radius=14 * SCALE, fill=DARK, outline=RING, width=2 * SCALE)
    text_center(d, S / 2, S / 2, label, 22, LIGHT, S - 16 * SCALE)
    return img


def stick(label: str, arrows: list[int]) -> Image.Image:
    img, d = canvas()
    m = 4 * SCALE
    d.ellipse((m, m, S - m, S - m), fill=DARK, outline=RING, width=2 * SCALE)
    inner = 14 * SCALE
    d.ellipse((inner, inner, S - inner, S - inner), outline=RING, width=2 * SCALE)
    text_center(d, S / 2, S / 2, label, 20, LIGHT, S - 2 * inner)
    for a in arrows:
        r = (S / 2 - m) * 0.82
        arrow(d, S / 2 + r * math.sin(math.radians(a)) * 0.92, S / 2 - r * math.cos(math.radians(a)) * 0.92, 6 * SCALE, a, ACCENT)
    return img


def dpad(direction: int) -> Image.Image:
    img, d = canvas()
    w = 20 * SCALE
    c = S / 2
    arm = S / 2 - 4 * SCALE
    d.rounded_rectangle((c - w / 2, c - arm, c + w / 2, c + arm), radius=4 * SCALE, fill=DARK, outline=RING, width=2 * SCALE)
    d.rounded_rectangle((c - arm, c - w / 2, c + arm, c + w / 2), radius=4 * SCALE, fill=DARK, outline=RING, width=2 * SCALE)
    d.rectangle((c - w / 2 + 2 * SCALE, c - w / 2 + 2 * SCALE, c + w / 2 - 2 * SCALE, c + w / 2 - 2 * SCALE), fill=DARK)
    r = arm - w / 2
    arrow(d, c + r * math.sin(math.radians(direction)), c - r * math.cos(math.radians(direction)), 8 * SCALE, direction, LIGHT)
    return img


def mouse(button: str) -> Image.Image:
    img, d = canvas()
    l, t, r, b = 14 * SCALE, 4 * SCALE, S - 14 * SCALE, S - 4 * SCALE
    mid, split = (l + r) / 2, t + 24 * SCALE
    d.rounded_rectangle((l, t, r, b), radius=18 * SCALE, fill=LIGHT, outline=KEY_EDGE, width=2 * SCALE)
    if button in ("left", "right"):
        # ボタンの面だけを塗るため、本体の形の型で切り抜いた色を重ねる
        body = Image.new("L", (S, S), 0)
        ImageDraw.Draw(body).rounded_rectangle((l, t, r, b), radius=18 * SCALE, fill=255)
        half = Image.new("L", (S, S), 0)
        ImageDraw.Draw(half).rectangle((l, t, mid, split) if button == "left" else (mid, t, r, split), fill=255)
        mask = ImageChops.darker(body, half)
        img.paste(Image.new("RGBA", (S, S), ACCENT), (0, 0), mask)
        d.rounded_rectangle((l, t, r, b), radius=18 * SCALE, outline=KEY_EDGE, width=2 * SCALE)
    d.line((mid, t, mid, split), fill=KEY_EDGE, width=2 * SCALE)
    d.line((l, split, r, split), fill=KEY_EDGE, width=2 * SCALE)
    wheel_fill = ACCENT if button == "middle" else KEY_EDGE
    d.rounded_rectangle((mid - 3 * SCALE, t + 8 * SCALE, mid + 3 * SCALE, t + 18 * SCALE), radius=3 * SCALE, fill=wheel_fill)
    if button in ("x1", "x2"):
        text_center(d, mid, (split + b) / 2, button.upper(), 18, ACCENT, r - l - 6 * SCALE)
    return img


def none() -> Image.Image:
    return Image.new("RGBA", (S, S), (0, 0, 0, 0))


def pad_glyphs() -> dict[str, Image.Image]:
    g: dict[str, Image.Image] = {}
    for k, rgb in XBOX_FACE.items():
        g[f"xbox_{k}"] = face(k.upper(), rgb + (255,))
    g["ps_cross"] = face(None, (124, 167, 230, 255), "cross")
    g["ps_circle"] = face(None, (232, 96, 106, 255), "circle")
    g["ps_square"] = face(None, (217, 142, 199, 255), "square")
    g["ps_triangle"] = face(None, (78, 201, 165, 255), "triangle")
    for k in "abxy":
        g[f"nintendo_{k}"] = face(k.upper(), LIGHT)
    for name, label in (("xbox_lb", "LB"), ("xbox_rb", "RB"), ("ps_l1", "L1"), ("ps_r1", "R1"),
                        ("nintendo_l", "L"), ("nintendo_r", "R")):
        g[name] = shoulder(label, trigger=False)
    for name, label in (("xbox_lt", "LT"), ("xbox_rt", "RT"), ("ps_l2", "L2"), ("ps_r2", "R2"),
                        ("nintendo_zl", "ZL"), ("nintendo_zr", "ZR")):
        g[name] = shoulder(label, trigger=True)
    for name, label in (("xbox_menu", "MENU"), ("xbox_view", "VIEW"), ("ps_options", "OPT"),
                        ("ps_create", "CRT"), ("nintendo_plus", "+"), ("nintendo_minus", "−")):
        g[name] = pill(label)
    for name, label in (("xbox_ls", "LS"), ("xbox_rs", "RS"), ("ps_l3", "L3"), ("ps_r3", "R3"),
                        ("nintendo_ls", "LS"), ("nintendo_rs", "RS")):
        g[name] = stick(label, [])
    for name, a in (("up", 0), ("right", 90), ("down", 180), ("left", 270)):
        g[f"dpad_{name}"] = dpad(a)
    for side, label in (("left", "L"), ("right", "R")):
        g[f"axis_{side}stickx"] = stick(label, [90, 270])
        g[f"axis_{side}stickx_pos"] = stick(label, [90])
        g[f"axis_{side}stickx_neg"] = stick(label, [270])
        g[f"axis_{side}sticky"] = stick(label, [0, 180])
        g[f"axis_{side}sticky_pos"] = stick(label, [0])
        g[f"axis_{side}sticky_neg"] = stick(label, [180])
    return g


def key_glyphs() -> dict[str, Image.Image]:
    g: dict[str, Image.Image] = {}
    for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
        g[f"key_{c.lower()}"] = keycap(c)
    for i in range(10):
        g[f"key_num{i}"] = keycap(f"N{i}")
    for i in range(1, 13):
        g[f"key_f{i}"] = keycap(f"F{i}")
    for name, label in NAMED_KEYS.items():
        g[f"key_{name}"] = keycap(label)
    for name, angle in ARROW_KEYS.items():
        g[f"key_{name}"] = keycap(None, angle)
    for b in ("left", "right", "middle", "x1", "x2"):
        g[f"mouse_{b}"] = mouse(b)
    return g


def all_glyphs() -> dict[str, Image.Image]:
    g = {"none": none()}
    g.update(pad_glyphs())
    g.update(key_glyphs())
    return g


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="PNG の名前の組が最新か調べるだけで書かない")
    args = ap.parse_args()
    glyphs = all_glyphs()
    if args.check:
        have = {p.stem for p in OUT.glob("*.png")}
        missing = sorted(set(glyphs) - have)
        extra = sorted(have - set(glyphs))
        if missing or extra:
            print(f"assets/glyphs is out of date: missing {missing} extra {extra}", file=sys.stderr)
            return 1
        return 0
    OUT.mkdir(parents=True, exist_ok=True)
    for name, img in sorted(glyphs.items()):
        small = img.resize((SIZE, SIZE), Image.LANCZOS)
        # 色数を減らした palette PNG にして、配布物を小さくする (64 色で縁の滑らかさは残る)
        small.quantize(colors=64, method=Image.Quantize.FASTOCTREE).save(OUT / f"{name}.png", optimize=True)
    print(f"wrote {len(glyphs)} glyphs to {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
