#pragma once

#include <mitiru/core/InlineMacro.hpp>

/// @file Engine.hpp
/// @brief Mitiruエンジン本体
/// @details プラットフォーム・ウィンドウ・GPUデバイスを統合し、
///          ゲームループを実行するメインクラス。
///
/// @note platform 別の機能対応状況:
///
///   Windows のみ (DX11/DX12 backend 必須):
///     - PostProcess pipeline (PostProcessIntegration)
///     - DX12 Renderer3D (Renderer3D_DX12)
///     - Live2D Cubism SDK (precompiled x64 library)
///     - Win32 audio output (WaveAudioEngine, Win32AudioOutput)
///
///   Cross-platform (全 desktop + Emscripten):
///     - OpenGL backend (GlDevice) -- SDL2 または GLFW 必須
///     - Vulkan backend (VulkanDevice) -- GLFW 必須
///     - WebGL2 backend -- Emscripten のみ
///     - Null backend (NullDevice) -- headless/test
///     - GLFW window (GlfwWindow) -- Linux/macOS/Windows
///     - SDL2 window (Sdl2Window) -- Linux/macOS/Windows
///     - Software audio (SoftAudioEngine) -- 全 platform
///     - miniaudio -- 全 desktop platform (Emscripten 除く)
///
///   Cross-platform 化を予定している差し替え対象:
///     - NanoVG UI -- editor UI
///     - OpenGL PostProcess -- DX11 専用 pipeline を置き換える予定

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/core/Clock.hpp>
#include <mitiru/core/GameSettings.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/gfx/GfxFactory.hpp>
#include <mitiru/render/TrueTypeScreenRenderer.hpp>
#include <mitiru/render/SdfFont.hpp>
#include <mitiru/vn/TrueTypeFont.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>
#include <mitiru/render/Renderer3D.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>
#ifdef _WIN32
#include <mitiru/gfx/dx12/Dx12LoFiTarget.hpp>
#endif
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/input/InputRecorder.hpp>
#include <mitiru/input/InputReplayer.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/GamepadInput.hpp>
#include <mitiru/input/SdlGamepadInput.hpp>
#include <mitiru/util/ImageWriter.hpp>
#include <mitiru/observe/Snapshot.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/platform/WindowFactory.hpp>
#include <mitiru/ecs/MitiruWorld.hpp>
#include <mitiru/scene/MitiruScene.hpp>
#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/platform/headless/HeadlessPlatform.hpp>

// 前方宣言 -- raw/shared pointer で保持し、Engine からは method を呼ばない
namespace mitiru::validate { class TemporalInvariantChecker; }
namespace mitiru::observe { class StructuredDiff; class CausalChain; }
// ModuleHost は pimpl 方式: 完全型は mitiru/module/ModuleHost.hpp に置く
// (<windows.h> を引き込む)。WIN32 macro 汚染を実際の host code
// (Engine_Module.hpp + tests) のみに限定するため Engine.hpp consumer からは隠す。
namespace mitiru::module { class ModuleHost; }
// StateStore は mitiru/cef/StateStore.hpp に置く (nlohmann/json を引き込む)。
// module-mode で Engine が 1 個所有する (ADR 0005)。pimpl で Engine.hpp を軽く保つ。
namespace mitiru::cef { class StateStore; }
// IAudioEngine は applyVolumes() で setVolume() を呼ぶため完全型が必要
#include <mitiru/audio/AudioEngine.hpp>
namespace mitiru::render { class PostProcessManager; }

#include <mitiru/server/EngineHttpServer.hpp>
#include <mitiru/server/EngineHttpBridge.hpp>

#ifdef _WIN32
#include <mitiru/platform/win32/Win32Platform.hpp>
#include <mitiru/render/Renderer3D_DX12.hpp>
#include <mitiru/render/PostProcessIntegration.hpp>
#endif

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)
#include <mitiru/cef/MitiruCefContext.hpp>
using CefContext = mitiru::cef::MitiruCefContext;
#else
#include <mitiru/cef/NullCefContext.hpp>
using CefContext = mitiru::cef::NullCefContext;
#endif

#ifdef MITIRU_HAS_OPENGL
#include <mitiru/platform/sdl2/Sdl2Window.hpp>
#endif

#ifdef MITIRU_HAS_GLFW
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <mitiru/platform/emscripten/EmscriptenPlatform.hpp>
#endif

namespace mitiru
{

/// @brief Mitiruエンジン本体
/// @details ゲームループの実行・フレーム制御・スクリーンショット等を提供する。
///          メソッド本体は include/mitiru/core/detail/Engine_*.hpp に分割されている
///          (ヘッダー800行上限を遵守するため)。
class Engine
{
public:
	/// @brief デフォルトコンストラクタ
	Engine() = default;

	/// @brief デストラクタ (CEF + HTTP サーバーシャットダウンを保証する)
	~Engine();

	/// @brief CEF コンテキスト取得 (初期化前は isInitialized()==false)
	[[nodiscard]] CefContext* cefContext() noexcept;

	/// @brief エンジンを初期化しゲームループを実行する
	/// @param game ゲームインスタンス
	/// @param configIn エンジン設定
	void run(Game& game, const EngineConfig& configIn = {});

	/// @brief 指定フレーム数だけバッチ実行する (ヘッドレス用)
	/// @param game ゲームインスタンス
	/// @param frameCount 実行フレーム数
	/// @param config エンジン設定
	void stepFrames(Game& game, std::uint64_t frameCount,
	                const EngineConfig& config = {});

	/// @brief スクリーンショットを取得する
	/// @return RGBA8形式のピクセルデータ
	/// @details ソフトウェアフレームバッファが有効な場合はそちらを優先返却する。
	[[nodiscard]] std::vector<std::uint8_t> capture() const;

	/// @brief capture() が返す pixel buffer の幅 (px)
	[[nodiscard]] int captureWidth() const noexcept
	{
		if (m_window) { return m_window->width(); }
		if (m_screen) { return m_screen->width(); }
		return 0;
	}

	/// @brief capture() が返す pixel buffer の高さ (px)
	[[nodiscard]] int captureHeight() const noexcept
	{
		if (m_window) { return m_window->height(); }
		if (m_screen) { return m_screen->height(); }
		return 0;
	}

	/// @brief GPU付きでNフレーム実行し、スクリーンショットをキャプチャする
	/// @param game ゲームインスタンス
	/// @param frameCount 実行フレーム数
	/// @param config エンジン設定 (headless=falseでGPU使用)
	/// @return キャプチャしたピクセルデータ (RGBA8)
	[[nodiscard]] std::vector<std::uint8_t> runAndCapture(
		Game& game, int frameCount, const EngineConfig& config = {});

	/// @brief ECSワールドを設定する
	/// @param world MitiruWorldへのポインタ (nullptrで解除)
	void setWorld(ecs::MitiruWorld* world) noexcept;

	/// @brief シーンマネージャーを設定する
	/// @param mgr MitiruSceneManagerへのポインタ (nullptrで解除)
	void setSceneManager(scene::MitiruSceneManager* mgr) noexcept;

	/// @brief オーディオエンジンを設定する
	/// @param engine オーディオエンジンの共有ポインタ
	void setAudioEngine(std::shared_ptr<audio::IAudioEngine> engine) noexcept;

	/// @brief オーディオエンジンを取得する
	/// @return オーディオエンジンへのポインタ (未設定ならnullptr)
	[[nodiscard]] audio::IAudioEngine* audioEngine() noexcept;

	// ── ランタイム時間制御 (debug toggle) ───────────────────────────
	// 内部状態は EngineConfig 側に置く。host から host hotkey で叩く。
	void setPaused(bool p) noexcept    { mutableConfig().paused = p; }
	void togglePaused() noexcept       { auto& c = mutableConfig(); c.paused = !c.paused; }
	[[nodiscard]] bool isPaused() const noexcept { return config().paused; }
	/// 次の 1 フレームだけ通常 dt で進める (paused 中のステップ実行用)。
	void stepOneFrame() noexcept       { ++mutableConfig().stepFrames; }
	void setTimeScale(float s) noexcept { mutableConfig().timeScale = s; }
	[[nodiscard]] float timeScale() const noexcept { return config().timeScale; }

	// lo-fi post-FX (ADR #30: シーン毎の hi-res / lofi 切替):
	void setLofiEnabled(bool e) noexcept { mutableConfig().loFi.enabled = e; }
	void toggleLofi() noexcept           { auto& c = mutableConfig(); c.loFi.enabled = !c.loFi.enabled; }
	[[nodiscard]] bool isLofiEnabled() const noexcept { return config().loFi.enabled; }

	// -- 標準ゲーム音量 API --
	// マスター音量を変えると全体に反映される。BGM/SE/Voice は将来の bus 分離用
	// (現在は IAudioEngine が単一 bus のみ -> master * category を engine に渡す)

	/// @brief マスター音量を設定する (0.0-1.0)
	void setMasterVolume(float v) noexcept;
	void setBgmVolume(float v) noexcept;
	void setSeVolume(float v) noexcept;
	void setVoiceVolume(float v) noexcept;

	[[nodiscard]] float masterVolume() const noexcept;
	[[nodiscard]] float bgmVolume()    const noexcept;
	[[nodiscard]] float seVolume()     const noexcept;
	[[nodiscard]] float voiceVolume()  const noexcept;

	/// @brief 現在の EngineConfig を参照取得 (settings UI からの読み書き用)
	[[nodiscard]] const EngineConfig& config() const noexcept;

	/// @brief 設定を変更可能な参照として取得 (settings UI 専用)
	/// @details 書き込み後は saveSettings() を呼ぶか、persistSettings 有効時は
	///          自動保存させたいなら setMasterVolume 等の dedicated API を使うこと
	[[nodiscard]] EngineConfig& mutableConfig() noexcept;

	/// @brief 現在の m_config を settings.json に永続化する
	/// @return persistSettings が無効なら false (no-op)
	bool saveSettings() noexcept;

	void setTemporalChecker(validate::TemporalInvariantChecker* checker) noexcept;
	void setDiffTracker(observe::StructuredDiff* tracker) noexcept;
	void setCausalChain(observe::CausalChain* chain) noexcept;

	/// @brief ポストプロセスマネージャーを取得する
	/// @return PostProcessManagerへのポインタ (未初期化時・非Win32ではnullptr)
	[[nodiscard]] render::PostProcessManager* postProcess() noexcept;

	/// @brief ポストプロセスマネージャーを取得する (const版)
	[[nodiscard]] const render::PostProcessManager* postProcess() const noexcept;

	/// @brief シーン状態のJSONスナップショットを取得する
	/// @return JSON文字列
	[[nodiscard]] std::string snapshot() const;

	/// @brief エンジン停止を要求する
	void requestStop() noexcept;

	/// @brief フルスクリーン/ウィンドウを切り替える (Win32 のみ)
	void setFullscreen(bool enable) noexcept;

	/// @brief 現在フルスクリーンかどうか (Win32 のみ)
	[[nodiscard]] bool isFullscreen() const noexcept;

	/// @brief 現在のフレーム番号を取得する
	[[nodiscard]] std::uint64_t frameNumber() const noexcept;

	/// @brief 入力インジェクターへの参照を取得する
	/// @return InputInjector への参照
	[[nodiscard]] InputInjector& inputInjector() noexcept;

	/// @brief 入力レコーダーへの参照を取得する (axis 4)
	/// @details MITIRU_RECORD env var が設定されている場合、Engine::run() が
	///          beginRecording() を自動で呼ぶ。手動 begin / end / saveToFile
	///          したい場合はこの accessor 経由で操作する。
	[[nodiscard]] InputRecorder& inputRecorder() noexcept;
	[[nodiscard]] const InputRecorder& inputRecorder() const noexcept;

	/// @brief 入力リプレイヤーへの参照を取得する (axis 4)
	/// @details MITIRU_REPLAY=<path> が設定されている場合、Engine::run() が
	///          loadFromFile + load を自動で行い、各フレームの記録済み
	///          コマンドを injector 経由で apply する。
	[[nodiscard]] InputReplayer& inputReplayer() noexcept;
	[[nodiscard]] const InputReplayer& inputReplayer() const noexcept;

	/// @brief 現在の入力状態を取得する
	[[nodiscard]] const InputState& inputState() const noexcept;

	/// @brief スクリーンへの参照を取得する (初期化後のみ有効)
	/// @return Screen へのポインタ (未初期化時は nullptr)
	[[nodiscard]] const Screen* screen() const noexcept;

	/// @brief HTTP APIサーバーを取得する
	/// @return EngineHttpServerへのポインタ (未初期化時はnullptr)
	[[nodiscard]] server::EngineHttpServer* httpServer() noexcept;

	/// @brief HTTP APIサーバーにコマンドシステムを接続する
	/// @param cmd CommandSystemへのポインタ
	void setCommandSystem(CommandSystem* cmd) noexcept;

	/// @brief ゲームフラグを設定する (HTTP API経由で取得可能)
	/// @param key フラグ名
	/// @param value フラグ値
	void setGameFlag(const std::string& key, const std::string& value);

	/// @brief ゲームフラグを取得する
	/// @param key フラグ名
	/// @return フラグ値 (存在しない場合は空文字列)
	[[nodiscard]] std::string getGameFlag(const std::string& key) const;

	/// @brief スクリーンへの非const参照を取得する (テスト・デバッグ用)
	/// @return Screen へのポインタ (未初期化時は nullptr)
	[[nodiscard]] Screen* screen() noexcept;

	/// @brief クロックへの参照を取得する (初期化後のみ有効)
	/// @return Clock へのポインタ (未初期化時は nullptr)
	[[nodiscard]] const Clock* clock() const noexcept;

	// ── Module loading (v0.2.0 step 2 — game-as-DLL hot reload foundation) ──
	// Game DLL を host process に load して main loop から callback 駆動する。
	// 既存の `run(Game&, cfg)` 経路と並走する別エントリ。step 7 で exe-モードを
	// 廃止した時点で `runModule` が一級市民になる。

	/// @brief Game DLL を一度だけ load する。memory pointer は first-time のため null 渡し
	/// @param modulePath 元 DLL の path (rebuild される側)。実際に LoadLibrary するのは
	///        %TEMP% に copy された別 file
	/// @return 成功なら true。失敗時は内部 ModuleHost::lastError() を参照する
	/// @details
	///   - すでに module が load 済みなら false (先に `unloadModule()` を呼ぶこと)
	///   - 成功時、`mitiru_module_load` を invoke して ModuleApi + memory pointer を
	///     populate する。version mismatch は失敗扱い
	///   - 直後に `api.on_init` が non-null なら呼び出す
	bool loadModule(const std::filesystem::path& modulePath);

	/// @brief 現在 load 済みの module を unload する。call safe (未 load なら no-op)
	/// @details on_shutdown を呼び、FreeLibrary し、callback table を null 化する。
	///          memory は破棄しない (reload で再利用される; 完全に手放すには
	///          `discardModuleMemory()` を別途呼ぶこと — v0.2.0 では未提供)
	void unloadModule() noexcept;

	/// @brief 現在の module を unload して新しい (or 同じ) path から再 load
	/// @param modulePath 新 DLL の path
	/// @return 成功なら true。失敗時 module は unloaded 状態に置かれる
	/// @details
	///   - 既存 memory pointer は新 DLL に渡される (state 維持)
	///   - rebuild 直後の DLL を hot-swap するための主用途
	bool reloadModule(const std::filesystem::path& modulePath);

	/// @brief module を load してから main loop を回す統合エントリ
	/// @param modulePath 元 DLL の path
	/// @param configIn engine 設定
	/// @details
	///   - 内部で stack-local Game adapter を立て、`run(adapter, config)` を呼ぶ
	///   - ループ終了後 `unloadModule()` を呼ぶ
	///   - module load 失敗時は stderr に理由を出して false を返す (engine は init しない)
	/// @return 成功で true。MITIRU_GAME 入口無し等の load 失敗で false → host は非ゼロ終了を。
	bool runModule(const std::filesystem::path& modulePath,
	               const EngineConfig& configIn = {});

	/// @brief 現在 module が load されているか
	[[nodiscard]] bool hasModule() const noexcept;

	/// @brief 現在 active な ModuleApi callback table (read-only)
	[[nodiscard]] const module::ModuleApi& moduleApi() const noexcept;

	/// @brief module の persistent memory pointer (DLL が自分で `new` したもの)
	[[nodiscard]] void* moduleMemory() const noexcept;

	/// @brief DLL が申告した GameMemory のバイト数 (ADR 0013、0=未申告)
	[[nodiscard]] std::uint32_t moduleMemorySize() const noexcept;

	/// @brief time-travel: ring に貯めた N フレーム前の GameMemory bytes を取得 (ADR 0017)
	/// @param offsetFromNewest 0 = 最新, 1 = 1 フレーム前, ...。範囲外は nullptr。
	[[nodiscard]] const std::uint8_t* moduleMemoryRingAt(std::size_t offsetFromNewest) const noexcept;

	/// @brief time-travel ring が現在保持しているフレーム数 (ADR 0017)
	[[nodiscard]] std::size_t moduleMemoryRingSize() const noexcept;

	/// @brief time-travel rewind: live GameMemory を過去 bytes で memcpy 上書きする (ADR 0017)
	/// @details host が scrub command を受けて呼ぶ。size が GameMemory サイズと一致しない /
	///          live が無い場合は false (live を壊さない)。game DLL は rewind を知らない
	///          — 次フレームの on_update が復元された state を「現在」として淡々と進める。
	/// @return 上書きに成功したら true
	bool rewindModuleMemory(const void* bytes, std::uint32_t size) noexcept;

	/// @brief 反実仮想フォーク (ADR 0018): 現 GameMemory を保存 → 台本 input で on_update を
	///        frameCount 回 headless 実行 (draw / intents drain なし = 副作用ゼロ) → 結果を
	///        reflected JSON 文字列で返す → GameMemory を保存値へ復元する。
	/// @details AI の「この入力を続けたらどうなる?」を実際に試して比較できる。決定論は
	///          各 inputs[i].rngSeed で制御。GameMemory が単一 flat-POD + on_update が純関数
	///          だから副作用なく試行→復元できる (ADR 0017 の配当)。reflection 未宣言なら "{}"。
	/// @param inputs     frameCount 個の InputSnapshot 台本
	/// @param frameCount 進めるフレーム数
	[[nodiscard]] std::string branchModuleMemory(const module::InputSnapshot* inputs, int frameCount);

	/// @brief module-mode で engine 所有の CEF StateStore (lazy created)
	/// @details CEF init 後 + module load 後にのみ non-null。ADR 0005 により
	///          DLL は直接これに触らず、`FrameIntents::statePushes` 経由で
	///          host に push を依頼する。
	[[nodiscard]] cef::StateStore* moduleStateStore() noexcept;

private:
	// ── Module-mode per-frame helper 群 (runModule 内の ModuleAdapter が呼ぶ) ──
	// detail/Engine_Module.hpp で out-of-class 定義する。
	void ensureModuleCefBindings();        ///< CEF 準備完了後に StateStore + SharedSnapshot を遅延生成
	void buildModuleInputSnapshot();       ///< m_inputState + action queue から m_moduleInputSnapshot を構築
	void zeroModuleFrameIntents();         ///< 各 on_update 呼び出し前に m_moduleFrameIntents をクリア
	void drainModuleFrameIntents();        ///< on_update 後に DLL が要求した side-effect を適用
	void recordModuleMemoryFrame();        ///< on_update 後に GameMemory bytes を time-travel ring へ push (ADR 0017)

	/// @brief 1フレーム分のゲームループを実行する
	/// @details run()から呼ばれる。Emscriptenではemscripten_set_main_loop_argのコールバック。
	void tickOneFrame();

	/// @brief 入力ポーリングと注入入力の反映を行う
	/// @return ループ続行可能なら true、Emscripten で main loop が cancel されたら false
	/// @details false 戻り時は tickOneFrame() を即座に return する想定。
	[[nodiscard]] bool tickInputPollPhase();

	/// @brief マウス座標を Screen 論理座標にスケーリングする
	/// @details Win32Window が保持する RAW 座標から毎フレーム再計算する。
	void tickMouseScalingPhase();

	/// @brief 固定タイムステップ更新ループを実行する
	/// @details clock->tick()、accumulator 加算、game.update / scene.onUpdate の固定回数実行。
	void tickFixedUpdatePhase();

	/// @brief 2D/3D 描画コマンドを Screen / Renderer3D に積み上げる
	/// @details Screen クリア、device->beginFrame / beginPostProcess、game.draw、scene.onDraw。
	void tickRenderPhase();

	/// @brief 2D を GPU へ送信し、PostFX 終端と 3D finalize を行う
	/// @details m_device 非保持時は何もしない。endFrame は本フェーズでは呼ばない。
	void tickPresentPhase();

	/// @brief CEF UI レイヤーの message loop / input 反映 / 合成を行う
	/// @details m_cefContext.isInitialized() が false なら no-op。
	void tickCefComposite();

	/// @brief 自律テストキャプチャと device->endFrame() を実行する
	/// @return ループ続行可能なら true、autoTestExitAfter による早期終了なら false
	/// @details false 戻り時は tickOneFrame() を即座に return する想定。
	[[nodiscard]] bool tickAutoCaptureAndEndFrame();

	/// @brief HTTP API サーバーをポーリングし、フレームレートキャップで sleep する
	/// @param frameStart このフレーム開始時刻 (tickOneFrame 頭で取得した値)
	void tickHttpPollAndCap(const std::chrono::steady_clock::time_point& frameStart);

	/// 音量を [0,1] にクランプ
	[[nodiscard]] static float clampVol(float v) noexcept;

	/// audio engine に master*bgm を流す (最も一般的な BGM 音量)
	void applyVolumes() noexcept;

	/// 永続化が有効なら settings.json に書き出す
	void persistIfEnabled() noexcept;

	/// @brief エンジン内部を初期化する
	/// @param config 設定
	void initialize(const EngineConfig& config);

	/// @brief CEF サブシステムを初期化する (DX12 バックエンド限定)
	/// @details DX12 デバイスでなければ no-op。失敗しても UI 無しで続行する。
	void initializeCef(const EngineConfig& config);

	/// @brief レンダリングパイプラインを構築してScreenに接続する
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @details Backend-specific dispatch is delegated to
	///          render::createPipeline2DFor (no dynamic_cast in Engine).
	void createRenderPipeline(int screenWidth, int screenHeight);

	/// @brief ウィンドウリサイズ時のハンドラ
	/// @param w 新しいクライアント領域幅
	/// @param h 新しいクライアント領域高さ
	/// @details スワップチェーンのResizeBuffers、Screenサイズ更新、
	///          RenderPipeline再構築を行う。
	void onWindowResize(int w, int h);

	/// @brief TTFフォントを自動検索・読み込み・Screen接続する
	/// @param userPath ユーザー指定パス (空の場合自動検索)
	void initFont(const std::string& userPath);

	/// @brief SDFフォントアトラスを構築する
	void initSdfFont(std::vector<std::uint8_t> fontData);

	/// @brief テキスト内の全コードポイントがSDFアトラスに含まれるか判定する
	[[nodiscard]] bool sdfContainsAll(std::string_view text) const
	{
		if (!m_sdfAtlas) { return false; }

		render::sdf_detail::Utf8Decoder dec(text);
		while (dec.hasNext())
		{
			const std::uint32_t cp = dec.next();
			if (cp == '\n' || cp == '\r' || cp == '\t' || cp == ' ') { continue; }
			if (m_sdfAtlas->findGlyph(cp) == nullptr) { return false; }
		}
		return true;
	}

	/// @brief ビューポートをウィンドウ実サイズに同期する
	void syncViewport();

	/// @brief 3Dレンダラーを初期化する (DX11/DX12検出時)
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	/// @details Backend-specific dispatch is delegated to
	///          render::createRenderer3DFor (no dynamic_cast in Engine).
	void create3DRenderer(int screenWidth, int screenHeight);

	/// @brief HTTP APIサーバーを初期化する
	/// @param port リッスンポート番号
	void initHttpServer(int port, Game& game);

	/// @brief 自律テストモード: スクリーンショットをPNG形式でファイルに保存する
	/// @param outputDir 出力先ディレクトリ
	void saveAutoTestScreenshot(const std::string& outputDir);

	/// @brief 自律テストモード: テスト結果レポートをJSONファイルに書き出す
	/// @param outputDir 出力先ディレクトリ
	/// @param config エンジン設定
	void saveAutoTestReport(const std::string& outputDir, const EngineConfig& config);

	/// @brief RGBA8ピクセルデータをBMPファイルとして保存する (外部依存なし)
	/// @param path ファイルパス
	/// @param pixels RGBA8ピクセルデータ
	/// @param w 画像幅
	/// @param h 画像高さ
	static void saveBmp(const std::string& path, const std::vector<std::uint8_t>& pixels,
		int w, int h);

	/// @brief RGBA8ピクセルデータをPNGファイルとして保存する (外部依存なし・非圧縮PNG)
	static void savePng(const std::string& path, const std::vector<std::uint8_t>& pixels,
		int w, int h);

	/// @brief 注入された入力コマンドを適用する
	void applyInjectedInput();

	EngineConfig m_config;                          ///< エンジン設定
	std::unique_ptr<IPlatform> m_platform;          ///< プラットフォーム
	std::unique_ptr<IWindow> m_window;              ///< ウィンドウ
	std::unique_ptr<gfx::IDevice> m_device;         ///< GPUデバイス
	std::unique_ptr<Clock> m_clock;                 ///< ゲームクロック
	std::unique_ptr<Screen> m_screen;               ///< 描画サーフェス
	std::unique_ptr<render::RenderPipeline2D> m_renderPipeline; ///< 2Dレンダリングパイプライン
#ifdef _WIN32
	std::unique_ptr<gfx::Dx12LoFiTarget> m_loFiTarget; ///< ローファイ・ポストFX（DX12のみ・config で opt-in）
#endif
	int m_logicalWidth = 0;                          ///< Screen論理幅 (layout()で決定、固定)
	int m_logicalHeight = 0;                         ///< Screen論理高さ (layout()で決定、固定)
	// Debug overlay 用データ
	float m_dbgWindowW = 0, m_dbgWindowH = 0;
	float m_dbgScreenW = 0, m_dbgScreenH = 0;
	float m_dbgRawMx = 0, m_dbgRawMy = 0;
	float m_dbgPostPollMx = 0, m_dbgPostPollMy = 0;
	float m_dbgScaledMx = 0, m_dbgScaledMy = 0;
	std::unique_ptr<vn::TrueTypeFont> m_ttfFont;     ///< TTFフォント (Engine管理・SDF非対応グリフのフォールバック)
	std::unique_ptr<render::SdfFontAtlas> m_sdfAtlas; ///< SDFフォントアトラス
	render::SdfTextRenderer m_sdfRenderer;            ///< SDFテキストレンダラー
	std::unique_ptr<render::IRenderer3D> m_renderer3D;            ///< 3Dレンダラー (DX11/DX12統一)
	std::shared_ptr<render::PostProcessManager> m_postProcess;   ///< ポストプロセスマネージャー (Win32のみ生成)
	InputInjector m_inputInjector;                   ///< 入力インジェクター
	InputState m_inputState;                         ///< 現在の入力状態
#ifdef _WIN32
	GamepadInput m_gamepad;                          ///< XInput ゲームパッド (module InputSnapshot へ供給, #12)
#endif
	input::SdlGamepadInput m_sdlGamepad;             ///< SDL_GameController (DS4/DS5 等、#32)。SDL2 無し時は no-op stub
	InputRecorder m_inputRecorder;                   ///< 決定論的リプレイ用入力レコーダー (axis 4)
	InputReplayer m_inputReplayer;                   ///< 決定論的リプレイ用入力再生器 (axis 4)
	std::string m_recordOutputPath;                  ///< MITIRU_RECORD で設定: 終了時にここへ ReplayData を保存
	bool m_replayActive = false;                     ///< MITIRU_REPLAY で設定: 入力イベントを replayer から injection
	ecs::MitiruWorld* m_world = nullptr;             ///< ECSワールド (非所有)
	scene::MitiruSceneManager* m_sceneManager = nullptr; ///< シーンマネージャー (非所有)
	validate::TemporalInvariantChecker* m_temporalChecker = nullptr; ///< 時系列不変条件チェッカー (非所有)
	observe::StructuredDiff* m_diffTracker = nullptr;   ///< 構造化差分トラッカー (非所有)
	observe::CausalChain* m_causalChain = nullptr;      ///< 因果チェーン (非所有)
	std::unique_ptr<server::EngineHttpServer> m_httpServer; ///< 組み込みHTTP APIサーバー
	std::shared_ptr<audio::IAudioEngine> m_audioEngine;  ///< オーディオエンジン (オプション)

	/// 標準ゲーム音量 (0.0-1.0)
	float m_masterVolume = 1.0f;
	float m_bgmVolume    = 0.8f;
	float m_seVolume     = 1.0f;
	float m_voiceVolume  = 1.0f;
	CefContext m_cefContext;                              ///< CEF 統合ファサード (Win32+DX12 のみ実体、他は no-op)
	std::map<std::string, std::string> m_gameFlags;  ///< ゲームフラグストア (HTTP API用)
	std::set<std::string> m_spawnedToolKeys;         ///< 既に開いたツール窓 (tool|args) — 重複 spawn 防止
	std::atomic<bool> m_shouldStop{false};            ///< 停止要求フラグ (スレッドセーフ)
	bool m_initialized = false;                      ///< 初期化済みフラグ
	int m_pendingResizeW = 0;                        ///< modal-loop drag 中の defer 先 (WM_EXITSIZEMOVE で flush)
	int m_pendingResizeH = 0;
	int m_autoTestFrameCount = 0;                    ///< 自律テストモード用フレームカウンタ
	bool m_autoTestCaptured = false;                 ///< 自律テスト用キャプチャ完了フラグ
	Game* m_loopGame = nullptr;                      ///< tickOneFrame()用ゲーム参照
	const EngineConfig* m_loopConfig = nullptr;      ///< tickOneFrame()用設定参照

	/// 固定タイムステップアキュムレータ
	static constexpr float kFixedDt = 1.0f / 60.0f;  ///< 固定タイムステップ (秒)
	static constexpr int kMaxFrameSkip = 5;           ///< 最大フレームスキップ数
	static constexpr float kMaxDelta = 0.1f;          ///< スパイラルオブデス防止キャップ
	float m_accumulator = 0.0f;                       ///< 固定タイムステップ用アキュムレータ

	// ── Module (game-as-DLL) state ──────────────────────────────────────────
	// Pimpl: ModuleHost は <windows.h> を include する。unique_ptr の裏に隠して
	// Engine.hpp の transitive include set から除外する。
	std::unique_ptr<module::ModuleHost>   m_moduleHost;
	module::ModuleApi                     m_moduleApi{};            ///< zero-init: load まで全 callback は null
	void*                                 m_moduleMemory = nullptr; ///< DLL 所有の game state (engine は解放しない)
	std::uint32_t                         m_moduleMemorySize = 0;   ///< DLL 申告の GameMemory バイト数 (ADR 0013、0=未申告)
	observe::GameMemoryRing               m_moduleMemoryRing;       ///< 過去フレームの GameMemory bytes (軸② time-travel、ADR 0017)

	// host→DLL signal flow 用の per-frame POD scratch buffer。struct 合計が
	// ~50KB ある上、module を一切 load しない Engine instance まで肥大化させたく
	// ないので heap に (遅延) 確保する。
	std::unique_ptr<module::InputSnapshot> m_moduleInputSnapshot;
	std::unique_ptr<module::FrameIntents>  m_moduleFrameIntents;

	// Module-mode の CEF StateStore + Inspector SharedSnapshot — DLL が host
	// object の pointer を一切持たないよう engine 所有とする (ADR 0005)。
	std::unique_ptr<cef::StateStore>        m_moduleStateStore;
	std::unique_ptr<observe::SharedSnapshot> m_moduleInspectorSnapshot;
	// 直近に書き出した inspector export 内容の FNV-1a hash。同一なら parse+rebuild+
	// disk-write を丸ごと省く (inspector は同じ内容を読み続けるので観測結果は不変)。
	std::uint64_t                            m_lastInspectorDigest = 0;

	// host 所有の観察 (perf / audio) を game inspectable と併記して書くためのキャッシュ。
	// game export とは別 cadence (常時変化) なので throttle write する (ADR 0014 tool windows:
	// mitiru_perf / mitiru_mixer が同じ SharedSnapshot を読む)。
	cef::json                                m_lastInspectorOut = cef::json::object();
	bool                                     m_inspectorDirty = false;
	int                                      m_toolWriteAccum = 0;
	std::chrono::steady_clock::time_point    m_lastPerfTp{};
	bool                                     m_havePerfTp = false;
	float                                    m_emaFps = 0.0f;       ///< 平滑 update fps
	float                                    m_lastFrameMs = 0.0f;

	// 次の on_update 向けに queue した CEF JS 由来の action event。StateStore の
	// handler は CEF UI thread で発火するが on_update は engine main thread で
	// 走るため mutex で保護する。
	struct ModuleActionEventBuffer
	{
		std::mutex                                  mu;
		std::vector<std::pair<std::string, std::string>> events; // (name, payloadJson)
	};
	std::unique_ptr<ModuleActionEventBuffer> m_moduleActionEvents;
};

} // namespace mitiru

// -- Detail headers (out-of-class method definitions) ----------------------
// header-only mode (MITIRU_HEADER_ONLY=1) ではここで include し、Engine.hpp を
// include する全 TU が inline 定義を得られるようにする。
// STATIC mode (MITIRU_HEADER_ONLY 未定義) では detail header を src/core/Engine.cpp
// 内で 1 度だけ compile する -- ここで include すると ODR 違反になる。
#if defined(MITIRU_HEADER_ONLY)
#include <mitiru/core/detail/Engine_Accessors.hpp>
#include <mitiru/core/detail/Engine_Audio.hpp>
#include <mitiru/core/detail/Engine_Settings.hpp>
#include <mitiru/core/detail/Engine_Init_Font.hpp>
#include <mitiru/core/detail/Engine_Init_Input.hpp>
#include <mitiru/core/detail/Engine_Init_Lifecycle.hpp>
#include <mitiru/core/detail/Engine_Init_Pipeline.hpp>
#include <mitiru/core/detail/Engine_Window.hpp>
#include <mitiru/core/detail/Engine_Snapshot.hpp>
#include <mitiru/core/detail/Engine_AutoTest.hpp>
#include <mitiru/core/detail/Engine_Http.hpp>
#include <mitiru/core/detail/Engine_Run.hpp>
#include <mitiru/core/detail/Engine_Frame.hpp>
// CEF detail は無条件に include する。method 本体が自前の
// #if defined(_WIN32) && defined(MITIRU_HAS_CEF) guard を持つ (上の Engine.hpp
// 既存 CefContext typedef guard と一致) ため、全 platform で安全に compile される。
#include <mitiru/core/detail/Engine_Cef.hpp>
// Module loader detail (loadModule / unloadModule / reloadModule / runModule)。
// Windows では <windows.h> を持ち込む ModuleHost.hpp を引き込むため、macro 汚染を
// この include の transitive set に閉じ込めるべく最後に置く。
#include <mitiru/core/detail/Engine_Module.hpp>
#endif // MITIRU_HEADER_ONLY
