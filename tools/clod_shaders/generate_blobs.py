# clod_engine.hlsl を DXC (SM 6.6) でコンパイルし、DXIL byte 配列ヘッダ
# include/mitiru/render/dx12/clod/ClodShaderBlobs_tables.hpp を生成する。
# d3dcompiler (FXC) は SM 6.6 を扱えないため offline 生成 + checked-in 運用。
# DXC はこのファイルかシェーダーを直す人だけが要り、dxil.dll は配らない。
#
# 使い方: python tools/clod_shaders/generate_blobs.py [--dxc <path to dxc.exe>]
# 既定の DXC は 1.8.2505.28 (dxcompiler.dll 1.9、9efbb6c32)。同じ版なら同じソースから同じバイト列が出る。
# 1 本ごとに次の形で呼ぶ:
#   dxc.exe -T cs_6_6 -E ResolveCS -I <一時フォルダ> -Fo <一時フォルダ>/ResolveCS.dxil tools/clod_shaders/clod_engine.hlsl
#
# 焼いた光の引き方 (放射照度のプローブ・漏れの防ぎ・トゥーンの段・反射のプローブ) と太陽の影の引き方 (余白・PCF・
# カスケードの選び方) は前方の描画と同じ式を使うため、SHARED の C++ の raw string を取り出して一時フォルダの
# .hlsli に書き、clod_engine.hlsl が #include する。取り出した文字列ごとの FNV-1a (64 bit) を blob の表に残し、
# 前方の側だけ変えて blob を作り直し忘れたことを試験が見つけられるようにする。
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ENTRIES = [
    ("MSMain", "ms_6_6", "kClodMS"),
    ("PSMain", "ps_6_6", "kClodPS"),
    ("CullCS", "cs_6_6", "kClodCull"),
    ("PrepArgsCS", "cs_6_6", "kClodPrep"),
    ("VisClear", "cs_6_6", "kClodClear"),
    ("ResolveCS", "cs_6_6", "kClodResolve"),
    ("SwRasterCS", "cs_6_6", "kClodSwRaster"),
    ("InstCullCS", "cs_6_6", "kClodInstCull"),
    ("SeedCS", "cs_6_6", "kClodSeed"),
    ("TraverseCS", "cs_6_6", "kClodTraverse"),
    ("PrepQueueCS", "cs_6_6", "kClodPrepQueue"),
    ("HzbBuild", "cs_6_6", "kClodHzb"),
]

DEFAULT_DXC = r"E:\user\cluster-lod-renderer\external\dxc\build\native\bin\x64\dxc.exe"

# (C++ の定数名, 置いてあるヘッダ, 一時フォルダに書く名前, blob の表に残す hash の名前)
SHARED = [
    ("DX12_LIGHTING_PROBES_HLSL", "include/mitiru/render/dx12/DX12LightingProbeShaders.hpp",
     "lighting_probes.hlsli", "kClodLightingProbesHash"),
    ("DX12_SUN_SHADOW_HLSL", "include/mitiru/render/dx12/DX12SunShadowShaders.hpp",
     "sun_shadow.hlsli", "kClodSunShadowHash"),
]


def raw_string(repo: pathlib.Path, header: str, name: str) -> str:
    """name の raw string の中身 (C++ の翻訳と同じく改行は LF)"""
    src = (repo / header).read_text(encoding="utf-8")
    m = re.search(name + r' = R"hlsl\((.*?)\)hlsl"', src, re.S)
    if m is None:
        raise RuntimeError(f"{name} が {header} に見つからない")
    return m.group(1)


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dxc", default=DEFAULT_DXC)
    args = ap.parse_args()

    here = pathlib.Path(__file__).parent
    repo = (here / "../..").resolve()
    hlsl = here / "clod_engine.hlsl"
    out = repo / "include/mitiru/render/dx12/clod/ClodShaderBlobs_tables.hpp"
    out.parent.mkdir(parents=True, exist_ok=True)
    shared = [(raw_string(repo, header, name), file, hash_name) for name, header, file, hash_name in SHARED]

    parts = [
        "#pragma once\n",
        "// 生成物。編集禁止。tools/clod_shaders/generate_blobs.py が\n"
        "// clod_engine.hlsl (SM 6.6) から生成する DXIL blob 群。\n",
        "#include <cstdint>\n#include <cstddef>\n",
        "namespace mitiru::render::clod {\n",
    ]
    for text, _, hash_name in shared:
        parts.append(f"inline constexpr uint64_t {hash_name} = 0x{fnv1a64(text.encode('utf-8')):016X}ull;\n")
    with tempfile.TemporaryDirectory(prefix="clod_blobs_") as tmp:
        tmpdir = pathlib.Path(tmp)
        for text, file, _ in shared:
            (tmpdir / file).write_text(text, encoding="utf-8", newline="\n")
        for entry, profile, name in ENTRIES:
            dxil = tmpdir / f"{entry}.dxil"
            cmd = [args.dxc, "-T", profile, "-E", entry, "-I", str(tmpdir),
                   "-Fo", str(dxil), str(hlsl)]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                print(f"DXC failed for {entry}:\n{r.stderr}", file=sys.stderr)
                return 1
            data = dxil.read_bytes()
            body = ",".join(str(b) for b in data)
            parts.append(f"inline constexpr uint8_t {name}[] = {{{body}}};\n")
            parts.append(f"inline constexpr size_t {name}Size = sizeof({name});\n")
            print(f"{entry}: {len(data)} bytes")
    parts.append("} // namespace mitiru::render::clod\n")
    out.write_text("".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
