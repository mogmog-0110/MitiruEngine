# ddgi.hlsl を DXC (SM 6.6) でコンパイルし、DXIL の byte 配列のヘッダ
# include/mitiru/render/dx12/ddgi/DdgiShaderBlobs_tables.hpp を作る。
# inline ray query (SM 6.5) と ResourceDescriptorHeap (SM 6.6) を使うので d3dcompiler (FXC) では作れない。
# DXC はシェーダーを直す人だけが要り、dxil.dll は配らない。
#
# 使い方: python tools/ddgi_shaders/generate_blobs.py [--dxc <dxc.exe のパス>]
# 既定の DXC は clod と同じ 1.8.2505.28。同じ版なら同じソースから同じバイト列が出る。1 本ごとに次の形で呼ぶ:
#   dxc.exe -T cs_6_6 -E TraceCS -I <一時フォルダ> -Fo <一時フォルダ>/TraceCS.dxil tools/ddgi_shaders/ddgi.hlsl
#
# 跳ね返りは前方の描画と同じ式で前のフレームの格子を引くため、DX12_LIGHTING_PROBES_HLSL を一時フォルダの
# lighting_probes.hlsli に書いて取り込む。取り出した文字列の FNV-1a (64 bit) を blob の表に残し、前方の側だけ
# 変えて blob を作り直し忘れたことを試験が見つけられるようにする (clod の blob と同じ仕組み)。
import argparse
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
sys.path.insert(0, str(HERE.parent / "clod_shaders"))
from generate_blobs import DEFAULT_DXC, fnv1a64, raw_string  # noqa: E402

ENTRIES = [
    ("TraceCS", "cs_6_6", "kDdgiTrace"),
    ("UpdateCS", "cs_6_6", "kDdgiUpdate"),
]

SHARED = ("DX12_LIGHTING_PROBES_HLSL", "include/mitiru/render/dx12/DX12LightingProbeShaders.hpp",
          "lighting_probes.hlsli", "kDdgiLightingProbesHash")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dxc", default=DEFAULT_DXC)
    args = ap.parse_args()

    repo = (HERE / "../..").resolve()
    hlsl = HERE / "ddgi.hlsl"
    out = repo / "include/mitiru/render/dx12/ddgi/DdgiShaderBlobs_tables.hpp"
    out.parent.mkdir(parents=True, exist_ok=True)
    name, header, file, hash_name = SHARED
    text = raw_string(repo, header, name)

    parts = [
        "#pragma once\n",
        "// 生成物。編集禁止。tools/ddgi_shaders/generate_blobs.py が\n"
        "// ddgi.hlsl (SM 6.6) から生成する DXIL blob 群。\n",
        "#include <cstdint>\n#include <cstddef>\n",
        "namespace mitiru::render::ddgi {\n",
        f"inline constexpr uint64_t {hash_name} = 0x{fnv1a64(text.encode('utf-8')):016X}ull;\n",
    ]
    with tempfile.TemporaryDirectory(prefix="ddgi_blobs_") as tmp:
        tmpdir = pathlib.Path(tmp)
        (tmpdir / file).write_text(text, encoding="utf-8", newline="\n")
        for entry, profile, blob in ENTRIES:
            dxil = tmpdir / f"{entry}.dxil"
            cmd = [args.dxc, "-T", profile, "-E", entry, "-I", str(tmpdir), "-Fo", str(dxil), str(hlsl)]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                print(f"DXC failed for {entry}:\n{r.stderr}", file=sys.stderr)
                return 1
            data = dxil.read_bytes()
            parts.append(f"inline constexpr uint8_t {blob}[] = {{{','.join(str(b) for b in data)}}};\n")
            parts.append(f"inline constexpr size_t {blob}Size = sizeof({blob});\n")
            print(f"{entry}: {len(data)} bytes")
    parts.append("} // namespace mitiru::render::ddgi\n")
    out.write_text("".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
