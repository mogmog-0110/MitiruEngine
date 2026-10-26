"""assets/materials/*.slang を Dawn 用の WGSL にして、同じフォルダへ <name>.wgsl として書く (ADR 0100)。

    python tools/bake_materials.py <materials のフォルダ> [--slangc <slangc.exe>] [--check] [--verify] [--name <名前>...]

DX12 は .slang をそのまま陰影の PS に差して作るので、この道具は要らない。Dawn は実行時に Slang を持たない (出荷が増える)
ので、ここで shaders/dawn/forward.slang の fsMain に利用者の material() を差し込んで WGSL にしておく。
--check は書かずに、いまある .wgsl と作り直した物が同じかだけを見る (違えば 1 で終わる)。
--verify は slangc を使わず、.wgsl の先頭の行のハッシュが今の .slang と .mat.json のハッシュと合うかだけを見る (焼き忘れの検査。違えば 1)。
slangc は引数、環境変数 SLANGC、PATH の順に探す (tools/gen_dawn_shaders.py と同じ。Slang のバージョンは ADR 0087 の 10.5)。
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_dawn_shaders as gen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PRELUDE_HEADER = ROOT / "include" / "mitiru" / "render" / "UserMaterial.hpp"
MAX_PARAMS = 16
NAME_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,47}$")
HEADER_RE = re.compile(r"^// mitiru-material (\S+) source=([0-9a-f]{16})\n")


def read_prelude():
    # C++ の kUserMaterialPrelude を唯一の源にする。DX12 と同じ文字列が Slang にも入る
    text = PRELUDE_HEADER.read_text(encoding="utf-8")
    m = re.search(r'kUserMaterialPrelude = R"hlsl\((.*?)\)hlsl";', text, re.S)
    if not m:
        sys.exit(f"{PRELUDE_HEADER} に kUserMaterialPrelude が見つからない")
    return m.group(1)


def parse_desc(name, json_text):
    """UserMaterial.hpp の parseUserMaterialJson と同じ規則。lighting と param の名前だけを返す。"""
    if not json_text.strip():
        return False, []
    try:
        doc = json.loads(json_text)
    except json.JSONDecodeError as e:
        sys.exit(f"{name}.mat.json を JSON として読めない: {e}")
    if not isinstance(doc, dict):
        sys.exit(f"{name}.mat.json は JSON の object ではない")
    lighting = doc.get("lighting", "scene")
    if lighting not in ("scene", "unlit"):
        sys.exit(f'{name}.mat.json: lighting は "scene" か "unlit"')
    params = doc.get("params", [])
    if not isinstance(params, list) or len(params) > MAX_PARAMS:
        sys.exit(f"{name}.mat.json: params は {MAX_PARAMS} 個までの配列")
    names = []
    for p in params:
        pname = p.get("name") if isinstance(p, dict) else None
        if not isinstance(pname, str) or not IDENT_RE.match(pname) or pname in names:
            sys.exit(f"{name}.mat.json: params の name {pname!r} が不正か重なっている")
        names.append(pname)
    return lighting == "unlit", names


def source_hash(slang_text, json_text):
    """UserMaterial.hpp の userMaterialSourceHash と同じ FNV-1a 64 (改行は LF にそろえる)。"""
    h = 14695981039346656037
    for s in (slang_text, json_text):
        for b in s.encode("utf-8"):
            if b == 0x0D:
                continue
            h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ 0) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def param_defines(names):
    lanes = "xyzw"
    return "".join(f"#define param_{n} (mitiruUserVec({i // 4}).{lanes[i % 4]})\n" for i, n in enumerate(names))


def bake_one(slangc, prelude, slang_path):
    name = slang_path.stem
    if not NAME_RE.match(name):
        sys.exit(f"{slang_path.name}: 材質の名前は英数字と _ と - だけで 64 文字まで")
    slang_text = slang_path.read_text(encoding="utf-8")
    json_path = slang_path.with_suffix(".mat.json")
    json_text = json_path.read_text(encoding="utf-8") if json_path.exists() else ""
    unlit, names = parse_desc(name, json_text)
    user = prelude + param_defines(names) + f'#line 1 "{name}.slang"\n' + slang_text + '\n#line 1 "mitiru"\n'
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "user_material.slang").write_text(user, encoding="utf-8", newline="\n")
        out = tmp / f"{name}.wgsl"
        cmd = [slangc, str(gen.SRC / "forward.slang"), "-I", str(tmp), "-DMITIRU_USER_MATERIAL=1"]
        if unlit:
            cmd.append("-DMITIRU_USER_UNLIT=1")
        cmd += ["-entry", "fsMain", "-stage", "fragment", "-matrix-layout-column-major", "-target", "wgsl", "-o", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
        if r.returncode != 0 or not out.exists():
            sys.exit(f"{name}.slang: slangc が失敗\n{r.stderr}")
        text = out.read_text(encoding="utf-8").replace("\r\n", "\n")
    # gen_dawn_shaders.compile_one と同じ補正 (互換レベルは flat の補間に either だけを通す)。エントリポイントだけを書いた WGSL には
    # @location の無い構造体にも @interpolate(flat) が出て Tint が断るので、@location のある所だけ残す
    text = re.sub(r"@interpolate\(flat\)(\s+@location)", lambda m: "@interpolate(flat, either)" + m.group(1), text)
    text = re.sub(r"@interpolate\(flat\)\s*", "", text)
    return f"// mitiru-material {name} source={source_hash(slang_text, json_text)}\n" + text


def verify(sources):
    bad = 0
    for slang in sources:
        wgsl = slang.with_suffix(".wgsl")
        json_path = slang.with_suffix(".mat.json")
        want = source_hash(slang.read_text(encoding="utf-8"), json_path.read_text(encoding="utf-8") if json_path.exists() else "")
        m = HEADER_RE.match(wgsl.read_text(encoding="utf-8")) if wgsl.exists() else None
        if m is None:
            print(f"{wgsl.name}: 焼いていない")
            bad += 1
        elif m.group(2) != want:
            print(f"{wgsl.name}: .slang か .mat.json を書き換えた後に焼き直していない (ハッシュ {m.group(2)}、今の中身 {want})")
            bad += 1
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", type=Path)
    ap.add_argument("--slangc")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--name", action="append", default=[])
    args = ap.parse_args()
    sources = sorted(args.dir.glob("*.slang"))
    if args.name:
        sources = [s for s in sources if s.stem in args.name]
    if not sources:
        sys.exit(f"{args.dir} に焼く .slang が無い")
    if args.verify:
        return verify(sources)
    slangc = gen.find_slangc(args.slangc)
    prelude = read_prelude()
    bad = 0
    for slang in sources:
        text = bake_one(slangc, prelude, slang)
        target = slang.with_suffix(".wgsl")
        if args.check:
            have = target.read_text(encoding="utf-8").replace("\r\n", "\n") if target.exists() else ""
            if have != text:
                print(f"{target.name}: 作り直した物と違う")
                bad += 1
        else:
            target.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {target} ({len(text)} bytes)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
