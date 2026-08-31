# AIワークフロー: AIエージェントがゲームを観測・検証する

MitiruEngineは「AIがコードを書き、実行結果をAI自身が観て、直し、正しさを機械的に検証する」
ループをエンジン標準機能として提供する。状態が、ポインタも`std::vector`も持たない
丸ごとコピーできる1個のstruct (= flat POD、[FLAT_POD.md](FLAT_POD.md))に
集約され、実行が決定論的だからできること。

## 有効化(zero-config)

環境変数`MITIRU_AI=1`を立ててゲームを起動するだけ。hostが`127.0.0.1:8090`
(変更は`MITIRU_AI_PORT`)でHTTP APIをlistenする。

```bat
set MITIRU_AI=1
mitiru run
```

起動するとstderrに`[ai] HTTP API listening on 127.0.0.1:8090`が出る。

## 観測API一覧

| エンドポイント | 内容 |
|---|---|
| `GET /api/ai/state` | ゲームの全状態(1個のstruct)の全フィールドを構造化したJSON (`MITIRU_REFLECT`宣言時) |
| `GET /api/ai/diff?from=N&to=M` | リング内2フレーム間の状態差分 |
| `POST /api/ai/branch` | 反実仮想実行 —「この状態からNフレーム別入力なら?」 |
| `GET /api/ai/frame` | **draw list** (何をどこに描いたか +テキスト内容) +縮小screenshot |
| `GET /api/ai/audio?max=N` | 最近の音イベント(SE/BGMの再生・停止・pitch) |
| `GET /api/screenshot?width=W` | 画面PNG。HTML/CSS HUDも合成済み |
| `GET /api/scene/tree` | シーンツリーJSON |
| `POST /api/input/simulate` | 入力注入(AIがゲームを操作する) |
| `POST /api/runtime/pause` / `step` / `timescale` | ポーズ・コマ送り・倍速 |
| `GET /api/health` | 生存確認(frame番号 +経過秒) |

### /api/ai/frame: 画面の「意味」を読む

screenshotのピクセルから座標を推測する必要はない。draw listが
「どのAPIで・どの矩形に・何を」描いたかを返す。

```json
{"frameNumber":8652,"screen":{"width":1280,"height":720},
 "drawCalls":[
   {"call":"drawRect","x":24,"y":24,"w":360,"h":22},
   {"call":"drawRect","x":24,"y":24,"w":284.4,"h":22},
   {"call":"drawText","x":398,"y":22,"w":110,"h":22,"text":"HP 79"},
   {"call":"drawCircle","x":618,"y":338,"w":44,"h":44}],
 "screenshot":{"width":640,"height":360,"pngBase64":"..."}}
```

注意:
- 初回呼び出しで記録が有効化される(エントリは次フレームから)。空配列が返ったら
  1フレーム以上待って再取得する。
- 1フレーム上限1024エントリ。`?screenshot=0`でPNGを省くと軽い。
- テキストは先頭47 byteまで記録される。

## MCPサーバー(Claude Code等から直接使う)

`mitiru mcp`が上記APIをMCPツールとして公開する。`.mcp.json`:

```json
{"mcpServers": {"mitiru": {"command": "mitiru", "args": ["mcp"]}}}
```

ツール: `game_state` / `state_diff` / `frame` / `screenshot` / `audio_log` /
`simulate_input` / `pause` / `step` / `timescale` / `scene_tree` / `verify`

## mitiru verify: 修正の機械的検証

「直した → 正しく動く」をコマンド1発で判定する。

```bat
mitiru verify                          # build + 起動 + screenshot 取得
mitiru verify --golden ref.png         # golden 画像と画素比較
mitiru verify --replay session.mtrr    # リプレイの bit-exact 検証も実行
```

stdoutにJSON判定1個(`{"build":"ok","verdict":"pass",...}`)、
exit codeが結果(0=pass / 1=fail / 2=build error)。CIにそのまま置ける。

## 典型ループ

1. AIがコードを修正 → `mitiru verify`でbuild +起動
2. `frame`で画面の意味を読み、`game_state`で内部状態と突き合わせる
3. ズレていれば`state_diff` / `branch`で原因フレームを特定して修正
4. `mitiru verify --replay`で1 bitも違わず一致するか(bit-exact)を、`--golden`で正解として保存した基準画像(golden)と見た目が一致するかを機械検証

録画(`mitiru run --record`) → 修正 → リプレイ検証の流れは
[GETTING_STARTED.md](GETTING_STARTED.md)も参照。
