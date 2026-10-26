"""3D LUT (.cube) を、エンジンが読む「幅 N*N・高さ N」の PNG に並べ替える。恒等 LUT も作れる。

    python tools/cube_to_lut_png.py in.cube out.png [--size N]
    python tools/cube_to_lut_png.py --identity N out.png
    python tools/cube_to_lut_png.py --preset warm|mono|teal-orange|swap [--size N] out.png

並べ方 (include/mitiru/render/ColorFinishMath.hpp の lutTexel と同じ): 入力 (r, g, b) の出力は画素 (x = b*N + r, y = g)。
.cube は赤が最も速く変わる (r、次に g、最後に b)。PNG の値は sRGB 符号化のまま 8 bit に丸める。
--size を .cube の一辺と変えると、トライリニアに引き直す。標準ライブラリだけで動く (PNG は zlib で自前に書く)。
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path


def parse_cube(text):
    """(size, table, lo, hi) を返す。table[(b * n + g) * n + r] = (r, g, b) の出力 (0..1 に丸める)。
    DOMAIN_MIN / MAX は入力の範囲で、出力の値は変えない (resample が入力の側を 0..1 へ直す)。"""
    size = 0
    lo = (0.0, 0.0, 0.0)
    hi = (1.0, 1.0, 1.0)
    rows = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        head = line.split()
        key = head[0].upper()
        if key == "LUT_3D_SIZE":
            size = int(head[1])
        elif key == "DOMAIN_MIN":
            lo = tuple(float(v) for v in head[1:4])
        elif key == "DOMAIN_MAX":
            hi = tuple(float(v) for v in head[1:4])
        elif key in ("TITLE", "LUT_1D_SIZE", "LUT_1D_INPUT_RANGE", "LUT_3D_INPUT_RANGE"):
            if key == "LUT_1D_SIZE":
                raise ValueError("1D LUT には対応しない (LUT_3D_SIZE の .cube を渡す)")
        else:
            rows.append(tuple(float(v) for v in head[:3]))
    if size < 2:
        raise ValueError("LUT_3D_SIZE が無い、または 2 未満")
    if len(rows) != size**3:
        raise ValueError(f"データ行が {len(rows)} 行で、{size}^3 = {size**3} 行と合わない")
    table = [tuple(min(max(v, 0.0), 1.0) for v in r) for r in rows]
    return size, table, lo, hi


def identity_table(n):
    return [(r / (n - 1), g / (n - 1), b / (n - 1)) for b in range(n) for g in range(n) for r in range(n)]


def resample(size, table, n, lo=(0.0, 0.0, 0.0), hi=(1.0, 1.0, 1.0)):
    """一辺 size の表を、一辺 n の格子でトライリニアに引き直す。表の入力は lo..hi で、出力の格子は 0..1 の入力に当てる (範囲の外は端の値)。"""
    if size == n and tuple(lo) == (0.0, 0.0, 0.0) and tuple(hi) == (1.0, 1.0, 1.0):
        return table
    out = []
    top = size - 1
    for b in range(n):
        for g in range(n):
            for r in range(n):
                pos = [min(max((c / (n - 1) - l) / ((h - l) if h != l else 1.0), 0.0), 1.0) * top
                       for c, l, h in zip((r, g, b), lo, hi)]
                base = [min(int(p), top - 1) for p in pos]
                f = [p - c0 for p, c0 in zip(pos, base)]
                acc = [0.0, 0.0, 0.0]
                for corner in range(8):
                    d = (corner & 1, (corner >> 1) & 1, (corner >> 2) & 1)
                    w = 1.0
                    for k in range(3):
                        w *= f[k] if d[k] else 1.0 - f[k]
                    t = table[((base[2] + d[2]) * size + base[1] + d[1]) * size + base[0] + d[0]]
                    for k in range(3):
                        acc[k] += t[k] * w
                out.append(tuple(acc))
    return out


def _luma(r, g, b):
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def _warm(r, g, b):
    return (r * 1.08 + 0.02, g * 1.0, b * 0.86)


def _mono(r, g, b):
    y = _luma(r, g, b)
    return (y * 0.96, y * 1.0, y * 1.06)


def _teal_orange(r, g, b):
    y = _luma(r, g, b)
    shadow = 1.0 - y
    return (r + 0.10 * y - 0.06 * shadow, g + 0.01 * y + 0.03 * shadow, b - 0.10 * y + 0.08 * shadow)


def _swap(r, g, b):
    """軸の向きの確認用。出力 (r, g, b) を入力 (g, b, r) にする。"""
    return (g, b, r)


PRESETS = {"warm": _warm, "mono": _mono, "teal-orange": _teal_orange, "swap": _swap}


def preset_table(name, n):
    fn = PRESETS[name]
    out = []
    for b in range(n):
        for g in range(n):
            for r in range(n):
                v = fn(r / (n - 1), g / (n - 1), b / (n - 1))
                out.append(tuple(min(1.0, max(0.0, c)) for c in v))
    return out


def to_strip(n, table):
    """RGBA8 の幅 n*n・高さ n。画素 (b*n + r, g)。"""
    width = n * n
    buf = bytearray(width * n * 4)
    for b in range(n):
        for g in range(n):
            for r in range(n):
                v = table[(b * n + g) * n + r]
                o = (g * width + b * n + r) * 4
                for k in range(3):
                    buf[o + k] = min(255, max(0, int(v[k] * 255.0 + 0.5)))
                buf[o + 3] = 255
    return width, n, bytes(buf)


def encode_png(width, height, rgba):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="in.cube out.png、または --identity のとき out.png だけ")
    ap.add_argument("--size", type=int, help="出力の一辺 N (2 から 64)。省略は .cube の一辺")
    ap.add_argument("--identity", type=int, metavar="N", help="一辺 N の恒等 LUT を書く")
    ap.add_argument("--preset", choices=sorted(PRESETS), help="見本の LUT を書く (out.png だけを渡す)。--size で一辺を替える (既定 32)")
    args = ap.parse_args(argv)
    if args.identity or args.preset:
        if len(args.paths) != 1:
            ap.error("--identity と --preset は出力の PNG だけを渡す")
        n = args.identity or args.size or 32
        table = identity_table(n) if args.identity else preset_table(args.preset, n)
        out = Path(args.paths[0])
    else:
        if len(args.paths) != 2:
            ap.error("in.cube と out.png を渡す")
        size, table, lo, hi = parse_cube(Path(args.paths[0]).read_text(encoding="utf-8"))
        n = args.size or size
        table = resample(size, table, n, lo, hi)
        out = Path(args.paths[1])
    if not 2 <= n <= 64:
        ap.error("一辺は 2 から 64")
    w, h, rgba = to_strip(n, table)
    out.write_bytes(encode_png(w, h, rgba))
    print(f"wrote {out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
