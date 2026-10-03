#pragma once

/// @file Config.hpp
/// @brief エンジン設定構造体
/// @details Mitiru エンジンの初期化パラメータを保持する。

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <string_view>

#include <sgc/types/Color.hpp>

#include <mitiru/render/ColorVision.hpp>
#include <mitiru/render/PostEffectSettings.hpp>
#include <mitiru/render/QualityCaps.hpp>

namespace mitiru
{

class Engine;  // onFrameStart コールバックの signature 用 forward decl

// 後述の replay record/inject コールバック用 forward decl (host 側専用。
// DLL ABI には含まれない。EngineConfig は game から一切見えない)。
namespace module { struct InputSnapshot; struct FrameIntents; }

/// @brief グラフィックスバックエンド列挙
namespace gfx
{

/// @brief GPU バックエンドの種別
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

/// @brief EngineConfig::moduleFrameDriver が 1 フレームをどう進めたか
enum class ModuleFrameDrive
{
	Advanced,   ///< 進めた。out に確定した intent がある
	Idle,       ///< 進めなかった (相手を待っている等)。このフレームは記録も intent の処理もしない
	Faulted,    ///< game が落ちた
	Local,      ///< 進め方を任せない (オンラインでない)。engine がいつもどおり on_update を呼ぶ
};

/// @brief エンジン設定
/// @details エンジン起動時に渡す全パラメータを集約する。
///          デフォルト値が設定されているため、必要なフィールドだけ変更すればよい。
struct EngineConfig
{
	std::string title = "Mitiru Game";     ///< ウィンドウタイトル
	int windowWidth = 1920;                ///< ウィンドウ幅（Windowed モード時）
	int windowHeight = 1080;               ///< ウィンドウ高さ（Windowed モード時）
	/// @brief 窓の初期スクリーン座標 (既定 = CW_USEDEFAULT = OS 任せ)。
	/// @details 録画・自動化で「実画面に一瞬も出さず、最初から指定位置 (仮想ディスプレイ等) に
	///          出す」ために使う。値が INT_MIN (=CW_USEDEFAULT) のままなら従来どおり OS 任せ。
	int windowX = (-2147483647 - 1);       ///< CW_USEDEFAULT 相当 (INT_MIN)
	int windowY = (-2147483647 - 1);       ///< CW_USEDEFAULT 相当 (INT_MIN)

	/// @brief リサイズ時の最小クライアントサイズ (0 = 制限なし)
	/// @details ユーザが枠を drag で縮めても、この client px 未満には縮まない
	///          (Win32 は WM_GETMINMAXINFO で強制)。文字や panel が潰れて
	///          読めなくなる極端な縮小を防ぐ。resize 安全な窓の最低保証。
	int minWindowWidth = 0;                ///< 最小クライアント幅 (px、0=制限なし)
	int minWindowHeight = 0;               ///< 最小クライアント高さ (px、0=制限なし)

	/// @brief windowWidth/Height の解釈
	/// @details
	///   - false (既定、後方互換): 物理ピクセル指定。1280 を渡すと 125% DPI 環境でも
	///     client 幅 1280 物理 px = 論理幅 1024 DIP になるため、1:1 描画したい
	///     ゲームは game.layout() で受ける実 client サイズを使う必要がある。
	///   - true: 論理ピクセル (DIP) 指定。engine が systemDpi を見て
	///     物理 px = dip * (dpi/96) へ拡大してから OS に渡す。125% DPI で
	///     windowWidth=1280 → 物理 1600 px が確保される。
	bool useLogicalWindowSize = false;
	/// @brief 音の置き場 (空 = 音を鳴らさない)。
	/// @details mitiru_host は DLL の隣を見て自分で決めるので、これを使うのは
	///          DLL を持たない静的リンク経路 (wasm)。id は
	///          `<audioDir>/<id>.wav|.ogg|.mp3|.flac` で解決する。
	std::string audioDir;

	DisplayMode displayMode = DisplayMode::Windowed; ///< [起動時のみ] 表示モード
	bool vsync = true;                     ///< 垂直同期 (DX12 SwapChain Present interval)
	int targetFps = 0;                     ///< フレームレート上限 (0=無制限。vsync が ON の場合は vsync が優先)

	/// @brief ユーザがウィンドウ枠でリサイズできるか (既定: 固定サイズ)
	/// @details 既定は false = 固定サイズ (WS_THICKFRAME / WS_MAXIMIZEBOX を外す。
	///          Siv3D の `WindowStyle::Fixed` に相当)。固定解像度のゲーム/デモが
	///          誤ってリサイズされて崩れるのを防ぐ。リサイズを許すゲームは true にする。
	/// @brief [起動時のみ] ウィンドウ生成時のスタイルビットに焼き込まれるため、生成後の変更は反映されない。
	bool windowResizable = false;

	/// @brief 動的リサイズ時の logical screen size の扱い
	/// @details Siv3D の `Scene::SetResizeMode` に相当する 3-mode 切替。
	///   - **Actual**: logical = physical (1:1)。HTML の `@media` も発火し、
	///                 native draw も physical coords。動的レイアウト推奨。
	///   - **Virtual**: logical を初期値で固定し、viewport だけ physical に追従。
	///                  anisotropic stretch (アスペクトは保たれない)。Siv3D の
	///                  `Virtual` と同じ。論理座標で書かれた legacy game 向け。
	///   - **Keep**: logical を初期値で固定し、viewport は letterbox/pillarbox
	///               でアスペクトを保つ。固定解像度のゲームに最適。
	enum class ResizeMode : std::uint32_t
	{
		Actual  = 0,
		Virtual = 1,
		Keep    = 2,
	};
	ResizeMode resizeMode = ResizeMode::Actual;

	/// @brief 背景クリア色の「初期値」。
	/// @details Engine::run 起動時に一度だけ screen->clearColor() に設定される。
	///          以後はゲームの draw() 内 `screen->clear(色)` が背景色を制御し、その色が
	///          次フレーム頭で device の ClearRenderTargetView に反映される（1 フレーム遅れ）。
	///          ゲームが clear() を呼ばなければ、この既定色が背景として残る。default は黒。
	sgc::Colorf backgroundColor{0.0f, 0.0f, 0.0f, 1.0f};

	/// @brief ローファイ・ポスト FX（低解像レンダー + パレット量子化 + Bayer ディザ）。
	/// @details DirectX5 / 256 色・16bit 期の "粗い網点質感" を再現する（DX12 のみ）。
	///          有効時、ゲームは低い内部解像度のオフスクリーンに描画され、最終提示時に
	///          パレット量子化 + 4×4 Bayer オーダードディザを掛けてニアレストでウィンドウへ拡大する。
	///          既定は無効（無効時は従来どおりの描画で挙動は一切変わらない）。
	struct LoFiConfig
	{
		bool enabled = false;          ///< 機能 ON/OFF（既定 OFF）
		int internalWidth = 320;       ///< 内部レンダー幅（例 320 / 640）
		int internalHeight = 240;      ///< 内部レンダー高さ（例 240 / 480）
		bool quantize = true;          ///< パレット量子化を行うか
		bool dither = true;            ///< Bayer オーダードディザを行うか
		int colorBitsR = 5;            ///< R チャンネル量子化ビット数（既定 RGB565）
		int colorBitsG = 6;            ///< G チャンネル量子化ビット数
		int colorBitsB = 5;            ///< B チャンネル量子化ビット数
		float ditherStrength = 1.0f;   ///< ディザ強度（0=無し, 1=量子化 1 段ぶん）
		bool softUpscale = true;       ///< 拡大: true=柔らか (テクセル量子化のままバイリニア。
		                               ///<        実機の映像出力の滲みに近い) / false=硬いニアレスト
		bool viFilter = false;         ///< 映像出力段の de-dither + divot。ON なら拡大はニアレスト固定
		float gamma = 1.0f;            ///< 出力ガンマ（1 で素通し）

		/// @brief 全チャンネルを同一ビット数に設定する（例 256 色相当なら 3/3/2 を個別指定）。
		void setUniformBits(int bits) noexcept { colorBitsR = colorBitsG = colorBitsB = bits; }
	};
	LoFiConfig loFi;                   ///< ローファイ・ポストFX 設定

	/// @brief 2D 描画の 4x MSAA アンチエイリアス（DX12 のみ）。既定 ON。
	/// @details 回転した塗り図形・三角形・多角形・線・円の輪郭を、4x MSAA の中間
	///          カラー RT でサンプル単位のカバレッジ平滑化してから提示する。軸並行の
	///          矩形はもともと綺麗だが、回転や斜辺がジャギー（階段状）になる問題の
	///          根本対策。
	///          lo-fi（粗い網点質感）／3D 描画フレーム／ポストプロセス使用時は自動的に
	///          バイパスされる（それらは独自の合成経路を持つ）。4x MSAA 非対応の稀な
	///          環境では自動的に 1x へフォールバックする（無効化されるだけで落ちない）。
	///          メモリ増は概算 幅×高さ×4 サンプル×4byte（1920×1080 で約 33MB）。
	bool antialiasing2D = true;

	/// @brief 3D の AA の方式 (DX12 のみ)。TAA は射影を画素内でずらして履歴を重ねる。描画だけの設定で、
	///        GameMemory とシミュレーションには触れない (ずらしの相はレンダラが数えるフレームで決まる)
	render::AntiAliasing3D antiAliasing3D = render::AntiAliasing3D::MsaaFxaa;
	/// @brief 3D の動きのぼけ (DX12 のみ)。シャッターの開いている割合 0..1、0 で無効
	float motionBlur3D = 0.0f;
	/// @brief 3D のパスごとの GPU 時間を測る (DX12 のみ、Engine::gpuPassTimes で読む)。タイムスタンプを打つぶん重くなるので計測の時だけ
	bool gpuPassTiming = false;
	/// @brief 読み込み中の資産を描かずに先へ進む (DX12 のみ、docs/STREAMING.md)。false は描く前に読み終えるのを待ち、
	///        フレームの頭で頼んだ読み込みを全部終える。headless の撮影とリプレイの照合は false のままにし、
	///        読み込みの速さを絵に出さない。読み込みの結果は描画にだけ効き、GameMemory には届かない
	bool asyncLoads = false;
	/// @brief 読み込んだモデルと world.json の GPU の大きさの予算 (byte)。越えている間は region の新しい区画を読まない。0 は無制限
	std::uint64_t streamingBudgetBytes = 0;

	/// @brief ゲーム側で選択可能な解像度プリセット
	/// @details 設定 UI のドロップダウン用。空ならプリセット非表示。
	std::vector<std::pair<int,int>> resolutionPresets = {
		{1920, 1080},
		{1280, 720},
	};
	bool headless = false;                 ///< [起動時のみ] ヘッドレスモード（ウィンドウなし）。ウィンドウ生成前に確定させる必要がある
	/// @brief headless SW フレームバッファの CPU ラスタライズ間隔 (#53)。
	/// @details 1 = 毎フレーム (既定・従来挙動)。N>1 = capture が読むフレーム
	///          だけラスタライズ (--capture-every N と同じ周期)。0 = 自動では
	///          行わない (capture() が stale を検知したら次フレームだけ実施)。
	///          ラスタライズはフルスクリーン CPU 描画でピクセル数に比例して
	///          重い (640x480 で数 ms〜) ため、観測しないフレームを省くと
	///          headless の自動回しが大幅に速くなる。sim の決定性には無関係。
	/// @details `--capture-dir` 運用では `mitiru_host` が `--capture-every` と同じ値を
	///          ここへ自動設定する (apps/mitiru_host/main.cpp)。Engine を host 経由でなく
	///          直接埋め込み、独自に PNG capture を回す場合は呼び出し側がこの値を
	///          capture 間隔に合わせること。既定の 1 のままだと毎フレーム全画面 CPU
	///          ラスタライズが走る (dirty rect 化は行っていない。差分が局所的でも
	///          全画面走査になる)。
	int swRasterizeEvery = 1;
	/// @brief 1 フレームで描く sprite の期待数 (C4)。0 = 既定 (SpriteBatch の内部既定値、
	///        現状 256 件) のまま。頂点バッファの初回 `reserve` を事前確保しておき、実行中の
	///        容量拡張 (毎回の再確保) を避けたいときに、既知の期待数を入れる。
	/// @details `Screen::applyExpectedSprites(int)` が実際の `SpriteBatch::reserveSprites()`
	///          呼び出しへ繋ぐ。この構造体は host 側の値渡し用で、実際に Screen へ渡すのは
	///          呼び出し側 (Screen 構築直後に `screen->applyExpectedSprites(config.expectedSprites)`
	///          を呼ぶこと) の責務。
	int expectedSprites = 0;
	/// @brief 決定論的モード（固定 dt = 1/targetTps）。
	/// @details インタラクティブ実行では `false` 推奨（実時間 dt）。`true` だと
	///          高 refresh rate モニタで accumulator が 1/60 ずつしか積まれず、
	///          物理経過 1 秒に対しゲーム時間が refreshRate / 60 倍進む結果に
	///          なる（144 Hz で 2.4 倍速）。リプレイ / ヘッドレス / 自動テスト
	///          のように決定性を要する用途でだけ明示的に `true` にすること。
	bool deterministic = false;
	/// @brief `--capture-dir` 撮影中かどうか (G1)。
	/// @details true だと `deterministic` の値に関わらず Clock を固定 dt へ強制する
	///          (Engine::initialize 内)。PNG 書き出しは描画コストを増やすため、実時間 dt
	///          のままだと撮影が重いフレームだけゲーム内時刻が遅れ、`--input-script` の
	///          秒指定 (`t=<sec>`) や記録した打鍵表がずれる。host は `--capture-dir`/
	///          `--capture-every` が有効なとき、headless かどうかに関わらずこれを true にする。
	bool captureActive = false;
	std::uint64_t randomSeed = 42;         ///< 決定論 RNG seed。module 経路では毎フレーム
	                                       ///< InputSnapshot::rngSeed として DLL に渡る。
	float targetTps = 60.0f;               ///< 目標TPS（tick/秒）
	gfx::Backend gfxBackend = gfx::Backend::Auto;  ///< [起動時のみ] グラフィックスバックエンド。device 生成後の切替は非対応
	bool enableObserver = true;            ///< [起動時のみ] オブザーバー機能の有効化
	int observePort = 0;                   ///< [起動時のみ] オブザーバーポート（0=自動割当）

	/// @brief [起動時のみ] 巻き戻しでさかのぼれるフレーム数 = リングバッファの長さ (0 = 未指定)
	/// @details 何フレーム前まで巻き戻せるか。優先順位は host の `--rewind-frames N` >
	///          game の `MITIRU_REWIND_BUFFER(N)` 宣言 > 既定 300 (60fps で約 5 秒)。
	///          大きくするほど過去まで戻せるが、GameMemory バイト数 × この数だけメモリを使う。
	std::uint32_t timeTravelBufferFrames = 0;

	/// @brief [起動時のみ] GameMemoryRing の予算バイト数 (0 = 無制限、従来どおり frames*frameSize を無条件確保)。
	/// @details host の `--rewind-mb N` から渡る。>0 で、生で持つとこの値を超えるときは ring が
	///          XOR+RLE の差分で圧縮し、記録の実バイト数がこの値を超える分だけ古い側から捨てる
	///          (`mitiru::observe::GameMemoryRing::configure` 参照)。
	std::size_t timeTravelBudgetBytes = 0;

	/// @brief `timeTravelBudgetBytes` が host の明示指定 (`--rewind-mb`) 由来かどうか。
	/// @details false のまま (既定) だと、game の `MITIRU_REWIND_BUDGET` 宣言 > 既定 512MB の順で
	///          `Engine_Module_Loader.hpp::recordModuleMemoryFrame` が決める。true なら
	///          `timeTravelBudgetBytes` (0 = 無制限も含む) を無条件で使う。
	bool timeTravelBudgetBytesExplicit = false;

	/// @brief [起動時のみ] GameMemoryRing を生 (memcpy) で持つ上限バイト数。frames × GameMemory が
	///        これを超えたら、予算内でも差分で圧縮する (host の `--rewind-raw-mb N`)。
	/// @details 実ゲームの差分は数 % に縮むので、60 秒の窓でも数 MB で持てる。半分も縮まない状態は
	///          ring が最初の 1 周期で見分けて生へ戻す。0 = 予算いっぱいまで生で持つ。
	std::size_t timeTravelRawLimitBytes = 64u * 1024u * 1024u;

	/// @brief [起動時のみ] ホットリロード直後、差し替え前の DLL と GameMemory を生かしておくフレーム数。
	/// @details この間に新しい DLL の update / draw がアクセス違反などで止まったら、host は落ちずに
	///          差し替え前へ戻る (MSVC のみ)。0 = 戻らない (差し替えた瞬間に旧 DLL を解放する)。
	std::uint32_t reloadRollbackFrames = 180;

	/// @brief Engine::frameArena() の容量バイト数 (毎フレーム先頭で reset される bump アロケータ)。
	/// @details `include/mitiru/core/FrameArena.hpp` 参照。hot path の一時バッファ (JSON 文字列化・
	///          snapshot バッファ等) をここから確保すると、フレーム末で丸ごと巻き戻り allocation が
	///          蓄積しない。既定 4 MB。
	std::size_t frameArenaBytes = 4u * 1024u * 1024u;
	bool enableDiffTracking = false;       ///< 構造化差分トラッキング有効化
	bool enableCausalTracking = false;     ///< 因果チェーン追跡有効化
	bool enableTemporalValidation = false; ///< 時系列不変条件チェック有効化

	// ── 組込オラクル (N1) ────────────────────────────────────────────────
	/// @brief 組込オラクル (NaN/Inf・range・停滞・スパイク・決定論・画面不変/黒・MITIRU_INVARIANT) の
	///        有効/無効。既定 ON (NaN/Inf・range・スパイク・停滞・invariant は軽量なので常時実行)。
	bool oracleEnabled = true;
	/// @brief 停滞判定 (c): 入力が変化しているのにこの秒数だけ GameMemory が不変なら違反とする。
	float oracleStagnantSeconds = 5.0f;
	/// @brief 決定論オラクル (e) を有効にするか。既定 OFF (K フレーム前からの再シミュレーション +
	///        memcmp は GameMemory サイズに比例したコストがかかるため明示 opt-in)。
	bool oracleDeterminism = false;
	/// @brief 決定論オラクルの実行間隔 (フレーム数)。0 なら既定 120 (2 秒 @ 60fps) を使う。
	std::uint32_t oracleDeterminismEveryFrames = 0;
	/// @brief 画面オラクル (f) を有効にするか。既定 OFF (capture 経路が無いと画素が取れない)。
	bool oracleScreenCheck = false;
	/// @brief 画面不変判定のフレーム数しきい値。
	std::uint32_t oracleScreenStagnantFrames = 180;
	/// @brief 機械可読な `[oracle] kind=... frame=... field=... value=...` 行を stderr へ追加出力するか。
	/// @details 既定 OFF。人間向けの `warnOnceFix` 出力 (`[mitiru] frame N: ...`) はそのまま残し、
	///          `mitiru-cli` の `ScanOracleLines`（`E:\user\mitiru-cli\internal\hunt\oracle.go`）が
	///          正規表現で拾える別行として足す。ON にすると同じ違反でも毎回 (warnOnce の間引き無しで) 出す。
	bool oracleMachineLog = false;
	/// @brief セーブ往復検査 (`--save-roundtrip-test`)。save → 読み戻し → 再 save の 2 回の
	///        書き込みが bit 一致するかを確認する (Factorio FFF #158 の save-load stability と同じ考え方)。
	///        既定 OFF (通常セーブに 1 回余分な書込 + memcmp が乗るため明示 opt-in)。
	bool saveRoundtripTest = false;

	// ── 常時バグリング (P1) ──────────────────────────────────────────────
	/// @brief 「昨日のバグ」再生用の常時短リングの長さ (秒)。0 で無効。既定 30 秒。
	/// @details 60fps 換算でフレーム数に変換して `observe::pushBugRingFrame` の capacity に渡す。
	/// @brief [起動時のみ] リング容量はエンジン初期化時に確保される。実行中の変更は次回起動まで反映されない。
	float bugRingSeconds = 30.0f;
	/// @brief host がホットキー等で「今すぐ ring を bug_<timestamp>.mtrr へ保存せよ」と要求する
	///        フラグ。Engine 側が消費すると false に戻す (ワンショット)。
	bool bugRingSaveRequested = false;
	// ── 1 ファイル配布 (P12) ─────────────────────────────────────────────
	/// @brief `.mtpak` (v1, tools/make_pack.py が書く固定レイアウト: "module.dll" + "assets/**" +
	///        "recordings/**") から DLL とアセットを読む。空なら従来どおり disk から読む。
	///        `Engine::loadModule` がこの path を見て、渡された modulePath を無視し pack 内の
	///        DLL を一時展開して load する (`MITIRU_PACK` 環境変数でも同じ経路に入る、host 引数の
	///        配線を待たずに動かすため)。
	/// @brief [起動時のみ] loadModule 時に一度だけ参照される。実行中の切替は非対応
	std::string packPath;
	/// @brief [起動時のみ] 物理問い合わせ job (v37) が答える静的 collision の JSON (`--collision`)。
	///        `[{"min":[x,y,z],"max":[x,y,z],"layer":0}, ...]` の箱の列 (`{"vertices":[..],"indices":[..]}` の
	///        三角形メッシュも混ぜられる)。空なら物理 world を持たず、結果は全部 kPhysicsHitUnsupported になる。
	std::string collisionPath;

	bool enableUIValidation = false;       ///< UIレイアウト検証有効化
	bool enableHttpApi = false;            ///< [起動時のみ] 組み込みHTTP APIサーバー有効化
	int httpApiPort = 8090;                ///< [起動時のみ] HTTP APIサーバーポート（デフォルト8090）

	// ── フォント ──
	std::string fontPath;                  ///< TTFフォントファイルパス（空=自動検索）
	bool skipDefaultFont = false;          ///< [起動時のみ] true なら書体を読まず、2D Screen の文字は 8x8 ビットマップで描く
	                                       ///< (字形は使われた分だけ焼くので、書体を読むこと自体は起動をほぼ遅らせない)

	// ── UI (RmlUi、Win32 + DX12 のみ) ──
	std::string uiDocument;                ///< [起動時のみ] main window に重ねる RML 文書 (空=RmlUi を使わない)。
	                                       ///< mitiru_host は DLL の隣の assets/ui/main.rml があればそれを入れる

	// ── 音量 (0.0 - 1.0) ──
	float masterVolume = 1.0f;             ///< マスター音量
	float bgmVolume = 0.8f;                ///< BGM 音量
	float seVolume = 1.0f;                 ///< SE / 効果音 音量
	float voiceVolume = 1.0f;              ///< ボイス音量

	// ── 言語 ──
	std::string language = "ja";           ///< 現在の言語コード ("ja", "en" 等)
	std::vector<std::string> availableLanguages = {"ja", "en"}; ///< 選択可能な言語

	// ── セーブ (hud.save / hud.load) ──
	/// @brief セーブスロットの置き場。既定は作業フォルダの save/。出荷するゲームは
	///        mitiru_host の --game-name か --save-dir で %APPDATA%/<game>/saves にする
	std::string saveDir = "save";
	int autosaveSlots = 3;                 ///< hud.save("auto") が輪番で使う枠の数
	std::uint32_t gameVersion = 0;         ///< セーブのメタ情報に残すゲームの版 (エンジンは比べない)

	// ── 利用者の設定 (アクセシビリティ、毎フレーム読む) ──
	float shakeScale = 1.0f;               ///< 画面揺れ (hud.shake) の振幅に掛ける。0 で揺らさない
	bool cameraShake = true;               ///< false なら 3D のカメラは揺らさない (2D の揺れは shakeScale に従う)
	float rumbleScale = 1.0f;              ///< パッドの振動の強さに掛ける
	render::ColorFilterSettings colorFilter; ///< 色覚のフィルタ (UI まで含めたフレーム全体に掛ける)
	render::QualityCaps qualityCaps;       ///< 画質の上限。動いている間は Engine::setQualityCaps で変える

	// ── ビルドエラー帯 (mitiru watch、host 内部設定 — DLL ABI 非通過) ──
	/// @brief CLI がビルド失敗時に書くエラーファイルのパス (空=機能 OFF)
	/// @details `mitiru watch` がビルド失敗時に
	///          `<project>/build/.mitiru_build_error.txt` を書き、成功時に削除する。
	///          engine は ~0.5 秒毎に mtime を poll し、ファイルが存在する間だけ
	///          画面上部に半透明のエラー帯を描く (console を見なくても気付ける)。
	std::string errorBannerFile;

	// ── ランタイム時間制御 (host が toggle する debug 用) ───────────────
	static constexpr std::uint8_t kPauseKindIngame = 1;  ///< ゲームのポーズメニュー (既定)
	static constexpr std::uint8_t kPauseKindDebug  = 2;  ///< host の F8 / console / 分岐エディタが止めた
	static constexpr std::uint8_t kPauseKindObject = 3;  ///< オブジェクトだけ止めてカメラ等は動かす
	/// @brief on_update に渡す dt の乗数。0=停止と同等、1=通常速。負値は未定義。
	float timeScale = 1.0f;
	/// @brief true なら on_update を dt=0 で呼ぶ (描画は継続)。
	bool paused = false;
	/// @brief paused 中の種類 (HE2 の INGAME / DEBUG / OBJECT pause 相当)。`InputSnapshot::paused` に
	///        そのまま乗り、game は「ポーズメニュー」と「デバッグで止めた」と「オブジェクトだけ止めた」を
	///        区別できる。pause 中も dt を通す layer は種類ごとに `MITIRU_PAUSE_LAYERS_BY_KIND` で決める。
	std::uint8_t pauseKind = kPauseKindIngame;
	/// @brief paused かつ > 0 のとき、1 フレームだけ通常 dt で進めて自動デクリメント。
	int stepFrames = 0;

	/// @brief layer 別の dt 倍率 (v30、§1-2)。`InputSnapshot::dtByLayer[i] = dt *
	///        layerTimeScale[i]` として毎フレーム焼き込まれる (module::Layer / Game.hpp の
	///        `Input::dt(Layer)` が読む)。timeScale (上記) はステップ数を増減させる別軸なので
	///        二重にはかけない。既定は全 layer 等倍。
	float layerTimeScale[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
	/// @brief hitStop 中にその layer も dt=0 にするか (v30、§1-2)。既定は layer 0
	///        (gameplay) だけ止まり、layer 1 (UI) は動き続ける (HUD のフェード等が
	///        hitStop で凍らない、hedgehog_study §1-2)。2..7 (予約) は安全側で凍らせる。
	bool layerFrozenByHitStop[8] = {true, false, true, true, true, true, true, true};

	// ── per-frame host hook ─────────────────────────────────────────────
	/// @brief tickOneFrame の先頭で呼ばれる (optional)
	/// @details Host が「main loop に割り込みたい」用途のためのフック。
	///          典型的な用途は **DLL hot reload の file watcher**。
	///          `mitiru_host --watch` がここで DLL の書き換えを見て
	///          (asset/FileWatcher.hpp)、変化があれば `engine.reloadModule(path)` を呼ぶ。
	///          設定されていなければ engine 側は no-op。
	std::function<void(mitiru::Engine&)> onFrameStart;

	// ── replay record / inject フック (axis 4: deterministic + replay-as-test) ──
	/// @brief 毎フレーム on_update 後に呼ばれ、その frame の InputSnapshot と
	///        FrameIntents を host へ渡す。host は Recorder へ書き出す
	///        (`mitiru run --record`)。設定が無ければ no-op。不変条件として、
	///        これは host 側 config であって DLL は一切見ない。
	std::function<void(const module::InputSnapshot&, const module::FrameIntents&)>
		onModuleFrameRecorded;

	/// @brief buildModuleInputSnapshot の末尾で呼ばれ、live 入力で組んだ snapshot を
	///        記録済みバイトで上書きする (`mitiru replay --test` のヘッドレス再生)。
	///        true を返すと上書き採用。設定が無ければ live 入力のまま。
	std::function<bool(module::InputSnapshot&)> moduleInputOverride;

	/// @brief on_update を 1 回呼ぶ代わりに、host がこのフレームの進め方を決める (オンライン協力プレイ。
	///        ロールバックで on_update を何度も呼び直す)。live は組み替えと上書きを済ませた手元の入力。
	std::function<ModuleFrameDrive(const module::InputSnapshot& live, module::FrameIntents& out)> moduleFrameDriver;

	/// @brief 実機の入力で組んだ snapshot を、利用者のキー割り当てで論理入力へ組み替える
	///        (input/ActionRemapper.hpp)。moduleInputOverride より前に呼ぶ。録画は組み替えた後の入力を
	///        残し、replay はそれで上書きするので、割り当てが変わっても再生の結果は変わらない。
	std::function<void(module::InputSnapshot&)> moduleInputRemap;

	/// @brief UI (RmlUi) の操作を game へ渡す前に host が受け取る。true を返したものは game へ渡さない
	///        (設定画面の "settings." 系の操作を host が引き受けるため)。
	std::function<bool(std::string_view name, std::string_view payloadJson)> uiActionFilter;

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
	///     上書きする。env var は常に明示的なオーバーライド扱い)。
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
