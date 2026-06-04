# Examples

MitiruEngine の挙動を最小限で見せるサンプル集。`mitiru` CLI を使わず、リポジトリのトップで CMake から直接ビルドできる:

```bash
cmake --preset default
cmake --build build --config Debug --target hello_game
build/examples/hello_game/hello_game.exe
```

`mitiru new` テンプレートが内部的に参考にしているのもこの中身。

## カタログ

| Example | 規模 | 何を見せるか |
|---|---|---|
| `hello_window` | ~20 行 | エンジン boot + ウィンドウ open + フレームループ |
| `hello_input` | ~40 行 | `input().isKeyDown / isKeyJustPressed` 系 API + 入力反応 |
| `hello_shapes` | ~50 行 | `Screen::drawRect` / `drawCircle` 等の 2D 描画プリミティブ |
| `hello_scene` | ~80 行 | scene graph / 子 entity の階層配置 + transform |
| `hello_cef_overlay` | ~80 行 | CEF を有効化して **静的な HTML/CSS HUD** を上に重ねる最小例 |
| **`hello_game`** | ~310 行 | **C++ gameplay + HTML/CSS HUD + live state push (StateStore bridge)** — engine flagship pattern showcase |

### Game-as-DLL サンプル (ADR 0005)

`mitiru_host.exe` が runtime に load する SHARED library 形式のゲーム。`MITIRU_GAME(...)` で
入口を 1 行宣言し、`Game.hpp` の薄いラッパ (`Input` / `Hud` / `Screen`) で書く。HUD は zero-JS の
`data-m-*` バインダ、効果音は `hud.play(...)` の intent。これが現行の canonical な書き方。

| Example | 何を見せるか |
|---|---|
| **`breakout`** | 旗艦サンプル。物理 / 当たり判定 / 手触り (粒子・シェイク・残像) / zero-JS HUD / 効果音を 1 本に |
| `dodge` | 初心者向け最小ゲーム (避けゲー) |
| `anchor` | 制約パズル。「触れた anchor へしか動けない」一行ルール + deterministic hazard。incubator の kept-concept を現行アーキへ port した demo |
| `showcase_platformer` | 横スクロール。replay-as-test を bit-exact で通す決定論ゲーム |

```bash
# 例: breakout を build して host で起動
cmake --build build --config Debug --target breakout
build/examples/mitiru_host/mitiru_host.exe build/examples/mitiru_host/breakout/breakout.dll
# anchor / dodge / showcase_platformer も同様 (target 名 = dll 名)
```

## 「これを見て」と言える 1 本: `hello_game`

`examples/hello_game/` が engine の **「HTML/CSS で UI が書ける C++ engine」軸 (差別化軸 1)** を最小限で見せる例:

- **C++ 側** (`main.cpp`): player / enemy / HP / timer の gameplay state を保持
- **HTML/CSS 側** (`assets/scene.html`): HP bar / 残り時間 / Game Over overlay を描画
- **bridge** (`mitiru::cef::StateStore`): C++ → JS で `view.hud.*` を push
- **逆方向**: HTML の Restart ボタンが `mitiru.dispatch('game.restart')` で C++ に届く

```
[C++ gameplay loop]                    [HTML/CSS HUD]
        │                                   │
        │  store.set("view.hud.hp", 80) ──→ │ HP bar 更新
        │  store.set("view.hud.time", 22)──→│ timer 更新
        │                                   │
        │ ←── mitiru.dispatch("game.restart")│ Restart button
        ▼                                   ▼
   gameplay 状態                         画面更新
```

これが engine の **canonical な書き方** — gameplay は C++、UI は HTML/CSS、間は signal-only bridge。

## ビルド + 実行

```bash
cmake --build build --config Debug --target hello_game
build/examples/hello_game/hello_game.exe
```

CEF runtime (`libcef.dll` + `icudtl.dat` + `MitiruCefHelper.exe` + locales 等) は `CMakeLists.txt` の POST_BUILD で自動配置される。手で何かをコピーする必要はない。

### 操作

- **矢印キー**: プレイヤー (シアン四角) を移動
- **ESC**: 終了
- 4 つの敵 (赤四角) がプレイヤーに寄ってくる。接触で HP -10、敵は 2 秒で復活
- 30 秒生存で勝利、HP=0 で敗北、いずれも Restart ボタンで再開

### Debug 操作 (`MITIRU_DEBUG=1` 環境変数 / `mitiru debug` で起動時のみ)

- **F1**: pause トグル — gameplay state freeze + 上部に `❚❚ PAUSED` overlay
- **F2**: 1 フレームだけ進める (pause 中のみ)
- 右下に **Input Monitor** パネルが常時表示:
  - 現在 held のキー (チップ表示)
  - マウス座標 + マウスボタン
  - 直近 8 件の just-pressed イベント (timestamp 付き履歴)

pause 中も draw() は継続するので、screenshot を撮るのに最適。
Input Monitor は pause 中も live で動くので、「**F1 を本当に押せてるか**」「**このキーボードでこのキーが反応するか**」を即確認できる。
これらは P2 (time-travel inspector) の最小単位でもある。

## CEF を使わない最小例

CEF を切って純 C++ で動かしたい場合は `hello_window` / `hello_input` / `hello_shapes` / `hello_scene` を参考に:

```cpp
mitiru::EngineConfig cfg;
cfg.enableCef = false;  // libcef.dll への依存も消える
```

詳細は `docs/GETTING_STARTED.md`。
