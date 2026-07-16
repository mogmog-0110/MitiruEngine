# clod_engine.hlsl を DXC (SM 6.6) でコンパイルし、DXIL byte 配列ヘッダ
# include/mitiru/render/dx12/clod/ClodShaderBlobs_tables.hpp を生成する。
# d3dcompiler (FXC) は SM 6.6 を扱えないため offline 生成 + checked-in 運用。
# 使い方: python tools/clod_shaders/generate_blobs.py [--dxc <path to dxc.exe>]
import argparse
import pathlib
import subprocess
import sys

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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dxc", default=DEFAULT_DXC)
    args = ap.parse_args()

    here = pathlib.Path(__file__).parent
    hlsl = here / "clod_engine.hlsl"
    out = here / "../../include/mitiru/render/dx12/clod/ClodShaderBlobs_tables.hpp"
    out = out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)

    parts = [
        "#pragma once\n",
        "// 生成物 — 編集禁止。tools/clod_shaders/generate_blobs.py が\n"
        "// clod_engine.hlsl (SM 6.6) から生成する DXIL blob 群。\n",
        "#include <cstdint>\n#include <cstddef>\n",
        "namespace mitiru::render::clod {\n",
    ]
    for entry, profile, name in ENTRIES:
        dxil = here / f"{entry}.dxil"
        cmd = [args.dxc, "-T", profile, "-E", entry,
               "-Fo", str(dxil), str(hlsl)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"DXC failed for {entry}:\n{r.stderr}", file=sys.stderr)
            return 1
        data = dxil.read_bytes()
        dxil.unlink()
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
