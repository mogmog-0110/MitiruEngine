#!/usr/bin/env python3
"""Mitiru CLI - AI自動化ツール for Mitiru game engine.

AIエージェントがMitiruゲームエンジンと対話するためのコマンドラインツール。
CMakeビルド、ヘッドレス実行、スナップショット取得、スクリーンショット保存、
入力注入、ヘルスチェック、バリデーション、リプレイ再生をサポートする。

Usage:
    mitiru-cli build [--config Debug|Release]
    mitiru-cli run --headless --frames N [--game <path>]
    mitiru-cli snapshot --port PORT
    mitiru-cli screenshot --port PORT --output <path.png>
    mitiru-cli inject --port PORT --key <keycode>
    mitiru-cli inject --port PORT --mouse <x,y>
    mitiru-cli inject --port PORT --click <x,y>
    mitiru-cli health --port PORT
    mitiru-cli validate --port PORT
    mitiru-cli replay --port PORT --file <replay.json>
"""

import argparse
import json
import subprocess
import sys
import urllib.error
import urllib.request


def cmd_build(args):
    """CMakeでゲームをビルドする。"""
    config = args.config or "Debug"

    # CMake設定がまだの場合は実行
    if args.configure:
        configure_result = subprocess.run(
            ["cmake", "--preset", "default"],
            capture_output=True,
            text=True,
        )
        if configure_result.returncode != 0:
            print(configure_result.stderr, file=sys.stderr)
            sys.exit(1)
        print(configure_result.stdout)

    # ビルド実行
    result = subprocess.run(
        ["cmake", "--build", "build", "--config", config],
        capture_output=True,
        text=True,
    )
    print(result.stdout)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(1)
    print(f"Build succeeded ({config})")


def cmd_run(args):
    """ゲームをヘッドレスモードで実行する。"""
    cmd = []
    if args.game:
        cmd.append(args.game)
    else:
        print("Error: --game <executable path> is required", file=sys.stderr)
        sys.exit(1)

    if args.headless:
        cmd.append("--headless")
    if args.frames:
        cmd.extend(["--frames", str(args.frames)])
    if args.port:
        cmd.extend(["--port", str(args.port)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    print(result.stdout)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode)


def cmd_snapshot(args):
    """実行中エンジンからJSONスナップショットを取得する。"""
    resp = _http_get(args.port, "/snapshot")
    print(json.dumps(json.loads(resp), indent=2))


def cmd_screenshot(args):
    """実行中エンジンからスクリーンショットをキャプチャする。"""
    data = _http_get_bytes(args.port, "/screenshot")
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"Screenshot saved to {args.output} ({len(data)} bytes)")


def cmd_inject(args):
    """入力コマンドを注入する。"""
    if args.key:
        payload = json.dumps({"type": "key_down", "keyCode": int(args.key)})
    elif args.key_up:
        payload = json.dumps({"type": "key_up", "keyCode": int(args.key_up)})
    elif args.mouse:
        x, y = args.mouse.split(",")
        payload = json.dumps({"type": "mouse_move", "x": float(x), "y": float(y)})
    elif args.click:
        x, y = args.click.split(",")
        payload = json.dumps({"type": "click", "x": float(x), "y": float(y)})
    else:
        print("Error: --key, --key-up, --mouse, or --click required", file=sys.stderr)
        sys.exit(1)

    resp = _http_post(args.port, "/input", payload)
    print(resp)


def cmd_health(args):
    """ヘルスメトリクスを取得する。"""
    resp = _http_get(args.port, "/health")
    print(json.dumps(json.loads(resp), indent=2))


def cmd_validate(args):
    """不変条件バリデーションを実行する。"""
    resp = _http_get(args.port, "/validate")
    result = json.loads(resp)
    print(json.dumps(result, indent=2))

    # 失敗がある場合は非ゼロ終了
    if isinstance(result, list):
        failures = [r for r in result if not r.get("passed", True)]
        if failures:
            sys.exit(1)


def cmd_replay(args):
    """リプレイファイルを送信して再生する。"""
    with open(args.file, "r", encoding="utf-8") as f:
        replay_data = f.read()

    resp = _http_post(args.port, "/replay", replay_data)
    print(resp)


def cmd_inspect(args):
    """タグ指定で状態を問い合わせる。"""
    tag = args.tag or ""
    resp = _http_get(args.port, f"/inspect?tag={tag}")
    print(json.dumps(json.loads(resp), indent=2))


def cmd_step(args):
    """指定フレーム数だけゲームを進める。"""
    frames = args.frames or 1
    payload = json.dumps({"frames": frames})
    resp = _http_post(args.port, "/step", payload)
    print(resp)


# ─── HTTPユーティリティ ───


def _http_get(port, path):
    """GETリクエストを送信しレスポンスボディを返す。"""
    url = f"http://localhost:{port}{path}"
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            return resp.read().decode()
    except urllib.error.URLError as e:
        print(f"Error connecting to localhost:{port}: {e}", file=sys.stderr)
        sys.exit(1)


def _http_get_bytes(port, path):
    """GETリクエストを送信しバイナリレスポンスを返す。"""
    url = f"http://localhost:{port}{path}"
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            return resp.read()
    except urllib.error.URLError as e:
        print(f"Error connecting to localhost:{port}: {e}", file=sys.stderr)
        sys.exit(1)


def _http_post(port, path, body):
    """POSTリクエストを送信しレスポンスボディを返す。"""
    url = f"http://localhost:{port}{path}"
    req = urllib.request.Request(
        url,
        data=body.encode(),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.read().decode()
    except urllib.error.URLError as e:
        print(f"Error connecting to localhost:{port}: {e}", file=sys.stderr)
        sys.exit(1)


# ─── メイン ───


def main():
    parser = argparse.ArgumentParser(
        description="Mitiru CLI - AI automation tool for Mitiru game engine"
    )
    subparsers = parser.add_subparsers(dest="command")

    # build
    p_build = subparsers.add_parser("build", help="Build the game using CMake")
    p_build.add_argument("--config", default="Debug", help="Build configuration")
    p_build.add_argument(
        "--configure", action="store_true", help="Run cmake configure first"
    )

    # run
    p_run = subparsers.add_parser("run", help="Run game in headless mode")
    p_run.add_argument("--headless", action="store_true", help="Run headless")
    p_run.add_argument("--frames", type=int, help="Number of frames to run")
    p_run.add_argument("--game", help="Path to game executable")
    p_run.add_argument("--port", type=int, help="Observer server port")

    # snapshot
    p_snap = subparsers.add_parser("snapshot", help="Get JSON state snapshot")
    p_snap.add_argument("--port", type=int, required=True, help="Observer port")

    # screenshot
    p_ss = subparsers.add_parser("screenshot", help="Capture screenshot")
    p_ss.add_argument("--port", type=int, required=True, help="Observer port")
    p_ss.add_argument("--output", required=True, help="Output file path")

    # inject
    p_inj = subparsers.add_parser("inject", help="Inject input command")
    p_inj.add_argument("--port", type=int, required=True, help="Observer port")
    p_inj.add_argument("--key", default=None, help="Key code to press")
    p_inj.add_argument("--key-up", default=None, help="Key code to release")
    p_inj.add_argument("--mouse", default=None, help="Mouse position (x,y)")
    p_inj.add_argument("--click", default=None, help="Click position (x,y)")

    # health
    p_health = subparsers.add_parser("health", help="Get health metrics")
    p_health.add_argument("--port", type=int, required=True, help="Observer port")

    # validate
    p_val = subparsers.add_parser("validate", help="Run invariant validation")
    p_val.add_argument("--port", type=int, required=True, help="Observer port")

    # replay
    p_replay = subparsers.add_parser("replay", help="Play back a replay file")
    p_replay.add_argument("--port", type=int, required=True, help="Observer port")
    p_replay.add_argument("--file", required=True, help="Path to replay JSON file")

    # inspect
    p_inspect = subparsers.add_parser("inspect", help="Inspect state by tag")
    p_inspect.add_argument("--port", type=int, required=True, help="Observer port")
    p_inspect.add_argument("--tag", default="", help="Tag prefix to filter")

    # step
    p_step = subparsers.add_parser("step", help="Advance game by N frames")
    p_step.add_argument("--port", type=int, required=True, help="Observer port")
    p_step.add_argument("--frames", type=int, default=1, help="Number of frames")

    args = parser.parse_args()

    commands = {
        "build": cmd_build,
        "run": cmd_run,
        "snapshot": cmd_snapshot,
        "screenshot": cmd_screenshot,
        "inject": cmd_inject,
        "health": cmd_health,
        "validate": cmd_validate,
        "replay": cmd_replay,
        "inspect": cmd_inspect,
        "step": cmd_step,
    }

    if args.command in commands:
        commands[args.command](args)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
