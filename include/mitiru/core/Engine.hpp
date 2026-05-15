#pragma once

#include <mitiru/core/InlineMacro.hpp>

/// @file Engine.hpp
/// @brief Mitiruエンジン本体
/// @details プラットフォーム・ウィンドウ・GPUデバイスを統合し、
///          ゲームループを実行するメインクラス。
///
/// @note Platform-specific feature availability:
///
///   Windows only (requires DX11/DX12 backend):
///     - PostProcess pipeline (PostProcessIntegration)
///     - DX12 Renderer3D (Renderer3D_DX12)
///     - Live2D Cubism SDK (precompiled x64 library)
///     - Win32 audio output (WaveAudioEngine, Win32AudioOutput)
///
///   Cross-platform (all desktop + Emscripten):
///     - OpenGL backend (GlDevice) -- requires SDL2 or GLFW
///     - Vulkan backend (VulkanDevice) -- requires GLFW
///     - WebGL2 backend -- Emscripten only
///     - Null backend (NullDevice) -- headless/test
///     - GLFW window (GlfwWindow) -- Linux/macOS/Windows
///     - SDL2 window (Sdl2Window) -- Linux/macOS/Windows
///     - Software audio (SoftAudioEngine) -- all platforms
///     - miniaudio -- all desktop platforms (not Emscripten)
///
///   Planned cross-platform replacements:
///     - NanoVG UI -- editor UI
///     - OpenGL PostProcess -- planned to replace DX11-only pipeline

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <fstream>
#include <map>
#include <memory>
#include <string>
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
#include <mitiru/input/InputInjector.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/util/ImageWriter.hpp>
#include <mitiru/observe/Snapshot.hpp>
#include <mitiru/platform/WindowFactory.hpp>
#include <mitiru/ecs/MitiruWorld.hpp>
#include <mitiru/scene/MitiruScene.hpp>
#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/IWindow.hpp>
#include <mitiru/platform/headless/HeadlessPlatform.hpp>

// Forward declarations -- stored as raw/shared pointers, no method calls in Engine
namespace mitiru::validate { class TemporalInvariantChecker; }
namespace mitiru::observe { class StructuredDiff; class CausalChain; }
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

private:
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
	int m_logicalWidth = 0;                          ///< Screen論理幅 (layout()で決定、固定)
	int m_logicalHeight = 0;                         ///< Screen論理高さ (layout()で決定、固定)
	// Debug overlay data
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
	std::atomic<bool> m_shouldStop{false};            ///< 停止要求フラグ (スレッドセーフ)
	bool m_initialized = false;                      ///< 初期化済みフラグ
	int m_autoTestFrameCount = 0;                    ///< 自律テストモード用フレームカウンタ
	bool m_autoTestCaptured = false;                 ///< 自律テスト用キャプチャ完了フラグ
	Game* m_loopGame = nullptr;                      ///< tickOneFrame()用ゲーム参照
	const EngineConfig* m_loopConfig = nullptr;      ///< tickOneFrame()用設定参照

	/// 固定タイムステップアキュムレータ
	static constexpr float kFixedDt = 1.0f / 60.0f;  ///< 固定タイムステップ (秒)
	static constexpr int kMaxFrameSkip = 5;           ///< 最大フレームスキップ数
	static constexpr float kMaxDelta = 0.1f;          ///< スパイラルオブデス防止キャップ
	float m_accumulator = 0.0f;                       ///< 固定タイムステップ用アキュムレータ
};

} // namespace mitiru

// -- Detail headers (out-of-class method definitions) ----------------------
// In header-only mode (MITIRU_HEADER_ONLY=1) these are included here so that
// every TU that includes Engine.hpp gets the inline definitions.
// In STATIC mode (MITIRU_HEADER_ONLY undefined) the detail headers are compiled
// once inside src/core/Engine.cpp -- including them here would cause ODR violations.
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
// CEF detail is unconditionally included; the method body contains its own
// #if defined(_WIN32) && defined(MITIRU_HAS_CEF) guard (matching Engine.hpp's
// existing CefContext typedef guard above), so it compiles safely on all platforms.
#include <mitiru/core/detail/Engine_Cef.hpp>
#endif // MITIRU_HEADER_ONLY
