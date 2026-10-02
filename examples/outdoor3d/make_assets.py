"""outdoor3d の素材 (高さマップ、層の画像、草と木の密度、岩と松のモデル) を作る。

標準ライブラリだけで書く。乱数はハッシュで決めるので、何度走らせても同じファイルになる。
出力は assets/ (コミット済み)。作り直すときだけ `python make_assets.py` を走らせる。
"""

import json
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "assets")

SIZE = 257          # 高さマップの標本の数 (1 辺)
EXTENT = 128.0      # 地形の 1 辺 (m)
HEIGHT = 24.0       # 標本 65535 の高さ (m)
SEA = 3.0           # 水面の高さ (m)


def hash01(*ints):
    h = 2166136261
    for v in ints:
        h ^= v & 0xFFFFFFFF
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= h >> 13
        h = (h * 0x5BD1E995) & 0xFFFFFFFF
    return (h & 0xFFFFFF) / float(0x1000000)


def value_noise(x, z, seed, period=0):
    """period が正なら格子を period ごとに繰り返す (層の画像を継ぎ目なく並べるため)"""
    ix, iz = math.floor(x), math.floor(z)
    fx, fz = x - ix, z - iz
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    wrap = (lambda v: v % period) if period > 0 else (lambda v: v)
    a, b = hash01(wrap(ix), wrap(iz), seed), hash01(wrap(ix + 1), wrap(iz), seed)
    c, d = hash01(wrap(ix), wrap(iz + 1), seed), hash01(wrap(ix + 1), wrap(iz + 1), seed)
    return (a + (b - a) * sx) * (1 - sz) + (c + (d - c) * sx) * sz


def fbm(x, z, seed, octaves=5, period=0):
    total, amp, freq = 0.0, 0.5, 1.0
    for o in range(octaves):
        total += amp * value_noise(x * freq, z * freq, seed + o * 31, period * int(freq))
        amp *= 0.5
        freq *= 2.0
    return total


def island_height(x, z):
    """世界の (x, z) の高さ (m)。中央が丘、外へ下がって海に沈む。北東に急な岩山"""
    r = math.hypot(x, z) / (EXTENT * 0.5)
    shape = max(0.0, 1.0 - r * r) ** 1.3
    hills = fbm(x / 22.0, z / 22.0, 7) * 10.0
    crag = max(0.0, 1.0 - math.hypot(x - 22.0, z + 20.0) / 16.0) ** 2 * 11.0
    return shape * (2.0 + hills + 8.0 * shape) + crag - 1.5


def smooth(a, b, v):
    t = min(max((v - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def write_png(path, width, height, rows, color_type, bit_depth):
    """rows は行ごとの bytes。color_type 0 = 灰、6 = RGBA"""
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def sample_world(i):
    return -EXTENT * 0.5 + i * EXTENT / (SIZE - 1)


def make_heightmap():
    heights = [[island_height(sample_world(ix), sample_world(iz)) for ix in range(SIZE)] for iz in range(SIZE)]
    rows = []
    for iz in range(SIZE):
        row = b"".join(struct.pack(">H", int(round(min(max(h / HEIGHT, 0.0), 1.0) * 65535))) for h in heights[iz])
        rows.append(row)
    write_png(os.path.join(OUT, "island_height.png"), SIZE, SIZE, rows, 0, 16)
    return heights


def slope_deg(heights, ix, iz):
    cell = EXTENT / (SIZE - 1)
    x0, x1 = max(ix - 1, 0), min(ix + 1, SIZE - 1)
    z0, z1 = max(iz - 1, 0), min(iz + 1, SIZE - 1)
    dx = (heights[iz][x1] - heights[iz][x0]) / ((x1 - x0) * cell)
    dz = (heights[z1][ix] - heights[z0][ix]) / ((z1 - z0) * cell)
    return math.degrees(math.atan(math.hypot(dx, dz)))


def make_density(heights):
    """草は浜より上で岩肌でない所、木は丘の中腹の林だけ"""
    grass, trees = [], []
    for iz in range(SIZE):
        g, t = bytearray(), bytearray()
        for ix in range(SIZE):
            h, s = heights[iz][ix], slope_deg(heights, ix, iz)
            flat = 1.0 - smooth(26.0, 34.0, s)
            g.append(int(255 * smooth(4.0, 5.0, h) * flat))
            grove = smooth(0.45, 0.6, fbm(sample_world(ix) / 18.0, sample_world(iz) / 18.0, 91, 3))
            t.append(int(255 * grove * smooth(5.0, 6.5, h) * flat))
        grass.append(bytes(g))
        trees.append(bytes(t))
    write_png(os.path.join(OUT, "island_grass.png"), SIZE, SIZE, grass, 0, 8)
    write_png(os.path.join(OUT, "island_trees.png"), SIZE, SIZE, trees, 0, 8)


def make_layer(name, base, variation, scale, seed, bumpiness):
    """128 x 128 の繰り返す色の画像と法線マップ。scale は 128 を割り切る数にする"""
    n = 128
    hgt = [[fbm(x / scale, z / scale, seed, 4, n // scale) for x in range(n)] for z in range(n)]
    color_rows, normal_rows = [], []
    for z in range(n):
        crow, nrow = bytearray(), bytearray()
        for x in range(n):
            v = hgt[z][x]
            for k in range(3):
                crow.append(int(min(max(base[k] * (1.0 + variation * (v - 0.5) * 2.0), 0.0), 1.0) * 255))
            crow.append(255)
            dx = (hgt[z][(x + 1) % n] - hgt[z][(x - 1) % n]) * bumpiness
            dz = (hgt[(z + 1) % n][x] - hgt[(z - 1) % n][x]) * bumpiness
            inv = 1.0 / math.sqrt(dx * dx + dz * dz + 1.0)
            # 緑は v が減る向き (glTF と同じ)
            nrow += bytes([int((-dx * inv * 0.5 + 0.5) * 255), int((dz * inv * 0.5 + 0.5) * 255), int((inv * 0.5 + 0.5) * 255), 255])
        color_rows.append(bytes(crow))
        normal_rows.append(bytes(nrow))
    write_png(os.path.join(OUT, name + ".png"), n, n, color_rows, 6, 8)
    write_png(os.path.join(OUT, name + "_n.png"), n, n, normal_rows, 6, 8)


def write_glb(path, prims):
    """prims: [(positions, normals, indices, rgba)] を 1 つのメッシュの prim として書く"""
    blob, views, accessors, materials, gl_prims = b"", [], [], [], []
    for positions, normals, indices, rgba in prims:
        def add(data, target, count, comp, kind, mn=None, mx=None):
            nonlocal blob
            blob += b"\x00" * (-len(blob) % 4)
            views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
            blob += data
            acc = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": kind}
            if mn is not None:
                acc["min"], acc["max"] = mn, mx
            accessors.append(acc)
            return len(accessors) - 1
        flat = [c for p in positions for c in p]
        mn = [min(p[k] for p in positions) for k in range(3)]
        mx = [max(p[k] for p in positions) for k in range(3)]
        pos = add(struct.pack("<%df" % len(flat), *flat), 34962, len(positions), 5126, "VEC3", mn, mx)
        nflat = [c for nrm in normals for c in nrm]
        nor = add(struct.pack("<%df" % len(nflat), *nflat), 34962, len(normals), 5126, "VEC3")
        idx = add(struct.pack("<%dI" % len(indices), *indices), 34963, len(indices), 5125, "SCALAR")
        materials.append({"pbrMetallicRoughness": {"baseColorFactor": rgba, "metallicFactor": 0.0, "roughnessFactor": 0.9}})
        gl_prims.append({"attributes": {"POSITION": pos, "NORMAL": nor}, "indices": idx, "material": len(materials) - 1})
    blob += b"\x00" * (-len(blob) % 4)
    doc = {"asset": {"version": "2.0", "generator": "outdoor3d make_assets.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
           "nodes": [{"mesh": 0}], "meshes": [{"primitives": gl_prims}], "materials": materials,
           "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    total = 12 + 8 + len(text) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


def faceted(tris):
    """三角形の列 (頂点 3 つずつ) を、面ごとの法線を持つ頂点の列にする"""
    pos, nor, idx = [], [], []
    for a, b, c in tris:
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        ln = math.sqrt(sum(v * v for v in n)) or 1.0
        for p in (a, b, c):
            idx.append(len(pos))
            pos.append(p)
            nor.append([v / ln for v in n])
    return pos, nor, idx


def make_rock():
    t = (1.0 + math.sqrt(5.0)) / 2.0
    v = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
         [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]]
    f = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6],
         [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]]
    pts = []
    for i, p in enumerate(v):
        ln = math.sqrt(sum(c * c for c in p))
        r = 0.45 * (0.8 + 0.4 * hash01(i, 5))
        pts.append([p[0] / ln * r * 1.2, max(p[1] / ln * r * 0.75, -0.1) + 0.1, p[2] / ln * r])
    pos, nor, idx = faceted([(pts[a], pts[b], pts[c]) for a, b, c in f])
    write_glb(os.path.join(OUT, "rock.glb"), [(pos, nor, idx, [0.30, 0.29, 0.27, 1.0])])


def cone(base_y, top_y, radius, sides):
    tris = []
    for i in range(sides):
        a0, a1 = 2 * math.pi * i / sides, 2 * math.pi * (i + 1) / sides
        p0 = [radius * math.cos(a0), base_y, radius * math.sin(a0)]
        p1 = [radius * math.cos(a1), base_y, radius * math.sin(a1)]
        tris.append(([0.0, top_y, 0.0], p1, p0))
        tris.append(([0.0, base_y, 0.0], p0, p1))
    return tris


def make_pine():
    trunk = faceted(cone(0.0, 1.4, 0.16, 6))
    leaves = faceted(cone(0.8, 2.6, 0.9, 8) + cone(1.7, 3.4, 0.65, 8))
    write_glb(os.path.join(OUT, "pine.glb"), [(trunk[0], trunk[1], trunk[2], [0.20, 0.11, 0.05, 1.0]),
                                              (leaves[0], leaves[1], leaves[2], [0.05, 0.16, 0.06, 1.0])])


def main():
    os.makedirs(OUT, exist_ok=True)
    heights = make_heightmap()
    make_density(heights)
    make_layer("grass", [0.24, 0.42, 0.13], 0.35, 8, 3, 6.0)
    make_layer("sand", [0.78, 0.70, 0.50], 0.15, 4, 17, 3.0)
    make_layer("rock", [0.42, 0.40, 0.38], 0.45, 8, 29, 10.0)
    make_rock()
    make_pine()


if __name__ == "__main__":
    main()
