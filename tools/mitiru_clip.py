"""直近の遊び (F11 で保存した .mtrr) を、1 コマンドで動画にする。

    python tools/mitiru_clip.py <game.dll> [rec.mtrr] [-o out.mp4] [--3d] [--gif] [--host mitiru_host.exe]

rec.mtrr を省くと、カレントフォルダ、DLL のフォルダ、mitiru_host.exe のフォルダにある一番新しい bug_*.mtrr (F11 の保存) を使う。
F11 の録画は、host が起動時に cwd を exe のフォルダへ固定するので、mitiru_host.exe の隣に書かれる。
mitiru_host の --replay-test --export-video で AVI にし (docs/VIDEO_EXPORT.md)、ffmpeg が PATH にあれば mp4 (--gif で GIF も) に変える。
ffmpeg が無いときは AVI までで終わる。録画は決定論なので、同じ録画と同じ DLL からは同じ AVI ができる。
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

EXIT_USAGE = 2
EXIT_HOST = 3
EXIT_FFMPEG = 4

GIF_WIDTH = 480
GIF_FPS = 15
# 同じ ffmpeg なら同じバイト列にするため、時刻とバージョンの書き込みを切り、符号化を 1 スレッドにする
BITEXACT = ["-map_metadata", "-1", "-fflags", "+bitexact", "-flags:v", "+bitexact", "-flags:a", "+bitexact", "-threads", "1"]


def find_latest_recording(dirs):
    found = []
    for d in dirs:
        base = Path(d)
        if base.is_dir():
            found.extend(p for p in base.glob("bug_*.mtrr") if p.is_file())
    if not found:
        return None
    return max(found, key=lambda p: (p.stat().st_mtime_ns, str(p)))


def find_host(explicit, repo_root):
    if explicit:
        return Path(explicit) if Path(explicit).is_file() else None
    if os.environ.get("MITIRU_HOST"):
        p = Path(os.environ["MITIRU_HOST"])
        return p if p.is_file() else None
    for sub in ("build/apps/mitiru_host/mitiru_host.exe", "build/apps/mitiru_host/mitiru_host",
                "build/apps/mitiru_host/Debug/mitiru_host.exe", "build/apps/mitiru_host/Release/mitiru_host.exe",
                "build/apps/mitiru_host/RelWithDebInfo/mitiru_host.exe", "build-release/apps/mitiru_host/mitiru_host.exe"):
        p = Path(repo_root) / sub
        if p.is_file():
            return p
    found = shutil.which("mitiru_host")
    return Path(found) if found else None


def plan_outputs(rec, out, gif, no_mp4):
    target = Path(out) if out else Path(Path(rec).stem + ".mp4")  # 録画は host のフォルダにあるので、隣ではなくカレントフォルダへ書く
    suffix = target.suffix.lower()
    known = suffix in (".mp4", ".gif", ".avi")
    stem = str(target.with_suffix("")) if known else str(target)
    mp4 = stem + ".mp4" if (suffix == ".mp4" or not known) and not no_mp4 else None
    gif_out = stem + ".gif" if gif or suffix == ".gif" else None
    return Path(stem + ".avi"), (Path(mp4) if mp4 else None), (Path(gif_out) if gif_out else None)


def host_command(host, dll, rec, avi, three_d):
    cmd = [str(host), str(dll), "--replay-test", str(rec), "--export-video", str(avi), "--json"]
    if three_d:
        cmd += ["--headless-3d", "--backend", "dx12"]
    return cmd


def parse_verdict(stdout):
    for line in reversed(stdout.splitlines()):
        if line.startswith("{") and line.endswith("}"):
            try:
                found = json.loads(line)
            except ValueError:
                continue
            if isinstance(found, dict) and "verdict" in found:
                return found
    return None


def judge_host_result(returncode, verdict, avi_exists):
    # F11 の録画は途中の状態を持たず、host は「比べた状態が 0 件」を FAIL と言うが、AVI は書けている
    reason = (verdict or {}).get("reason", "")
    if not avi_exists:
        return False, None
    if returncode == 0:
        return True, None
    if reason == "no_frames_compared":
        return True, "録画は途中の状態を持たない (F11 の録画はこの形) ので、再生が録画のときと同じ動きかは確かめていません"
    if reason == "diverged":
        return True, "再生の途中でゲームの状態が録画からずれました。動画は録画のときと違う動きかもしれません (DLL か資産が録画のときと違う可能性)"
    return False, None


def mp4_command(ffmpeg, avi, mp4):
    # MJPEG は全レンジの YUV で出てくる。放っておくと mp4 も全レンジのままになり、ブラウザや一部のプレイヤーで色がずれる
    return [ffmpeg, "-y", "-v", "error", "-i", str(avi), "-c:v", "libx264", "-preset", "medium", "-crf", "18",
            "-vf", "scale=out_range=tv,format=yuv420p", "-c:a", "aac", "-b:a", "160k", *BITEXACT,
            "-movflags", "+faststart", str(mp4)]


def gif_command(ffmpeg, avi, gif, width=GIF_WIDTH, fps=GIF_FPS):
    # 色数が 256 なので、動画ごとに色の表を作り直す。音は入れない
    graph = (f"fps={fps},scale='min({width},iw)':-1:flags=lanczos,split[a][b];"
             "[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle")
    return [ffmpeg, "-y", "-v", "error", "-i", str(avi), "-an", "-vf", graph, "-loop", "0",
            "-fflags", "+bitexact", "-map_metadata", "-1", str(gif)]


def run(cmd, label):
    print(f"[clip] {label} を走らせます: {Path(str(cmd[0])).name}", flush=True)
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8", errors="replace")


def size_text(path):
    size = Path(path).stat().st_size
    return f"{size / (1024 * 1024):.1f} MB" if size >= 1024 * 1024 else f"{size / 1024:.0f} KB"


def parse_args(argv):
    ap = argparse.ArgumentParser(description="F11 で保存した録画 (.mtrr) を動画 (AVI、mp4、GIF) にする")
    ap.add_argument("dll", help="録画したゲームの DLL (録画と同じバージョン)")
    ap.add_argument("rec", nargs="?", help="録画 (.mtrr)。省くと一番新しい bug_*.mtrr (F11 の保存先は mitiru_host.exe の隣)")
    ap.add_argument("-o", "--out", help="出力先。拡張子 .mp4 / .gif / .avi で作る物を決める (既定: カレントフォルダに録画の名前の .mp4)")
    ap.add_argument("--gif", action="store_true", help="GIF も作る (音なし、幅 480、15 fps)")
    ap.add_argument("--no-mp4", action="store_true", help="mp4 を作らない")
    ap.add_argument("--keep-avi", action="store_true", help="mp4 / GIF にしたあとも AVI を残す")
    ap.add_argument("--3d", dest="three_d", action="store_true", help="3D と UI を GPU で描く (--headless-3d --backend dx12)")
    ap.add_argument("--host", help="mitiru_host の場所 (既定: 環境変数 MITIRU_HOST、build、PATH の順)")
    ap.add_argument("--ffmpeg", help="ffmpeg の場所 (既定: PATH)")
    return ap.parse_args(argv)


def main(argv=None):
    a = parse_args(sys.argv[1:] if argv is None else argv)
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    dll = Path(a.dll).resolve()  # host は起動時に cwd を exe のフォルダへ変えるので、相対パスは渡せない
    if not dll.is_file():
        print(f"[clip] DLL {dll} がありません。録画したゲームの DLL を指定してください", file=sys.stderr)
        return EXIT_USAGE
    host = find_host(a.host, Path(__file__).resolve().parent.parent)
    host = host.resolve() if host else None
    if host is None:
        print("[clip] mitiru_host が見つかりません。--host で場所を指すか、環境変数 MITIRU_HOST に入れてください", file=sys.stderr)
        return EXIT_USAGE
    rec = Path(a.rec).resolve() if a.rec else find_latest_recording([Path.cwd(), dll.resolve().parent, host.resolve().parent])
    if rec is None or not rec.is_file():
        where = "指定した録画" if a.rec else "カレントフォルダ、DLL のフォルダ、mitiru_host のフォルダに bug_*.mtrr"
        print(f"[clip] {where}が見つかりません。ゲームで F11 を押して保存するか、録画のパスを渡してください", file=sys.stderr)
        return EXIT_USAGE
    avi, mp4, gif = plan_outputs(rec, a.out, a.gif, a.no_mp4)
    avi, mp4, gif = (p.resolve() if p else None for p in (avi, mp4, gif))
    avi.parent.mkdir(parents=True, exist_ok=True)
    avi.unlink(missing_ok=True)  # 前回の AVI が残っていると、host が失敗しても成功に見える

    r = run(host_command(host, dll, rec, avi, a.three_d), "host")
    usable, warning = judge_host_result(r.returncode, parse_verdict(r.stdout), avi.is_file())
    if usable:
        # 動画にできたときの host の判定行 (FAIL no_frames_compared など) は紛らわしいので、host の通知だけ見せる
        for line in (r.stdout + r.stderr).splitlines():
            if line.startswith("mitiru:"):
                print(line)
    else:
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        print(f"[clip] 録画を動画にできませんでした (mitiru_host が exit {r.returncode})。DLL が録画と同じバージョンか確かめてください", file=sys.stderr)
        return EXIT_HOST
    if warning:
        print(f"[clip] 注意: {warning}", file=sys.stderr)
    print(f"[clip] AVI: {avi} ({size_text(avi)})")

    if mp4 is None and gif is None:
        return 0
    ffmpeg = shutil.which(a.ffmpeg or "ffmpeg")
    if not ffmpeg:
        print(f"[clip] ffmpeg が PATH に無いので、AVI までにしました: {avi}\n"
              "[clip] mp4 にするには ffmpeg を入れて (Windows なら winget install Gyan.FFmpeg)、同じコマンドをもう一度流してください",
              file=sys.stderr)
        return 0
    for target, command in ((mp4, mp4_command), (gif, gif_command)):
        if target is None:
            continue
        q = run(command(ffmpeg, avi, target), "ffmpeg")
        if q.returncode != 0 or not target.is_file():
            print(f"[clip] ffmpeg が {target.name} を作れませんでした (exit {q.returncode}): {q.stderr.strip()[:400]}\n"
                  f"[clip] AVI は残してあります: {avi}", file=sys.stderr)
            return EXIT_FFMPEG
        print(f"[clip] {target.suffix[1:].upper()}: {target} ({size_text(target)})")
    if not a.keep_avi:
        avi.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
