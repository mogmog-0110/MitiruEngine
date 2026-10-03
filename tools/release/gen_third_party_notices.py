#!/usr/bin/env python3
"""配布物に同梱する第三者ライセンス表記 THIRD_PARTY_NOTICES.txt を作る。

ライセンス本文は external/ の各ライブラリが持つ LICENSE (無いものはソース内の表記) から
そのまま写す。手で写すと版が上がったときに古い本文が残るので、表 LIBRARIES だけを手で保つ。

使い方:
    python tools/release/gen_third_party_notices.py          # リポジトリ直下に書く
    python tools/release/gen_third_party_notices.py --check  # 書かれた内容が最新か調べる (差があれば 1)

SDL3 は on-demand で置かれる (tools/fetch_sdl3.py か configure) ので、それが無い環境では作れない。
配布物を作る機械で実行する。

ゲームの配布物には、そのビルドが実際に link した物と exe の隣に置いた物だけを書く。mitiru_host を
ビルドすると cmake/MitiruShipAssets.cmake がこの形で呼び、host の隣に THIRD_PARTY_NOTICES.txt を置く:
    python tools/release/gen_third_party_notices.py --features <定義の一覧> --deploy-dir <host の dir> --out <file>
<定義の一覧> は link した target の compile definitions (1 行 1 つ、MITIRU_HAS_JOLT=1 など)。
"""

from __future__ import annotations

import argparse
import glob
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUTPUT = REPO / "THIRD_PARTY_NOTICES.txt"

FREETYPE_CREDIT = (
    "Portions of this software are copyright (c) The FreeType Project "
    "(www.freetype.org). All rights reserved."
)

MIT_BODY = """Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE."""

BSD3_BODY = """Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of {owner} nor the names of its contributors may be used
   to endorse or promote products derived from this software without specific
   prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."""

MPLUS_LICENSE = """Copyright (C) 2002-2013 M+ FONTS PROJECT

These fonts are free software.
Unlimited permission is granted to use, copy, and distribute them, with
or without modification, either commercially or noncommercially.
THESE FONTS ARE PROVIDED "AS IS" WITHOUT WARRANTY."""


@dataclass(frozen=True)
class Library:
    name: str
    version: str  # 手で保つ。"{dir:<ファイルの glob>}" ならそのファイルのあるディレクトリ名から取る
    license: str
    url: str
    # ("file", パス) / ("excerpt", パス, 開始文字列, 終了文字列) / ("text", 本文)
    source: tuple
    # 配布物に載せる条件。"def:<定義>" は link した target にその定義がある時、"file:<glob>" は host の
    # dir にその file がある時。どれか 1 つが合えば載せる。空なら常に載せる
    when: tuple = ()


SECTIONS: list[tuple[str, list[Library]]] = [
    ("Compiled into mitiru_host and game modules", [
        Library("FreeType", "2.14.3", "FreeType License (FTL)", "https://freetype.org",
                ("file", "external/freetype/docs/FTL.TXT")),
        Library("RmlUi", "6.3", "MIT", "https://github.com/mikke89/RmlUi",
                ("file", "external/rmlui/LICENSE.txt"),
                when=("def:MITIRU_HAS_RMLUI",)),
        Library("Third-party code inside RmlUi Core (robin-hood-hashing and others)", "", "MIT", "https://github.com/mikke89/RmlUi",
                ("file", "external/rmlui/Include/RmlUi/Core/Containers/LICENSE.txt"),
                when=("def:MITIRU_HAS_RMLUI",)),
        Library("HarfBuzz", "14.5.1", "MIT (\"Old MIT\")", "https://github.com/harfbuzz/harfbuzz",
                ("file", "external/harfbuzz/COPYING")),
        Library("msdfgen", "1.13", "MIT", "https://github.com/Chlumsky/msdfgen",
                ("file", "external/msdfgen/LICENSE.txt")),
        Library("msdf-atlas-gen", "1.4", "MIT", "https://github.com/Chlumsky/msdf-atlas-gen",
                ("file", "external/msdf-atlas-gen/LICENSE.txt")),
        Library("Jolt Physics", "5.x", "MIT", "https://github.com/jrouwe/JoltPhysics",
                ("file", "external/jolt/LICENSE"),
                when=("def:MITIRU_HAS_JOLT",)),
        Library("Box2D", "3.1.1", "MIT", "https://github.com/erincatto/box2d",
                ("file", "external/box2d/LICENSE"),
                when=("def:MITIRU_HAS_BOX2D",)),
        Library("Zstandard", "1.6.0", "BSD-3-Clause (dual BSD / GPLv2, used under BSD)",
                "https://github.com/facebook/zstd", ("file", "external/zstd/LICENSE"),
                when=("def:MITIRU_HAS_ZSTD",)),
        Library("D3D12 Memory Allocator", "3.2.0", "MIT",
                "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator",
                ("file", "external/D3D12MemoryAllocator/LICENSE.txt")),
        Library("xxHash", "0.8.3", "BSD-2-Clause", "https://github.com/Cyan4973/xxHash",
                ("file", "external/xxhash/LICENSE")),
        Library("cpp-httplib", "0.58.0", "MIT", "https://github.com/yhirose/cpp-httplib",
                ("file", "external/httplib/LICENSE")),
        Library("fpng", "", "Unlicense (public domain)", "https://github.com/richgel999/fpng",
                ("excerpt", "external/fpng/fpng.cpp",
                 "This is free and unencumbered software released into the public domain.",
                 "For more information, please refer to <http://unlicense.org/>")),
        Library("efsw", "1.7.2", "MIT", "https://github.com/SpartanJ/efsw",
                ("file", "external/efsw/LICENSE"),
                when=("def:MITIRU_HAS_EFSW",)),
        Library("Taskflow", "4.1.0", "MIT", "https://github.com/taskflow/taskflow",
                ("file", "external/taskflow/LICENSE"),
                when=("def:MITIRU_HAS_TASKFLOW",)),
        Library("bc7enc_rdo (bc7enc / rgbcx / bc7decomp)", "", "MIT or public domain",
                "https://github.com/richgel999/bc7enc_rdo", ("file", "external/bc7enc_rdo/LICENSE")),
        Library("NVIDIA FLIP", "", "BSD-3-Clause", "https://github.com/NVlabs/flip",
                ("file", "external/flip/LICENSE"),
                when=("def:MITIRU_HAS_FLIP",)),
        Library("Recast & Detour", "1.6.0", "zlib", "https://github.com/recastnavigation/recastnavigation",
                ("file", "external/recastnavigation/License.txt"),
                when=("def:MITIRU_HAS_DETOUR", "def:MITIRU_HAS_RECAST")),
        Library("GekkoNet (only with MITIRU_WITH_GEKKONET)", "", "BSD-2-Clause", "https://github.com/HeatXD/GekkoNet",
                ("file", "external/GekkoNet/LICENSE"),
                when=("def:MITIRU_HAS_GEKKONET",)),
        Library("zpp_bits (inside GekkoNet)", "", "MIT", "https://github.com/eyalz800/zpp_bits",
                ("file", "external/GekkoNet/GekkoLib/thirdparty/zpp/LICENSE"),
                when=("def:MITIRU_HAS_GEKKONET",)),
        Library("meshoptimizer", "1.0", "MIT", "https://github.com/zeux/meshoptimizer",
                ("file", "external/meshoptimizer/LICENSE.md")),
        Library("cgltf", "", "MIT", "https://github.com/jkuhlmann/cgltf",
                ("excerpt", "external/cgltf/cgltf.h", "cgltf is distributed under MIT license:", " * SOFTWARE.")),
        Library("jsmn (inside cgltf)", "", "MIT", "https://github.com/zserge/jsmn",
                ("excerpt", "external/cgltf/cgltf.h", "Copyright (c) 2010 Serge A. Zaitsev", " * THE SOFTWARE.")),
        Library("ufbx", "0.23.1", "MIT or public domain", "https://github.com/ufbx/ufbx",
                ("file", "external/ufbx/LICENSE")),
        Library("tinyobjloader", "1.x", "MIT", "https://github.com/tinyobjloader/tinyobjloader",
                ("excerpt", "external/tiny_obj_loader.h", "The MIT License (MIT)", "THE SOFTWARE.")),
        Library("stb_image / stb_image_write / stb_vorbis", "", "MIT or public domain", "https://github.com/nothings/stb",
                ("excerpt", "external/stb/stb_image.h",
                 "This software is available under 2 licenses",
                 "WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.")),
        Library("miniaudio", "0.11.25", "Public domain or MIT No Attribution", "https://miniaud.io",
                ("excerpt", "external/miniaudio.h",
                 "This software is available as a choice of the following licenses.",
                 "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\nSOFTWARE.")),
        Library("JSON for Modern C++ (nlohmann/json)", "3.11.3", "MIT", "https://github.com/nlohmann/json",
                ("text", "Copyright (c) 2013-2023 Niels Lohmann\n\n" + MIT_BODY)),
        Library("GLM", "1.1.0", "MIT", "https://github.com/g-truc/glm",
                ("text", "Copyright (c) 2005 - G-Truc Creation\n\n" + MIT_BODY)),
        Library("ymfm (inside MitiruMML)", "", "BSD-3-Clause", "https://github.com/aaronsgiles/ymfm",
                ("file", "external/mml/external/ymfm/LICENSE")),
        Library("XeGTAO", "1.30 (a5b1686c)", "MIT", "https://github.com/GameTechDev/XeGTAO",
                ("file", "external/XeGTAO/LICENSE")),
        Library("Effekseer", "", "MIT", "https://github.com/effekseer/Effekseer",
                ("file", "external/Effekseer/LICENSE"), when=("def:MITIRU_HAS_EFFEKSEER",)),
        Library("LLGI (inside Effekseer)", "", "MIT", "https://github.com/altseed/LLGI",
                ("file", "external/Effekseer/Dev/Cpp/3rdParty/LLGI/LICENSE"), when=("def:MITIRU_HAS_EFFEKSEER",)),
        Library("spdlog", "", "MIT", "https://github.com/gabime/spdlog",
                ("text", "Copyright (c) 2016 Gabi Melman.\n\n" + MIT_BODY), when=("def:MITIRU_HAS_SPDLOG",)),
        Library("{fmt} (inside spdlog)", "", "MIT", "https://github.com/fmtlib/fmt",
                ("text", "Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors\n\n" + MIT_BODY
                 + "\n\n--- Optional exception to the license ---\n\n"
                 "As an exception, if, as a result of your compiling your source code, portions\n"
                 "of this Software are embedded into a machine-executable object form of such\n"
                 "source code, you may redistribute such embedded portions in such object form\n"
                 "without including the above copyright and permission notices."),
                when=("def:MITIRU_HAS_SPDLOG",)),
        Library("Tracy Profiler", "", "BSD-3-Clause", "https://github.com/wolfpld/tracy",
                ("file", "external/tracy/LICENSE"), when=("def:MITIRU_HAS_TRACY",)),
        Library("sentry-native", "", "MIT", "https://github.com/getsentry/sentry-native",
                ("text", "Copyright (c) 2019 Sentry (https://sentry.io) and individual contributors.\n\n" + MIT_BODY),
                when=("def:MITIRU_HAS_SENTRY",)),
        Library("Breakpad (inside sentry-native)", "", "BSD-3-Clause", "https://chromium.googlesource.com/breakpad/breakpad",
                ("text", "Copyright 2006 Google LLC\n\n" + BSD3_BODY.format(owner="Google LLC")),
                when=("def:MITIRU_HAS_SENTRY",)),
        Library("GameNetworkingSockets", "", "BSD-3-Clause", "https://github.com/ValveSoftware/GameNetworkingSockets",
                ("file", "external/GameNetworkingSockets/LICENSE"), when=("def:MITIRU_HAS_GNS",)),
        Library("Protocol Buffers (with GameNetworkingSockets)", "", "BSD-3-Clause", "https://github.com/protocolbuffers/protobuf",
                ("file", "external/protobuf/LICENSE"), when=("def:MITIRU_HAS_GNS",)),
        Library("libsodium (with GameNetworkingSockets)", "", "ISC", "https://github.com/jedisct1/libsodium",
                ("file", "external/libsodium/LICENSE"), when=("def:MITIRU_HAS_GNS",)),
        Library("Live2D Cubism Core", "", "Live2D Proprietary Software License", "https://www.live2d.com/sdk/",
                ("text", "Live2D Cubism Core is used under the Live2D Proprietary Software License Agreement.\n"
                         "https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html"),
                when=("def:MITIRU_HAS_CUBISM_CORE",)),
        Library("Live2D Cubism Native Framework", "", "Live2D Open Software License", "https://github.com/Live2D/CubismNativeFramework",
                ("text", "Live2D Cubism Native Framework is used under the Live2D Open Software License Agreement.\n"
                         "https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html"),
                when=("def:MITIRU_HAS_CUBISM_FRAMEWORK",)),
        Library("AMD FidelityFX SDK (FSR 3.1)", "1.1.4", "MIT", "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK",
                ("text", "Copyright (c) 2023-2024 Advanced Micro Devices, Inc. All rights reserved.\n\n" + MIT_BODY),
                when=("def:MITIRU_HAS_FSR3",)),
    ]),
    ("Runtime files shipped next to mitiru_host.exe", [

        Library("SDL3", "{dir:external/sdl3/SDL3-*/LICENSE.txt}", "zlib", "https://libsdl.org",
                ("file", "external/sdl3/SDL3-*/LICENSE.txt"),
                when=("file:SDL3.dll",)),
        Library("ONNX Runtime (only when onnxruntime.dll is present)", "", "MIT",
                "https://github.com/microsoft/onnxruntime",
                ("text", "Copyright (c) Microsoft Corporation\n\n" + MIT_BODY),
                when=("file:onnxruntime.dll",)),
        Library("DirectML (only when DirectML.dll is present)", "", "Microsoft Software License Terms",
                "https://www.nuget.org/packages/Microsoft.AI.DirectML",
                ("text", "DirectML.dll is redistributed under the Microsoft Software License Terms\n"
                         "for the Microsoft.AI.DirectML package."),
                when=("file:DirectML.dll",)),
        Library("Microsoft Visual C++ runtime", "14.x", "Microsoft Software License Terms",
                "https://visualstudio.microsoft.com",
                ("text", "vcruntime140*.dll / msvcp140*.dll are redistributable code under the\n"
                         "Microsoft Software License Terms for Microsoft Visual Studio.")),
    ]),
    ("Fonts (assets/fonts)", [
        Library("M PLUS Rounded 1c / IBM Plex Mono / Pacifico", "", "SIL Open Font License 1.1",
                "https://scripts.sil.org/OFL", ("file", "assets/fonts/OFL.txt")),
        Library("PixelMplus", "", "M+ FONT LICENSE", "http://itouhiro.hatenablog.com/entry/20130602/font",
                ("text", MPLUS_LICENSE),
                when=("file:assets/fonts/PixelMplus*.ttf",)),
    ]),
]


def _resolve(pattern: str) -> Path:
    hits = sorted(glob.glob(str(REPO / pattern)))
    if not hits:
        raise SystemExit(f"license source not found: {pattern} "
                         "(SDL3 は tools/fetch_sdl3.py で置いてから実行する)")
    return Path(hits[-1])


def _strip_comment(lines: list[str]) -> str:
    out = []
    for line in lines:
        s = line.rstrip()
        for prefix in (" * ", " *", "\t"):
            if s.startswith(prefix):
                s = s[len(prefix):]
                break
        out.append(s)
    return "\n".join(out)


def license_text(source: tuple) -> str:
    kind = source[0]
    if kind == "text":
        return source[1]
    path = _resolve(source[1])
    text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    if kind == "file":
        return text.strip("\n﻿")
    start = text.index(source[2])
    end = text.index(source[3], start) + len(source[3])
    return _strip_comment(text[start:end].split("\n"))


def version_of(lib: Library) -> str:
    """版の表記。"{dir:<ファイルの glob>}" はそのファイルが置かれたディレクトリ名を使う (版を手で書き換え忘れないように)。"""
    if lib.version.startswith("{dir:") and lib.version.endswith("}"):
        return _resolve(lib.version[5:-1]).parent.name
    return lib.version


def read_features(path: Path) -> set[str]:
    """compile definitions の一覧 (1 行 1 つ、";" 区切りも可) から、定義の名前の組を作る。値が 0 の定義は外す。"""
    names = set()
    for item in re.split(r"[;\n]", path.read_text(encoding="utf-8")):
        item = item.strip()
        if not item:
            continue
        name, _, value = item.partition("=")
        if value.strip() not in ("0", "OFF"):
            names.add(name.strip())
    return names


def shipped(lib: Library, features: set[str] | None, deploy_dir: Path | None) -> bool:
    """features が None (リポジトリの全部入りの表) なら常に載せる。"""
    if features is None or not lib.when:
        return True
    for c in lib.when:
        kind, _, arg = c.partition(":")
        if kind == "def" and arg in features:
            return True
        if kind == "file" and deploy_dir is not None and any(deploy_dir.glob(arg)):
            return True
    return False


def render(features: set[str] | None = None, deploy_dir: Path | None = None) -> str:
    parts = [
        "MitiruEngine - third-party notices",
        "==================================",
        "",
        "This distribution contains the following third-party software.",
        "",
        FREETYPE_CREDIT,
        "",
    ]
    for title, all_libs in SECTIONS:
        libs = [lib for lib in all_libs if shipped(lib, features, deploy_dir)]
        if not libs:
            continue
        parts += ["", "#" * 78, f"# {title}", "#" * 78]
        for lib in libs:
            version = f" {version_of(lib)}" if lib.version else ""
            parts += ["", "-" * 78, f"{lib.name}{version}", f"License: {lib.license}",
                      f"Source:  {lib.url}", "-" * 78, "", license_text(lib.source)]
    return "\n".join(parts).rstrip() + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="内容が最新か調べるだけで書かない")
    ap.add_argument("--features", type=Path, help="link した target の compile definitions の一覧。載せる物を絞る")
    ap.add_argument("--deploy-dir", type=Path, help="host の dir。exe の隣に置いた DLL と書体で載せる物を決める")
    ap.add_argument("--out", type=Path, help="書く先 (既定はリポジトリ直下)")
    args = ap.parse_args()
    if args.features is not None:
        text = render(read_features(args.features), args.deploy_dir)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8", newline="\n")
        return 0
    text = render()
    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != text:
            print(f"{OUTPUT.name} is out of date: run tools/release/gen_third_party_notices.py",
                  file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT} ({len(text.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
