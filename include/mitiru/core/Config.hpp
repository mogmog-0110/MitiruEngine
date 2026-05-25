#pragma once

/// @file Config.hpp
/// @brief エンジン設定構造体
/// @details Mitiruエンジンの初期化パラメータを保持する。

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <sgc/types/Color.hpp>

namespace mitiru
{

class Engine;  // onFrameStart コールバックの signature 用 forward decl

// 後述の replay record/inject コールバック用 forward decl (host 側専用。
// DLL ABI には含まれない — EngineConfig は game から一切見えない)。
namespace module { struct InputSnapshot; struct FrameIntents; }

/// @brief グラフィックスバックエンド列挙
namespace gfx
{

/// @brief GPUバックエンドの種別
enum class Backend
{
	Auto,    ///< 環境に応じて自動選択
	Dx11,    ///< DirectX 11
	Dx12,    ///< DirectX 12
	Vulkan,  ///< Vulkan
	OpenGL,  ///< OpenGL
	WebGL,   ///< WebGL2（Emscripten/WASM環境用）
	WebGPU,  ///< WebGPU（Emscripten/WASM環境用、WGSLシェーダー）
	Null     ///< ヌルデバイス（ヘッドレスモード用）
};

} // namespace gfx

/// @brief ディスプレイ表示モード
enum class DisplayMode
{
	Windowed,             ///< 通常ウィンドウ (枠/タイトルバー有り)
	BorderlessFullscreen, ///< ボーダーレスウィンドウ (枠無し全画面)。ALT+TAB 高速、modern game の標準
	// ExclusiveFullscreen は今は実装しない (DX12 では推奨されない)
};

/// @brief エンジン設定
/// @details エンジン起動時に渡す全パラメータを集約する。
///          デフォルト値が設定されているため、必要なフィールドだけ変更すればよい。
struct EngineConfig
{
	std::string title = "Mitiru Game";     ///< ウィンドウタイトル
	int windowWidth = 1920;                ///< ウィンドウ幅（Windowed モード時）
	int windowHeight = 1080;               ///< ウィンドウ高さ（Windowed モード時）

	/// @brief windowWidth/Height の解釈
	/// @details
	///   - false (既定、後方互換): 物理ピクセル指定。1280 を渡すと 125% DPI 環境でも
	///     client 幅 1280 物理 px = 論理幅 1024 DIP になるため、1:1 描画したい
	///     ゲームは game.layout() で受ける実 client サイズを使う必要がある。
	///   - true: 論理ピクセル (DIP) 指定。engine が systemDpi を見て
	///     物理 px = dip * (dpi/96) へ拡大してから OS に渡す。125% DPI で
	///     windowWidth=1280 → 物理 1600 px が確保される。
	bool useLogicalWindowSize = false;
	DisplayMode displayMode = DisplayMode::Windowed; ///< 表示モード
	bool vsync = true;                     ///< 垂直同期 (DX12 SwapChain Present interval)
	int targetFps = 0;                     ///< フレームレート上限 (0=無制限。vsync が ON の場合は vsync が優先)

	/// @brief ユーザがウィンドウ枠でリサイズできるか
	/// @details false にすると WS_THICKFRAME / WS_MAXIMIZEBOX が外れ、
	///          固定サイズの window になる。Siv3D の `WindowStyle::Fixed`
	///          に相当。ピクセルアート / 固定解像度ゲームで誤リサイズ
	///          を防ぎたい場合に使う。
	bool windowResizable = true;

	/// @brief 動的リサイズ時の logical screen size の扱い
	/// @details Siv3D の `Scene::SetResizeMode` に相当する3-mode 切替:
	///   - **Actual**: logical = physical (1:1)。HTML の `@media` も発火、
	///                 native draw も physical coords。動的レイアウト推奨。
	///   - **Virtual**: logical を初期値で固定、viewport だけ physical 追従。
	///                  anisotropic stretch (アスペクト保たれない)。Siv3D の
	///                  `Virtual` と同じ。論理座標で書かれた legacy game 向け。
	///   - **Keep**: logical を初期値で固定、viewport は letterbox/pillarbox
	///               でアスペクト保持。固定解像度のゲームに最適。
	enum class ResizeMode : std::uint32_t
	{
		Actual  = 0,
		Virtual = 1,
		Keep    = 2,
	};
	ResizeMode resizeMode = ResizeMode::Actual;

	/// @brief フレーム冒頭で device 経由で issued される clear color。
	/// @details DLL 内 `screen->clear(...)` は m_clearColor を更新するが
	///          実際の ClearRenderTargetView は frame 頭で device->m_clearColor
	///          を使って発行されるため、DLL からの per-frame 上書きは効かない。
	///          host (mitiru_host 等) でゲーム本体の背景色を統一したい場合は
	///          このフィールドに値を設定する。default は黒 (後方互換)。
	sgc::Colorf backgroundColor{0.0f, 0.0f, 0.0f, 1.0f};

	/// @brief ゲーム側で選択可能な解像度プリセット
	/// @details 設定 UI のドロップダウン用。空ならプリセット非表示。
	std::vector<std::pair<int,int>> resolutionPresets = {
		{1920, 1080},
		{1280, 720},
	};
	bool headless = false;                 ///< ヘッドレスモード（ウィンドウなし）
	/// @brief 決定論的モード（固定 dt = 1/targetTps）。
	/// @details インタラクティブ実行では `false` 推奨（実時間 dt）。`true` だと
	///          高 refresh rate モニタで accumulator が 1/60 ずつしか積まれず、
	///          物理経過 1 秒に対しゲーム時間が refreshRate / 60 倍進む結果に
	///          なる（144 Hz で 2.4 倍速）。リプレイ / ヘッドレス / 自動テスト
	///          のように決定性を要する用途でだけ明示的に `true` にすること。
	bool deterministic = false;
	std::uint64_t randomSeed = 42;         ///< 乱数シード
	float targetTps = 60.0f;               ///< 目標TPS（tick/秒）
	gfx::Backend gfxBackend = gfx::Backend::Auto;  ///< グラフィックスバックエンド
	bool enableObserver = true;            ///< オブザーバー機能の有効化
	int observePort = 0;                   ///< オブザーバーポート（0=自動割当）
	bool enableDiffTracking = false;       ///< 構造化差分トラッキング有効化
	bool enableCausalTracking = false;     ///< 因果チェーン追跡有効化
	bool enableTemporalValidation = false; ///< 時系列不変条件チェック有効化
	bool enableUIValidation = false;       ///< UIレイアウト検証有効化
	bool enableHttpApi = false;            ///< 組み込みHTTP APIサーバー有効化
	int httpApiPort = 8090;                ///< HTTP APIサーバーポート（デフォルト8090）
	bool imguiVisibleOnStart = false;      ///< ImGuiオーバーレイを起動時から表示する（Hub等向け）

	// ── フォント ──
	std::string fontPath;                  ///< TTFフォントファイルパス（空=自動検索）
	bool skipDefaultFont = false;          ///< true なら 2D Screen のデフォルト TTF/SDF 初期化を完全スキップ
	                                       ///< (CEF 経由ですべての文字を描画するゲーム向け、起動 ~15 秒短縮)

	/// @brief SDF フォントアトラスに含める Unicode 範囲 (bitmask)
	/// @details default の FontAtlas::Japanese は ASCII + かな + 漢字 で 3000+ glyphs を
	///          atlas に焼き込むため初回起動が ~15 秒かかる (UI 側は黒画面に見える)。
	///          英数のみでよければ FontAtlas::Latin で起動が 1 秒未満になる。
	///          skipDefaultFont=true のときは参照されない。
	enum class FontAtlas : std::uint32_t
	{
		None           = 0,
		Ascii          = 1u << 0,
		Hiragana       = 1u << 1,
		Katakana       = 1u << 2,
		CjkPunctuation = 1u << 3,
		Fullwidth      = 1u << 4,
		CommonKanji    = 1u << 5,

		Latin    = Ascii,
		Kana     = Ascii | Hiragana | Katakana,
		Japanese = Ascii | Hiragana | Katakana
		         | CjkPunctuation | Fullwidth | CommonKanji,
	};
	FontAtlas fontAtlasRanges = FontAtlas::Japanese;

	friend constexpr FontAtlas operator|(FontAtlas a, FontAtlas b) noexcept
	{
		return static_cast<FontAtlas>(
			static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
	}
	friend constexpr FontAtlas operator&(FontAtlas a, FontAtlas b) noexcept
	{
		return static_cast<FontAtlas>(
			static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
	}
	friend constexpr bool hasFontAtlasRange(FontAtlas set, FontAtlas bit) noexcept
	{
		return (static_cast<std::uint32_t>(set)
		      & static_cast<std::uint32_t>(bit)) != 0;
	}

	// ── CEF (Win32 + DX12 のみ) ──
	bool enableCef = true;                 ///< CEF 初期化を行うか (false=CEF 抜きで起動、~数秒短縮)
	std::string cefLogPath;                ///< CEF ログファイルパス (空=デフォルト: "<exeDir>/cef_debug.log")
	std::string cefStartUrl;               ///< 起動時に開く URL (空=about:blank)
	int cefRemoteDebuggingPort = 0;        ///< 0 以外で chrome-devtools MCP が http://localhost:<port>
	                                       ///< に attach 可能 (E-02)。開発ビルドのみで有効化推奨。

	/// @brief CEF ページが fetch / XHR で読み込めるローカルディレクトリ追加分
	/// @details 既定動作 (空ベクター) でも `file:///` から sibling ファイルを読む
	///          ことは `--allow-file-access-from-files` + `--disable-web-security`
	///          で有効になっているため、多くの consumer はこれを設定する必要はない。
	///
	///          より厳格に `app://` カスタムスキーム経由で配信したい場合 (CORS 完全
	///          対応・サンドボックス強化) に追加のルートを宣言する。指定した各
	///          ディレクトリは `app://` 仮想パス解決時に順に検索される
	///          (`(exeDir)/assets/` が常に最優先で先に試される)。
	///
	///          典型例:
	///            cfg.cefAdditionalAssetDirs = {
	///              "C:/path/to/shared-pack",      // theme pack 等
	///              "C:/path/to/dev-overrides"     // 開発時のホットリロード用
	///            };
	///            cfg.cefStartUrl = "app://ui/title.html";
	///
	///          トレードオフ: file:// はパス計算がシンプルで debug が容易。
	///          app:// は CORS が厳密で本番ビルドの埋め込みアセットと統一される。
	std::vector<std::string> cefAdditionalAssetDirs;

	// ── 音量 (0.0 - 1.0) ──
	float masterVolume = 1.0f;             ///< マスター音量
	float bgmVolume = 0.8f;                ///< BGM 音量
	float seVolume = 1.0f;                 ///< SE / 効果音 音量
	float voiceVolume = 1.0f;              ///< ボイス音量

	// ── 言語 ──
	std::string language = "ja";           ///< 現在の言語コード ("ja", "en" 等)
	std::vector<std::string> availableLanguages = {"ja", "en"}; ///< 選択可能な言語

	// ── キーバインド ──
	/// @brief アクション名 → キーコード(VK_*) のマップ
	/// @details 既定値は各ゲームが setup する。settings.json で上書き可
	std::map<std::string, int> keyBindings;

	// ── 設定永続化 ──
	bool persistSettings = false;          ///< 起動時に settings.json を読み込み、変更時に保存する
	std::string settingsFileName = "settings.json"; ///< 設定ファイル名 (%APPDATA%/<title>/ 配下に配置)

	// ── per-frame host hook ─────────────────────────────────────────────
	/// @brief tickOneFrame の先頭で呼ばれる (optional)
	/// @details Host が「main loop に割り込みたい」用途のためのフック。
	///          典型的な用途は **DLL hot reload の file watcher** —
	///          `mitiru_host --watch` がここで DLL の mtime を polling し、
	///          変化があれば `engine.reloadModule(path)` を呼ぶ。
	///          設定されていなければ engine 側は no-op。
	std::function<void(mitiru::Engine&)> onFrameStart;

	// ── replay record / inject フック (axis 4: deterministic + replay-as-test) ──
	/// @brief 毎フレーム on_update 後に呼ばれ、その frame の InputSnapshot と
	///        FrameIntents を host へ渡す。host は Recorder へ書き出す
	///        (`mitiru run --record`)。設定が無ければ no-op。ADR 0005 不変:
	///        これは host 側 config であって DLL は一切見ない。
	std::function<void(const module::InputSnapshot&, const module::FrameIntents&)>
		onModuleFrameRecorded;

	/// @brief buildModuleInputSnapshot の末尾で呼ばれ、live 入力で組んだ snapshot を
	///        記録済みバイトで上書きする (`mitiru replay --test` のヘッドレス再生)。
	///        true を返すと上書き採用。設定が無ければ live 入力のまま。
	std::function<bool(module::InputSnapshot&)> moduleInputOverride;

	// ── 自律テストモード ──
	/// @brief テストモードフラグ（指定フレーム後に自動キャプチャ＆終了）
	/// @details `applyAutoTestEnv()` を呼ぶと `MITIRU_AUTOTEST=1` 環境変数で
	///          外部から有効化できる。`Engine::run()` は自動でこのフックを
	///          通すため、`MITIRU_AUTOTEST=1 ./game.exe` だけで CI 向けの
	///          一発撮りが走る。明示的に `cfg.autoTestMode = true` を設定して
	///          いる場合は env var に上書きされない。
	bool autoTestMode = false;
	int autoTestFrames = 60;               ///< キャプチャまでのフレーム数
	/// @brief スクリーンショット・レポート出力先ディレクトリ
	/// @details `MITIRU_AUTOTEST_OUTPUT` 環境変数で上書き可能 (絶対パス推奨。
	///          相対パスは exe の起動 CWD に依存するため信頼できない)。
	std::string autoTestOutputDir = ".";
	bool autoTestExitAfter = true;         ///< キャプチャ後にメインループを終了する

	// ── 環境変数フック ──
	/// @brief `MITIRU_AUTOTEST` のキー名。grep 経由でフックを見付けやすくする。
	static constexpr const char* kEnvAutoTest       = "MITIRU_AUTOTEST";
	/// @brief `MITIRU_AUTOTEST_OUTPUT` のキー名 (絶対パス推奨ディレクトリ)。
	static constexpr const char* kEnvAutoTestOutput = "MITIRU_AUTOTEST_OUTPUT";

	/// @brief 環境変数 `MITIRU_AUTOTEST` / `MITIRU_AUTOTEST_OUTPUT` を反映する。
	/// @details
	///   - `MITIRU_AUTOTEST=1` (または `true` / 任意の non-empty / 非 `0` 値) が
	///     セットされていて、`autoTestMode` が **まだ既定値 (false)** の場合のみ
	///     `autoTestMode = true` / `autoTestExitAfter = true` / `autoTestFrames = 120`
	///     を適用する。プログラム側で明示的に `cfg.autoTestMode = true` を設定
	///     している場合は env var に上書きされない (ユーザー設定優先)。
	///   - `MITIRU_AUTOTEST_OUTPUT=<dir>` が空でない値でセットされている場合、
	///     `autoTestOutputDir` を上書きする (こちらは現在値が既定 `"."` でも
	///     上書きする — env var は常に明示的なオーバーライド扱い)。
	///   - `Engine::run()` の冒頭で自動的に呼ばれるため、通常 consumer 側で
	///     直接呼ぶ必要はない。テストや専用 main で env を再評価したい時に
	///     使う。
	void applyAutoTestEnv()
	{
		if (!autoTestMode)
		{
			const char* envOn = std::getenv(kEnvAutoTest);
			if (envOn && envOn[0] != '\0'
				&& !(envOn[0] == '0' && envOn[1] == '\0'))
			{
				autoTestMode      = true;
				autoTestExitAfter = true;
				autoTestFrames    = 120;  // ~2 秒 @ 60 fps の deterministic 窓
			}
		}

		const char* envOut = std::getenv(kEnvAutoTestOutput);
		if (envOut && envOut[0] != '\0')
		{
			autoTestOutputDir = envOut;
		}
	}
};

} // namespace mitiru
