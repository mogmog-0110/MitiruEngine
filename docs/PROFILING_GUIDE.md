# Profiling Guide — Tracy Zones in MitiruEngine

This guide explains how to enable Tracy profiling in MitiruEngine, capture a
live session, and interpret zones added to the hot path. The macro set lives in
[`include/mitiru/debug/TracyZones.hpp`](../include/mitiru/debug/TracyZones.hpp);
all zone macros are no-ops when Tracy is not compiled in, so instrumented
shipping builds carry zero overhead.

## Enabling Tracy

Tracy support is auto-detected by the root `CMakeLists.txt`. The build enables
Tracy only when the vendored source is present:

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/tracy/public/TracyClient.cpp")
    option(TRACY_ENABLE "" ON)
    add_subdirectory(external/tracy)
    target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_TRACY=1)
    target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} TracyClient)
endif()
```

To enable Tracy for a local capture session:

1. Place a working Tracy checkout under `external/tracy/` (the directory must
   contain `public/TracyClient.cpp`). Most users vendor the
   [tracy](https://github.com/wolfpld/tracy) repository directly.
2. Re-run CMake configuration. The status output should say
   `Tracy found - enabling profiler`.
3. Rebuild — `MITIRU_HAS_TRACY=1` is now defined for engine targets, and the
   zone macros expand to real `ZoneScopedN` / `ZoneScopedNC` instances.

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

| Zone name                  | Source                                                      | Macro used            | Notes                                                  |
| -------------------------- | ----------------------------------------------------------- | --------------------- | ------------------------------------------------------ |
| `Engine::Frame`            | `include/mitiru/core/detail/Engine_Frame.hpp:32`            | `MITIRU_ZONE_NAMED`   | 外側のフレームゾーン。`tickOneFrame()` 全体を包む。子ゾーンはすべてこの下に並ぶ。 |
| `Engine::Input`            | `include/mitiru/core/detail/Engine_Frame.hpp:54`            | `MITIRU_ZONE_NAMED`   | `m_window->pollEvents()` と注入入力の適用。Emscripten 終了判定もここ。 |
| `Engine::MouseScaling`     | `include/mitiru/core/detail/Engine_Frame.hpp:83`            | `MITIRU_ZONE_NAMED`   | Win32 RAW マウス座標を `Screen` 論理座標へ毎フレームスケーリングする処理。 |
| `Engine::FixedUpdate`      | `include/mitiru/core/detail/Engine_Frame.hpp:125`           | `MITIRU_ZONE_NAMED`   | 固定タイムステップアキュムレータループ。`game.update()` と現在シーンの `onUpdate()` を含む。 |
| `Engine::Render`           | `include/mitiru/core/detail/Engine_Frame.hpp:161`           | `MITIRU_ZONE_NAMED`   | `Screen::clear` から `game.draw()`、`Scene::onDraw()` までの 2D/3D 描画蓄積。`device->beginFrame()` も含む。 |
| `Engine::Present`          | `include/mitiru/core/detail/Engine_Frame.hpp:206`           | `MITIRU_ZONE_NAMED`   | `Screen::present()` と PostFX、3D レンダラーの `finalizeFrame()`。GPU コマンド送信が中心。 |
| `Engine::CefComposite`     | `include/mitiru/core/detail/Engine_Frame.hpp:245`           | `MITIRU_ZONE_NAMED`   | CEF UI レイヤーのメッセージループ処理、入力転送、テクスチャアップロード、バックバッファ合成。HTML UI 構成 (CEF あり) 専用。 |
| `Engine::AutoCapture`      | `include/mitiru/core/detail/Engine_Frame.hpp:271`           | `MITIRU_ZONE_NAMED`   | 自律テストモードのスクリーンショット保存と `device->endFrame()`。通常運用では `endFrame()` のみで軽量。 |
| `Engine::HttpPoll`         | `include/mitiru/core/detail/Engine_Frame.hpp:311`           | `MITIRU_ZONE_NAMED`   | HTTP API サーバーのポーリングと、vsync OFF 時のフレームレートキャップ用 `sleep_for`。 |
| `SmallFunction::invoke`    | `include/mitiru/time/detail/SmallFunction.hpp:79`           | `MITIRU_ZONE_NAMED`   | Wraps every call of the type-erased callable. Hot path. |
| `Sequence::action`         | `include/mitiru/time/Sequence.hpp:75`                       | `MITIRU_ZONE_NAMED`   | One zone per action step fired inside `Sequence::tick`.  |

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

`Engine::Frame` は外側で 1 フレーム全体を包むので、Tracy の Statistics ビューで
これを基準に他ゾーンの相対比率を見るのがいちばん速い。以下、各ゾーンの定性的な
期待値を記す。**実機ベースラインの数値は未計測** (本ドキュメント末尾の Known
Limitations 参照) のため、ここでは「どこに重点的に時間を使っているはずか」と
いう構造的な見方だけを示す。

- **`Engine::Render`** — 通常はフレーム時間の大部分を占める。`game.draw()` と
  Scene の `onDraw()` がここに集約されるので、コンテンツが重ければ最初に膨らむ
  のはここ。Tracy で子コール (drawSprite 等) が出ない場合は `MITIRU_ZONE_NAMED`
  を計装したい draw メソッドに足すと深掘りできる。
- **`Engine::Present`** — GPU コマンド送信と PostFX チェーン。vsync ON のとき
  は `Screen::present()` ないし `device->endFrame()` で vsync 待ちが入るため
  実時間が膨らみがちだが、これは「GPU 待ち時間」であってエンジン側の処理コスト
  ではない。CPU の純粋なコストは `Engine::Render` を見るほうが正確。
- **`Engine::FixedUpdate`** — 固定タイムステップでは複数 step が走り得る (例:
  144Hz vsync + 60Hz update)。スパイラルオブデス防止で `kMaxFrameSkip` に
  クリップされている。長フレームの後に幅広いゾーンを見たら累積アキュムレータ
  起因。
- **`Engine::Input`** — `pollEvents` が支配的。ネイティブ window のキューが
  大きいフレームではここが伸びる。Emscripten では shouldClose 判定もここで
  実行される。
- **`Engine::MouseScaling`** — Win32 のみで意味のある軽量フェーズ。ほぼ常に
  サブマイクロ秒オーダーで完結する。`dynamic_cast<Win32Window*>` が支配的なら
  これは設計上正常 (代替手段は ABI 変更を伴うため温存)。
- **`Engine::CefComposite`** — HTML UI 構成専用。`m_cefContext.isInitialized()`
  が false の場合は早期 return するため、native 構成 (CEF なし) の純ネイティブ運用ではほぼ
  ゼロ。HTML UI 構成でも CEF が dirty frame を持たない静的画面ではアップロードが
  スキップされ軽量。
- **`Engine::AutoCapture`** — 自律テストモード以外では `device->endFrame()`
  の呼び出しだけ。通常運用では `Engine::Present` と並ぶ軽量ゾーン。
- **`Engine::HttpPoll`** — `m_httpServer` が動いていない or アイドルなら
  ほぼゼロ。vsync OFF + `targetFps>0` のフレームレートキャップが効くと
  `sleep_for` の待機時間がここに乗るので、Tracy 上では「Engine::HttpPoll が
  長い = 余裕で targetFps を達成している」と読める。

子ゾーンの合計は `Engine::Frame` よりわずかに小さくなる (helper 関数呼び出し
の薄いシーケンサ部分が外側に残るため)。乖離が大きい場合は計装されていない
処理が `tickOneFrame()` 内に紛れていないか確認する。

## Adding New Zones

新規ホットパスを計装したいときは、対象関数の先頭で `MITIRU_ZONE_NAMED` を呼ぶ
だけでよい。`MITIRU_HAS_TRACY` が未定義のビルドでは展開結果が `((void)0)` に
なるため、リリースビルドへの混入も気にしなくてよい。

```cpp
#include "mitiru/debug/TracyZones.hpp"

void MySystem::update(float dt) {
    MITIRU_ZONE_NAMED("MySystem::update");
    // ... per-frame work ...
}
```

カテゴリ別の色分けをしたい場合は `MITIRU_ZONE_RENDER` / `MITIRU_ZONE_PHYSICS`
/ `MITIRU_ZONE_AUDIO` / `MITIRU_ZONE_SCRIPT` / `MITIRU_ZONE_UI` のいずれかを
使うと、`mitiru::debug::ZoneColors` に定義された色が自動で割り当てられる。
完全に独自色にしたい場合は `MITIRU_ZONE_COLOR("Name", 0xRRGGBB)` を使う。

命名規約は `Subsystem::Operation` を推奨。Tracy の Statistics ビューで
`Engine::*` や `MySystem::*` でフィルタしやすくするため。

## Auto-Test Capture (Env Var Hook)

エンジンの `Engine::run()` は、起動時に下記 2 つの環境変数を自動で読み取り、
`EngineConfig::autoTestMode` を有効化する。CI スクリーンショット / ギャラリー
キャプチャ / smoke ベースライン取得のためのフックで、ソース側に変更を入れずに
任意の consumer exe を「指定フレーム数だけ走らせて PNG 保存→終了」させられる。

| 変数 | 値 | 効果 |
|------|----|------|
| `MITIRU_AUTOTEST` | `1` (または `true` / 非空 / 非 `0`) | `autoTestMode = true` / `autoTestExitAfter = true` / `autoTestFrames = 120` を適用 (~2 秒 @ 60 fps) |
| `MITIRU_AUTOTEST_OUTPUT` | 絶対パス推奨のディレクトリ | `autoTestOutputDir` を上書き。生成物は `<dir>/auto_test.png` と `<dir>/auto_test_report.json` |

実装は [`include/mitiru/core/Config.hpp`](../include/mitiru/core/Config.hpp)
の `EngineConfig::applyAutoTestEnv()`。`Engine::run()` 冒頭で自動的に呼ばれる。
プログラム側で明示的に `cfg.autoTestMode = true` を設定済みの場合は env var が
**上書きしない** (ユーザー設定優先)。出力ディレクトリだけは env で常に強制
上書きされる (capture script は `MITIRU_AUTOTEST_OUTPUT` を絶対パスでセット
することで、起動 CWD が不定でも安全に成果物を回収できる)。

### 使用例

```bash
# CI / capture script から:
MITIRU_AUTOTEST=1 \
MITIRU_AUTOTEST_OUTPUT=C:/build/screenshots/cef_minimal \
build/examples/cef_minimal/Debug/mitiru_cef_minimal.exe

# 結果:
#   C:/build/screenshots/cef_minimal/auto_test.png
#   C:/build/screenshots/cef_minimal/auto_test_report.json
```

`tools/site/capture_examples.py` の `auto-test` strategy がこのフックを
使って 4 つの CEF サンプル (`cef_minimal` / `cef_overlay` / `cef_state_bridge`
/ `cef_transition`) を撮影する。Win32 `PrintWindow` フォールバックよりも
GPU コンポジット結果を正確に取得でき、ヘッドレス (ウィンドウを出さない) CI でも動作する。

### 注意点

- **`MITIRU_AUTOTEST_OUTPUT` には絶対パスを渡すこと**。相対パスは exe の起動
  CWD に依存するため信頼できない。capture script 側で常に絶対パスを生成する。
- env var フックは `EngineConfig` を経由する consumer (`engine.run(game, cfg)`)
  にのみ作用する。`examples/cpp_gameplay_minimal` のように Engine を使わない
  純粋な headless ミニ exe には効かないため、これらは引き続き stdout strategy
  で撮影する。
- `applyAutoTestEnv()` はテスト / 専用 main から再評価したい場合に直接呼べる。
  既定では `Engine::run()` が一度だけ呼ぶ。

## Known Limitations

- **本番ハードウェアでの計測は未実施**。Engine_Frame 9 ゾーン + 既存 2 ゾーン
  は計装済みで no-op gate も検証済みだが、実機キャプチャによるベースライン
  数値の公開は保留中。
- **GPU 側のゾーンは未計装**。Tracy には GPU タイムスタンプ機構があるが、
  現状の DX11/DX12/Vulkan/OpenGL バックエンドにはまだ統合されていない。
  GPU 時間を見たい場合は当面 PIX / RenderDoc / NSight などのベンダーツールを
  併用する。
- **HTML UI 構成 (CEF) のレンダリングは外部プロセスで動く**。`Engine::CefComposite`
  はホスト側のアップロード/コンポジットしか見えないので、CEF サブプロセス内の
  HTML レイアウトコストはこの計装からは可視化できない。Chrome DevTools の
  Performance パネルで別途プロファイルすること。

## See Also

- [`include/mitiru/debug/TracyZones.hpp`](../include/mitiru/debug/TracyZones.hpp)
  — full macro reference and category colors.
- [`include/mitiru/debug/TracyIntegration.hpp`](../include/mitiru/debug/TracyIntegration.hpp)
  — engine-level `MITIRU_PROFILE_*` helpers (frame markers, plots).
- [`tests/mitiru/TestSmallFunctionBench.cpp`](../tests/mitiru/TestSmallFunctionBench.cpp)
  — Catch2 synthetic benchmark for SBO vs heap.
- [`tests/mitiru/TestSmallFunctionTracy.cpp`](../tests/mitiru/TestSmallFunctionTracy.cpp)
  — regression test ensuring the zone macros stay no-op-safe.
