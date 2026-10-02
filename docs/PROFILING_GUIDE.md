# Profiling Guide: Tracy Zones in MitiruEngine

This guide explains how to enable Tracy profiling in MitiruEngine, capture a
live session, and interpret zones added to the hot path. The macro set lives in
[`include/mitiru/debug/TracyZones.hpp`](../include/mitiru/debug/TracyZones.hpp);
all zone macros are no-ops when Tracy is not compiled in, so instrumented
shipping builds carry zero overhead.

## Enabling Tracy

Tracy is vendored as a submodule (`external/tracy/`), but measuring is **off by
default**: with it on, every game and test would start a profiler thread and a
listening socket at launch and keep accumulating samples. Turn it on only for a
capture session:

1. Make sure the submodule is checked out
   (`git submodule update --init external/tracy`).
2. Configure with `-DMITIRU_ENABLE_TRACY=ON`. This also overrides any
   `TRACY_ENABLE` value left in an older CMake cache.
3. Rebuild — the zone macros now expand to real `ZoneScopedN` /
   `ZoneScopedNC` instances. Configure again with `-DMITIRU_ENABLE_TRACY=OFF`
   when you are done.

If `external/tracy/` is absent, CMake reports
`Tracy not found - profiling macros will be no-ops` and the build proceeds
with zero overhead.

## Capturing a Session

1. Launch the Tracy GUI (`tracy` or `Tracy.exe`) on the host machine.
2. Start the MitiruEngine application built with `MITIRU_HAS_TRACY=1`.
3. In Tracy, click **Connect** and select the running process (Tracy listens
   on `127.0.0.1:8086` by default).
4. Drive the application through the scenario you want to profile — the
   timeline view will populate with zones as they execute.
5. Stop the capture and save it to `.tracy` for later analysis.

## Known Zones

The following named zones are emitted by engine code. All zones expand to
no-ops when `MITIRU_HAS_TRACY` is undefined.

| Zone name                  | Source                                                                                | Macro used            | Notes                                                  |
| -------------------------- | -------------------------------------------------------------------------------------- | --------------------- | ------------------------------------------------------ |
| `Engine::Frame`            | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickOneFrame()`                | `MITIRU_ZONE_NAMED`   | 外側のフレームゾーン。`tickOneFrame()`全体を包む。子ゾーンはすべてこの下に並ぶ。 |
| `Engine::Input`            | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickInputPollPhase()`          | `MITIRU_ZONE_NAMED`   | `m_window->pollEvents()`と注入入力の適用。Emscripten終了判定もここ。 |
| `Engine::MouseScaling`     | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickMouseScalingPhase()`       | `MITIRU_ZONE_NAMED`   | Win32 RAWマウス座標を`Screen`論理座標へ毎フレームスケーリングする処理。 |
| `Engine::FixedUpdate`      | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickFixedUpdatePhase()`        | `MITIRU_ZONE_NAMED`   | 固定タイムステップアキュムレータループ。`game.update()`と現在シーンの`onUpdate()`を含む。 |
| `Engine::Render`           | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickRenderPhase()`             | `MITIRU_ZONE_NAMED`   | `Screen::clear`から`game.draw()`、`Scene::onDraw()`までの2D/3D描画蓄積。`device->beginFrame()`も含む。 |
| `Engine::Present`          | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickPresentPhase()`            | `MITIRU_ZONE_NAMED`   | `Screen::present()`とPostFX、3Dレンダラーの`finalizeFrame()`。GPUコマンド送信が中心。 |
| `Engine::UiComposite`      | `include/mitiru/core/detail/Engine_RmlUi.hpp`, `Engine::tickUiComposite()`             | `MITIRU_ZONE_NAMED`   | RmlUi の UI 層。RML / RCSS の読み直し、data model の更新、レイアウト、バックバッファへの描画。`assets/ui/main.rml` があるときだけ動く。 |
| `Engine::AutoCapture`      | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickAutoCaptureAndEndFrame()`  | `MITIRU_ZONE_NAMED`   | 自律テストモードのスクリーンショット保存と`device->endFrame()`。通常運用では`endFrame()`のみで軽量。 |
| `Engine::HttpPoll`         | `include/mitiru/core/detail/Engine_Frame.hpp`, `Engine::tickHttpPollAndCap()`          | `MITIRU_ZONE_NAMED`   | HTTP APIサーバーのポーリングと、vsync OFF時のフレームレートキャップ用`sleep_for`。 |
| `SmallFunction::invoke`    | `include/mitiru/time/detail/SmallFunction.hpp`, `SmallFunction::operator()()`          | `MITIRU_ZONE_NAMED`   | Wraps every call of the type-erased callable. Hot path. |
| `Sequence::action`         | `include/mitiru/time/Sequence.hpp`, `Sequence::tick()`                                | `MITIRU_ZONE_NAMED`   | One zone per action step fired inside `Sequence::tick`.  |

Search the codebase for additional zones with:

```bash
rg "MITIRU_ZONE" include/mitiru/
```

The full macro set is documented in
[`include/mitiru/debug/TracyZones.hpp`](../include/mitiru/debug/TracyZones.hpp)
and includes:

- `MITIRU_ZONE` — anonymous scoped zone (function name auto-captured).
- `MITIRU_ZONE_NAMED(name)` — named scoped zone; `name` must be a
  compile-time `const char*` literal.
- `MITIRU_ZONE_COLOR(name, color)` — named zone with an explicit color.
- `MITIRU_ZONE_RENDER / PHYSICS / AUDIO / SCRIPT / UI` — category shortcuts
  using the colors declared in `mitiru::debug::ZoneColors`.
- `MITIRU_FRAME_MARK` — frame boundary marker.
- `MITIRU_PLOT(name, value)` — numeric plot.
- `MITIRU_MESSAGE(text)` — annotation message.

## SBO vs Heap Profile Recipe

`mitiru::time::detail::SmallFunction` uses a 48-byte inline buffer (SBO);
captures larger than 48 bytes fall back to heap allocation. The
`SmallFunction::invoke` zone exposes the per-call cost of both paths in real
gameplay loads.

1. Run the game with Tracy connected; exercise the system that allocates the
   callables you care about (e.g. `Sequence` chains).
2. In Tracy, filter the **Find Zone** panel to `SmallFunction::invoke`.
3. Inspect the **Histogram** and **Time** columns:
   - Average µs near the synthetic SBO baseline → captures are inline.
   - Average µs near the synthetic heap baseline → captures are spilling.
   - Wide histogram tail → mixed populations; group by parent zone to
     identify which call sites use heap captures.
4. Cross-reference with the Catch2 benchmark numbers produced by
   [`tests/mitiru/TestSmallFunctionBench.cpp`](../tests/mitiru/TestSmallFunctionBench.cpp).
   The benchmark records SBO vs heap timings on synthetic captures; use those
   as the calibration baseline when interpreting in-app Tracy averages.
5. To zoom in on a specific timeline section, parent the
   `SmallFunction::invoke` zone under `Sequence::action` (Tracy displays the
   call hierarchy automatically), then inspect just the action-driven
   invocations.

If average µs for `SmallFunction::invoke` significantly exceeds the SBO
benchmark, capture the offending call sites' captures and consider:

- Shrinking the capture below 48 bytes (e.g. capture indices into a shared
  context object instead of large values directly).
- Splitting the callable into a smaller closure plus a lookup.

## Interpreting Engine_Frame Zones

`Engine::Frame`は外側で1フレーム全体を包むので、TracyのStatisticsビューで
これを基準に他ゾーンの相対比率を見るのがいちばん速い。以下、各ゾーンの定性的な
期待値を記す。**実機ベースラインの数値は未計測** (本ドキュメント末尾のKnown
Limitations参照)のため、ここでは「どこに重点的に時間を使っているはずか」と
いう構造的な見方だけを示す。

- **`Engine::Render`** — 通常はフレーム時間の大部分を占める。`game.draw()`と
  Sceneの`onDraw()`がここに集約されるので、コンテンツが重ければ最初に膨らむ
  のはここ。Tracyで子コール(drawSprite等)が出ない場合は`MITIRU_ZONE_NAMED`
  を計装したいdrawメソッドに足すと、その内訳まで見える。
- **`Engine::Present`** — GPUコマンド送信とPostFXチェーン。vsync ONのとき
  は`Screen::present()`ないし`device->endFrame()`でvsync待ちが入るため
  実時間が膨らみがちだが、これは「GPU待ち時間」であってエンジン側の処理コスト
  ではない。CPUの純粋なコストは`Engine::Render`を見るほうが正確。
- **`Engine::FixedUpdate`** — 固定タイムステップでは複数stepが走り得る(例:
  144Hz vsync + 60Hz update)。スパイラルオブデス防止で`kMaxFrameSkip`に
  クリップされている。長フレームの後に幅広いゾーンを見たら累積アキュムレータ
  起因。
- **`Engine::Input`** — `pollEvents`が支配的。ネイティブwindowのキューが
  大きいフレームではここが伸びる。EmscriptenではshouldClose判定もここで
  実行される。
- **`Engine::MouseScaling`** — Win32のみで意味のある軽量フェーズ。ほぼ常に
  サブマイクロ秒オーダーで完結する。`dynamic_cast<Win32Window*>`が支配的なら
  これは設計上正常(代替手段はABI変更を伴うため温存)。
- **`Engine::UiComposite`** — UI 文書が無ければ (`m_rmlUi.active()` が false) 早期 return するのでほぼ
  ゼロ。UI があるときは RmlUi の更新・レイアウト・描画がこのゾーンにまとめて乗る。
- **`Engine::AutoCapture`** — 自律テストモード以外では`device->endFrame()`
  の呼び出しだけ。通常運用では`Engine::Present`と並ぶ軽量ゾーン。
- **`Engine::HttpPoll`** — `m_httpServer`が動いていないorアイドルなら
  ほぼゼロ。vsync OFF + `targetFps>0`のフレームレートキャップが効くと
  `sleep_for`の待機時間がここに乗るので、Tracy上では「Engine::HttpPollが
  長い = 余裕でtargetFpsを達成している」と読める。

子ゾーンの合計は`Engine::Frame`よりわずかに小さくなる(helper関数呼び出し
の薄いシーケンサ部分が外側に残るため)。乖離が大きい場合は計装されていない
処理が`tickOneFrame()`内に紛れていないか確認する。

## Adding New Zones

新規ホットパスを計装したいときは、対象関数の先頭で`MITIRU_ZONE_NAMED`を呼ぶ
だけでよい。`MITIRU_HAS_TRACY`が未定義のビルドでは展開結果が`((void)0)`に
なるため、リリースビルドへの混入も気にしなくてよい。

```cpp
#include "mitiru/debug/TracyZones.hpp"

void MySystem::update(float dt) {
    MITIRU_ZONE_NAMED("MySystem::update");
    // ... per-frame work ...
}
```

カテゴリ別の色分けをしたい場合は`MITIRU_ZONE_RENDER` / `MITIRU_ZONE_PHYSICS`
/ `MITIRU_ZONE_AUDIO` / `MITIRU_ZONE_SCRIPT` / `MITIRU_ZONE_UI`のいずれかを
使うと、`mitiru::debug::ZoneColors`に定義された色が自動で割り当てられる。
完全に独自色にしたい場合は`MITIRU_ZONE_COLOR("Name", 0xRRGGBB)`を使う。

命名規約は`Subsystem::Operation`を推奨。TracyのStatisticsビューで
`Engine::*`や`MySystem::*`でフィルタしやすくするため。

## Auto-Test Capture (Env Var Hook)

エンジンの`Engine::run()`は、起動時に下記2つの環境変数を自動で読み取り、
`EngineConfig::autoTestMode`を有効化する。CIスクリーンショット / ギャラリー
キャプチャ / smokeベースライン取得のためのフックで、ソース側に変更を入れずに
任意のconsumer exeを「指定フレーム数だけ走らせてPNG保存→終了」させられる。

| 変数 | 値 | 効果 |
|------|----|------|
| `MITIRU_AUTOTEST` | `1` (または`true` / 非空 / 非`0`) | `autoTestMode = true` / `autoTestExitAfter = true` / `autoTestFrames = 120`を適用(~2秒 @ 60 fps) |
| `MITIRU_AUTOTEST_OUTPUT` | 絶対パス推奨のディレクトリ | `autoTestOutputDir`を上書き。生成物は`<dir>/auto_test.png`と`<dir>/auto_test_report.json` |

実装は [`include/mitiru/core/Config.hpp`](../include/mitiru/core/Config.hpp)
の`EngineConfig::applyAutoTestEnv()`。`Engine::run()`冒頭で自動的に呼ばれる。
プログラム側で明示的に`cfg.autoTestMode = true`を設定済みの場合はenv varが
**上書きしない** (ユーザー設定優先)。出力ディレクトリだけはenvで常に強制
上書きされる(capture scriptは`MITIRU_AUTOTEST_OUTPUT`を絶対パスでセット
することで、起動CWDが不定でも安全に成果物を回収できる)。

### 使用例

```bash
# CI / capture script から (host が DLL を load する構成):
MITIRU_AUTOTEST=1 \
MITIRU_AUTOTEST_OUTPUT=C:/build/screenshots/html_hud \
build/apps/mitiru_host/mitiru_host.exe build/apps/mitiru_host/html_hud/html_hud.dll

# 結果:
#   C:/build/screenshots/html_hud/auto_test.png
#   C:/build/screenshots/html_hud/auto_test_report.json
```

Win32 `PrintWindow`フォールバックよりもGPUコンポジット結果を正確に取得でき、
ヘッドレス(ウィンドウを出さない) CIでも動作する。

### 注意点

- **`MITIRU_AUTOTEST_OUTPUT`には絶対パスを渡すこと**。相対パスはexeの起動
  CWDに依存するため信頼できない。capture script側で常に絶対パスを生成する。
- env varフックは`EngineConfig`を経由するconsumer (`engine.run(game, cfg)`)
  にのみ作用する。Engineを使わず自前ループで回す純粋なheadlessミニexeには
  効かないため、その種のexeはstdout strategyで撮影する。
- `applyAutoTestEnv()`はテスト / 専用mainから再評価したい場合に直接呼べる。
  既定では`Engine::run()`が一度だけ呼ぶ。

## Known Limitations

- **本番ハードウェアでの計測は未実施**。Engine_Frame 9ゾーン +既存2ゾーン
  は計装済みでno-op gateも検証済みだが、実機キャプチャによるベースライン
  数値の公開は保留中。
- **GPU側のゾーンは未計装**。TracyにはGPUタイムスタンプ機構があるが、
  現状のDX11/DX12/Vulkan/OpenGLバックエンドにはまだ統合されていない。
  GPU時間を見たい場合は当面PIX / RenderDoc / NSightなどのベンダーツールを
  併用する。
- **UI の内訳は分かれていない**。`Engine::UiComposite` は RmlUi の更新・レイアウト・描画を
  1 つのゾーンで包むので、どれが重いかは見えない。

## Tracy のフレームと `.mtrr` のフレームを突き合わせる (7-1)

`MITIRU_PROFILE_FRAME()` (Tracy の `FrameMark`) が刻むフレーム境界と、`.mtrr` 録画の
`frameIdx` は**別の座標系**だった。Tracy 側は「起動してから何フレーム目か」、`.mtrr` 側は
「録画開始から何フレーム目か」で、両者を突き合わせる手段が無く、「この録画の 3,241 フレーム目が
遅かった」を Tracy のタイムラインで見ることができなかった。

`--record` で録画している間だけ、`replay::Recorder::record()` が
`mitiru::debug::TracyHelper::tagMtrrFrame(frameIdx)` を呼び、Tracy へ 2 つの手がかりを残す:

- `TracyPlot("mtrr.frame", frameIdx)` — 数値グラフとして時系列に残る。Tracy の **Plot** ビューで
  `mtrr.frame` を選ぶと、タイムライン上の任意の位置がどの `.mtrr` フレーム番号に対応するか読める。
- `TracyMessage("mtrr=<n>")` — その瞬間のタイムラインへテキストとして刻まれる。**Messages** パネル
  に一覧が並ぶので、目的のフレーム番号を検索して該当時刻へジャンプできる。

非録画時 (`--record` を付けていない起動) はこの呼び出し自体が起きないため no-op。
Tracy 非対応ビルド (`MITIRU_HAS_TRACY` 未定義) でも `tagMtrrFrame` は no-op に展開される。

`.mtrr` 側からも Tracy の有無が分かるようにしてある。`apps/mitiru_host/main.cpp` の
`buildRecordEnvTag` が envTag を `<backend>|<arch>|tracy` または `<backend>|<arch>|no-tracy`
の形式で書く (`Recorder::writeEnvTag`)。録画ファイルだけを見て「そもそも Tracy 計装ビルドで
録ったか」が分かる。

### 突き合わせを確認する手順

1. `MITIRU_HAS_TRACY=1` でビルドした host で Tracy GUI を接続する。
2. `mitiru_host <dll> --record capture.mtrr` で数秒プレイして終了する。
3. `Player::open("capture.mtrr")` 等で `recordedEnvTag()` を読み、末尾が `|tracy` であることを
   確認する (`|no-tracy` ならこの手順自体が無意味な組み合わせ)。
4. Tracy GUI の **Plot** ビューで `mtrr.frame` を開く。値が単調増加していれば録画区間全体が
   計装されている。
5. 遅かった `.mtrr` フレーム番号 (例: `Player`/`--replay-test` の出力や `mitiru hunt` の
   レポートから分かった値) を **Messages** パネルで `mtrr=3241` のように検索し、該当行の時刻を
   タイムラインへジャンプする。
6. その時刻の `Engine::Frame` ゾーンを展開すれば、`docs/PROFILING_GUIDE.md` の
   「Interpreting Engine_Frame Zones」節の内訳がそのまま使える。

### 意図的にやらないこと

`TRACY_ON_DEMAND` (起動時からの常時記録をやめ、プロファイラ接続時のみ記録するモード) は既定で
有効にしない。記帳コストそのものは決定論オラクルの対象外 (画面/GameMemory を変えない) だが、
フレーム時間スパイクを検出するオラクル (`oracleStagnantSeconds` 等) を接続タイミング依存で
誤発火させうるため、opt-in のまま据え置く。

## See Also

- [`include/mitiru/debug/TracyZones.hpp`](../include/mitiru/debug/TracyZones.hpp)
  — full macro reference and category colors.
- [`include/mitiru/debug/TracyIntegration.hpp`](../include/mitiru/debug/TracyIntegration.hpp)
  — engine-level `MITIRU_PROFILE_*` helpers (frame markers, plots).
- [`tests/mitiru/TestSmallFunctionBench.cpp`](../tests/mitiru/TestSmallFunctionBench.cpp)
  — Catch2 synthetic benchmark for SBO vs heap.
- [`tests/mitiru/TestSmallFunctionTracy.cpp`](../tests/mitiru/TestSmallFunctionTracy.cpp)
  — regression test ensuring the zone macros stay no-op-safe.
- [`include/mitiru/replay/Recorder.hpp`](../include/mitiru/replay/Recorder.hpp)
  — `.mtrr` format; `record()` calls `tagMtrrFrame` during recording (7-1).
- [`docs/DETERMINISM.md`](DETERMINISM.md) — what determinism guarantees do and do not cover.
