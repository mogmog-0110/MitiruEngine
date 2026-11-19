"""PNG をパレットファイル (.hex) の色だけに変換する。

    python tools/pal_convert.py in.png out.png [--palette p.hex] [--dither] [--dither-spread N] [--alpha-cutoff N]
    python tools/pal_convert.py --extract in.png out.hex [--max-colors N]

--palette を省くと assets/palettes/default16.hex。--dither は 4x4 の Bayer 行列でしきい値を足してから最も近い色を探す。
形式と式の詳細は docs/PALETTE.md。標準ライブラリだけで動く。
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

DEFAULT_PALETTE = Path(__file__).resolve().parent.parent / "assets" / "palettes" / "default16.hex"
MAX_COLORS = 256
BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def parse_palette(text):
    """1 行 1 色の RRGGBB (頭の # は任意、空行は飛ばす) を [(r, g, b), ...] にする。"""
    colors = []
    for number, raw in enumerate(text.lstrip("﻿").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        digits = line[1:] if line.startswith("#") else line
        if len(digits) != 6 or any(ch not in "0123456789abcdefABCDEF" for ch in digits):
            raise ValueError(f"{number} 行目: RRGGBB の 6 桁の 16 進数ではない ({line!r})")
        colors.append((int(digits[0:2], 16), int(digits[2:4], 16), int(digits[4:6], 16)))
    if not colors:
        raise ValueError("色が 1 つもない")
    if len(colors) > MAX_COLORS:
        raise ValueError(f"色が {len(colors)} 個ある (上限 {MAX_COLORS})")
    return colors


def format_palette(colors):
    if not 1 <= len(colors) <= MAX_COLORS:
        raise ValueError(f"色は 1 から {MAX_COLORS} 個 (渡された数 {len(colors)})")
    return "".join(f"{r:02x}{g:02x}{b:02x}\n" for r, g, b in colors)


def load_palette(path):
    return parse_palette(Path(path).read_text(encoding="utf-8"))


def save_palette(path, colors):
    Path(path).write_text(format_palette(colors), encoding="utf-8", newline="\n")


def nearest_index(colors, r, g, b):
    best, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(colors):
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def dither_offset(x, y, spread):
    # 浮動小数だと C++ 側 (PaletteFile.hpp) と同じ値にならない画素が出るので、床の整数にそろえる
    return ((2 * BAYER4[y & 3][x & 3] - 15) * spread) >> 5


def convert_pixels(width, height, rgba, colors, dither=False, spread=64, alpha_cutoff=128):
    out = bytearray(len(rgba))
    cache = {}

    def clamp(v):
        return 0 if v < 0 else 255 if v > 255 else v

    for y in range(height):
        for x in range(width):
            o = (y * width + x) * 4
            r, g, b, a = rgba[o], rgba[o + 1], rgba[o + 2], rgba[o + 3]
            if a < alpha_cutoff:
                continue
            off = dither_offset(x, y, spread) if dither else 0
            key = (r, g, b, off)
            hit = cache.get(key)
            if hit is None:
                hit = colors[nearest_index(colors, clamp(r + off), clamp(g + off), clamp(b + off))]
                cache[key] = hit
            out[o], out[o + 1], out[o + 2], out[o + 3] = hit[0], hit[1], hit[2], 255
    return bytes(out)


def extract_colors(rgba, alpha_cutoff=128, max_colors=16):
    seen = {}
    for o in range(0, len(rgba), 4):
        if rgba[o + 3] < alpha_cutoff:
            continue
        seen.setdefault((rgba[o], rgba[o + 1], rgba[o + 2]), None)
        if len(seen) > max_colors:
            raise ValueError(f"色が {max_colors} 個を超えた。--max-colors を上げるか、先にパレットへ変換する")
    return list(seen)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def _unfilter(raw, width_bytes, height, bpp):
    rows, prev, pos = [], bytearray(width_bytes), 0
    for _ in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + width_bytes])
        pos += 1 + width_bytes
        if kind == 1:
            for i in range(bpp, width_bytes):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif kind == 2:
            for i in range(width_bytes):
                line[i] = (line[i] + prev[i]) & 255
        elif kind == 3:
            for i in range(width_bytes):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(width_bytes):
                left = line[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], upleft)) & 255
        elif kind != 0:
            raise ValueError(f"PNG のフィルタ {kind} は読めない")
        rows.append(line)
        prev = line
    return rows


def _samples(line, width, channels, depth):
    # 16 bit は上位 byte だけ使う (出力が 8 bit なので)
    if depth == 8:
        return line
    if depth == 16:
        return line[0::2]
    per = 8 // depth
    mask = (1 << depth) - 1
    out = bytearray()
    for i in range(width * channels):
        out.append((line[i // per] >> (8 - depth * (i % per + 1))) & mask)
    return out


def decode_png(data):
    """(width, height, RGBA の bytes)"""
    try:
        return _decode_png(data)
    except (IndexError, struct.error, zlib.error) as e:
        raise ValueError(f"PNG が壊れている ({e})") from e


def _color_key(trns, ctype, depth):
    # グレーと RGB の透過は、パレットの番号ではなく「この色を透明にする」という 1 色の指定で書かれる
    n = 1 if ctype == 0 else 3 if ctype == 2 else 0
    if n == 0 or len(trns) < 2 * n:
        return None
    mask = (1 << depth) - 1
    return tuple(struct.unpack(">H", trns[2 * i:2 * i + 2])[0] & mask for i in range(n))


def _raw_pixel(line, samples, x, channels, depth):
    if depth == 16:
        return tuple((line[(x * channels + c) * 2] << 8) | line[(x * channels + c) * 2 + 1] for c in range(channels))
    return tuple(samples[x * channels:(x + 1) * channels])


def _decode_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("PNG ではない")
    pos, idat, plte, trns, header = 8, [], b"", b"", None
    while pos + 8 <= len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        tag, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            plte = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat.append(body)
        pos += 12 + n
    if header is None:
        raise ValueError("PNG の IHDR が無い")
    width, height, depth, ctype, _, _, interlace = header
    if interlace:
        raise ValueError("インターレースの PNG は読めない (インターレース無しで保存し直す)")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth not in (1, 2, 4, 8, 16):
        raise ValueError(f"PNG の種類 (color type {ctype}, bit depth {depth}) は読めない")
    width_bytes = (width * channels * depth + 7) // 8
    rows = _unfilter(zlib.decompress(b"".join(idat)), width_bytes, height, max(1, channels * depth // 8))
    scale = 255 // ((1 << depth) - 1) if depth < 8 and ctype == 0 else 1
    key = _color_key(trns, ctype, depth)
    out = bytearray(width * height * 4)
    for y, line in enumerate(rows):
        s = _samples(line, width, channels, depth)
        for x in range(width):
            o = (y * width + x) * 4
            if ctype == 6:
                out[o:o + 4] = s[x * 4:x * 4 + 4]
            elif ctype == 2:
                out[o:o + 3] = s[x * 3:x * 3 + 3]
                out[o + 3] = 0 if key and _raw_pixel(line, s, x, 3, depth) == key else 255
            elif ctype == 0:
                v = s[x] * scale
                out[o:o + 4] = bytes((v, v, v, 0 if key and _raw_pixel(line, s, x, 1, depth) == key else 255))
            elif ctype == 4:
                v = s[x * 2]
                out[o:o + 4] = bytes((v, v, v, s[x * 2 + 1]))
            else:
                i = s[x]
                out[o:o + 3] = plte[i * 3:i * 3 + 3]
                out[o + 3] = trns[i] if i < len(trns) else 255
    return width, height, bytes(out)


def encode_png(width, height, rgba):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs=2, help="in.png out.png、--extract のときは in.png out.hex")
    ap.add_argument("--palette", type=Path, default=DEFAULT_PALETTE, help="使うパレットファイル (省くと assets/palettes/default16.hex)")
    ap.add_argument("--dither", action="store_true", help="4x4 の Bayer 行列でディザにする")
    ap.add_argument("--dither-spread", type=int, default=64, metavar="N", help="ディザの幅 (0 から 255、既定 64)")
    ap.add_argument("--alpha-cutoff", type=int, default=128, metavar="N", help="これ以上のアルファを不透明にする (既定 128)")
    ap.add_argument("--extract", action="store_true", help="画像の色をパレットファイルへ書く")
    ap.add_argument("--max-colors", type=int, default=16, metavar="N", help="--extract で許す色の数 (既定 16、上限 256)")
    args = ap.parse_args(argv)
    if not 0 <= args.dither_spread <= 255:
        ap.error("--dither-spread は 0 から 255")
    try:
        width, height, rgba = decode_png(Path(args.paths[0]).read_bytes())
        if args.extract:
            colors = extract_colors(rgba, args.alpha_cutoff, min(args.max_colors, MAX_COLORS))
            save_palette(args.paths[1], colors)
            print(f"wrote {args.paths[1]} ({len(colors)} colors)")
            return 0
        colors = load_palette(args.palette)
        out = convert_pixels(width, height, rgba, colors, args.dither, args.dither_spread, args.alpha_cutoff)
        Path(args.paths[1]).write_bytes(encode_png(width, height, out))
    except (OSError, ValueError, zlib.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"wrote {args.paths[1]} ({width}x{height}, {len(colors)} colors{', dither' if args.dither else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
