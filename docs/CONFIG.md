# EngineConfig: 起動後に変えても効かない設定

`EngineConfig` (`include/mitiru/core/Config.hpp`) の大半のフィールドは実行中にいつでも
書き換えて構わない (`timeScale` / `paused` / 音量 / キーバインド等)。だが一部は
**ウィンドウ生成・device 生成・UI 文書の読み込み・リングバッファ確保のいずれかの初期化処理に
一度だけベイクされる**ため、実行中に値を変えても次回起動まで反映されない。

Unreal Insights の Memory トレースチャンネルが「プロセス起動時から有効にする必要があり、
遅延接続では割り当てイベントを取れない」と明記しているのと同じ性質の制約が、
MitiruEngine 側にも複数ある。本表はそれを一覧化したもの。

## 表: 起動時のみ確定するフィールド

| フィールド | 既定値 | 何にベイクされるか | 付け忘れたときの症状 |
|---|---|---|---|
| `gfxBackend` | `Auto` | `GfxFactory::createDevice` が起動時に 1 度だけ device を生成する | 実行中に変えても描画は既存 backend のまま (何も起きない) |
| `displayMode` | `Windowed` | ウィンドウ生成時のスタイルビット | 実行中に変えても枠は変わらない |
| `windowResizable` | `false` | `WS_THICKFRAME` / `WS_MAXIMIZEBOX` の有無 (ウィンドウ生成時) | ユーザーがリサイズできない/できてしまう状態のまま固定される |
| `headless` | `false` | ウィンドウを作るかどうかの分岐そのもの | 実行中に true へ変えても既に出ているウィンドウは消えない |
| `uiDocument` | 空 | `Engine` が起動時に 1 度だけ RmlUi を始めて RML 文書を読む | 実行中に入れても UI は出ない (mitiru_host は `assets/ui/main.rml` があれば起動時に入れる) |
| `enableHttpApi` / `httpApiPort` | `false` / `8090` | `Engine::initHttpServer` がリスンソケットを開く | 起動後に有効化しても、その回のプロセスでは API に届かない |
| `packPath` | 空 | `Engine::loadModule` が起動時に 1 度だけ参照し、DLL を展開する | 実行中に切り替えても load 済み DLL は差し替わらない |
| `timeTravelBufferFrames` | `0` (既定 300 相当) | 巻き戻しリングバッファの容量確保 (`GameMemoryRing`) | デモ中に「もっと遡りたい」と思っても既存リング容量は伸びない |
| `timeTravelBudgetBytes` | `0` (無制限) | 同上、byte 予算での容量切り詰め | 同上 |
| `bugRingSeconds` | `30.0` | 常時バグリング (P1) の容量確保 | バグを見てから記録を有効にしても、直前の状態は既に失われている |
| `skipDefaultFont` / `fontPath` | `false` / 空 | `Engine::initFont` が起動時に書体を 1 度だけ読む | 実行中に変えても既定の書体は差し替わらない (字形自体は使われた時に焼くので、範囲の指定は無い) |
| `--record` (host 引数、対応する `EngineConfig` フィールドは無い) | 無効 | `Recorder::open()` が起動直後に呼ばれる (`apps/mitiru_host/main.cpp`) | 「デモ中に異常を見つけてから録画を始める」はできない。常時バグリング (`bugRingSeconds`) が唯一の後追い手段 |

## 一覧に無いもの (実行中に変更可能)

`timeScale` / `paused` / `stepFrames` / 音量 4 種 / `layerTimeScale` / `layerFrozenByHitStop` /
`errorBannerFile` / キーバインド / `oracle*` 系の大半は `Engine::run()` のループ内で毎フレーム
読み直されるため、実行中の変更がそのフレームから反映される。迷ったら該当フィールドの
doc comment を見る — `[起動時のみ]` のマーカーが付いているものだけが本表の対象。

## 関連

- 決定論の保証範囲は `docs/DETERMINISM.md` を参照 (起動時にしか決まらない設定は
  決定論の前提条件の一部でもある)。
- `mitiru doctor` の症状表 (mitiru-cli の `internal/commands/doctor.go`) に
  「録画を後から始められない」症状を追加する余地があるが、既存の自動チェックは
  実行中プロセスの状態を検知できないため未実装 (手動診断のまま)。
