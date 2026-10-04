// mitiru_host。game-as-DLL モジュール用の最小エンジンランチャ (v0.2.0 step 3-4)
//
// Argv:
//   argv[0]  = host exe のパス
//   argv[1]  = game DLL のパス (cwd 相対または絶対)
//   argv[2+] = 任意フラグ:
//                --watch         DLL ファイルの書き換えを監視し変更時にリロード
//
// UI は DLL の隣の assets/ui/main.rml があれば RmlUi で重ねる (docs/UI_RMLUI.md)。
//
// エンジンを直接見るのは host のみ。game コードは DLL 内に閉じ、
// ModuleApi.hpp の C-only シグナルフロー経由でエンジンと通信する。
//
// `--watch` は L3 ホットリロード: DLL の書き換えを監視し (asset/FileWatcher.hpp)、変化したら
// HelloGameMemory* を生かしたまま DLL を差し替える (状態を保持)。
//
// Runtime hotkeys (Windows): F7 = step, F8 = pause/play, F9 = time-scale,
// F10 = lo-fi toggle, F12 = screenshot (file + clipboard).
//
// 将来: `mitiru-cli run [--watch]` が project/module.toml を解決して
// 等価な argv を自動構築する。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// アンブレラを廃止 (リファクタ P2)。使うものだけを明示的に include
#include <mitiru/util/PreciseWait.hpp>
#include <mitiru/platform/Utf8Args.hpp>
#ifdef _WIN32
#include <mitiru/platform/win32/Win32Window.hpp>  // --no-activate: 窓を前に出さない設定点
#endif
#include <mitiru/core/Engine.hpp>
#include <mitiru/module/Spawner.hpp>
#include <mitiru/debug/FrameBudget.hpp>  // FrameArena (2-1) の使用量を毎フレーム反映する
#include <mitiru/resource/AssetPath.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/asset/AssetPack.hpp> // vfs: pack mount / readGlobal
#include <mitiru/asset/AssetReload.hpp>
#include <mitiru/asset/FileWatcher.hpp>
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/observe/ScrubControlChannel.hpp>  // time-travel click-to-scrub
#include <mitiru/observe/DockChannel.hpp>          // 自窓矩形の broadcast (ツール窓のドッキング追従)
#include <mitiru/observe/SideStateInspect.hpp>     // replay 照合で窓口が食い違ったフレーム (side_state 窓)
#include <mitiru/render/SaveScreenshotPng.hpp>
#include <mitiru/render/AsyncPngWriter.hpp>
#include <mitiru/replay/Player.hpp>
#include <mitiru/replay/Recorder.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/debug/CrashReporter.hpp>
#include <mitiru/debug/HostCrashHandler.hpp>
#include <mitiru/module/ModuleHost.hpp>  // --bake: DLL の mitiru_module_bake_assets export を呼ぶだけの経路
#include <mitiru/input/InputScript.hpp>
#include <mitiru/text/TranslationCheck.hpp>

#include "FileAudioEngine.hpp"
#include "HostAudioCapture.hpp"
#include "HostGuiLog.hpp"
#include "HostOnline.hpp"
#include "HostShip.hpp"
#include "HostStreaming.hpp"
#include "HostPerfLog.hpp"
#include "HostWindowShot.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>  // ShellExecuteA (--console で既定ブラウザ起動)
#endif

#include <nlohmann/json.hpp>

// --perf-log の確保回数のため。この exe に入ったコードの new を数える
MITIRU_DEFINE_COUNTING_NEW

namespace
{

/// @brief replay の照合で、記録の GameMemory の後ろに付いた窓口の hash と今の窓口を比べる (ADR 0054)。
///        食い違ったフレームは窓口ごとに sideStateReplayMarks へ積み、side_state のツール窓が読む。
/// @return 最初に食い違った窓口の名前。一致すれば空
std::string replaySideDivergence(mitiru::Engine& engine, const std::vector<std::uint8_t>& recorded,
                                 std::uint32_t memSize, std::uint32_t frame, std::vector<std::uint8_t>& liveScratch)
{
	mitiru::observe::SideImageView rec;
	mitiru::observe::SideImageView live;
	if (!mitiru::observe::parseSideImage(recorded.data() + memSize, recorded.size() - memSize, rec))
	{
		return "(記録の形が壊れている)";
	}
	if (!engine.captureModuleSideState(liveScratch, false)
	    || !mitiru::observe::parseSideImage(liveScratch.data(), liveScratch.size(), live))
	{
		return "(今の状態を保存できない)";
	}
	mitiru::observe::sideStateReplayMarks().note(frame, rec, live);
	return std::string(mitiru::observe::firstDivergedSideChannel(rec, live));
}

/// プロセスの cwd を argv[0] のディレクトリに固定する。EngineConfig 内の相対パス
/// (書体など) を、どのシェルから起動しても解決できるようにするため。
void anchorCwdToExeDir(const char* argv0)
{
	if (argv0 == nullptr) { return; }
	std::error_code ec;
	const auto canon = std::filesystem::weakly_canonical(
		std::filesystem::path(argv0), ec);
	if (ec) { return; }
	std::filesystem::current_path(canon.parent_path(), ec);
}

/// exe と同じ場所に <exeStem>.mtargs があれば、その内容を argv[1..] 相当の
/// token 列として読む (引数なしでの起動 = ダブルクリック / Steam 用)。空白区切りだが、
/// "..." で囲まれた部分は空白を含む 1 token になる (--title "My Game" など)。
/// 先頭の token は通常 game DLL の相対パス。`mitiru dist` が生成する。
std::vector<std::string> readSidecarArgs(const char* argv0)
{
	std::vector<std::string> tokens;
	if (argv0 == nullptr) { return tokens; }
	std::error_code ec;
	const auto exe = std::filesystem::absolute(std::filesystem::path(argv0), ec);
	if (ec) { return tokens; }
	const auto side = exe.parent_path() / (exe.stem().string() + ".mtargs");
	std::ifstream f(side);
	if (!f) { return tokens; }
	// 簡易 quote lexer (CRT の command line 解釈とそろえる。backslash-escape は非対応)。
	std::string cur;
	bool inQuote  = false;
	bool sawToken = false;   // 空 quote ("") も 1 token として残す
	char c = 0;
	while (f.get(c))
	{
		if (c == '"') { inQuote = !inQuote; sawToken = true; continue; }
		if (!inQuote && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
		{
			if (sawToken || !cur.empty()) { tokens.push_back(cur); cur.clear(); sawToken = false; }
			continue;
		}
		cur.push_back(c);
		sawToken = true;
	}
	if (sawToken || !cur.empty()) { tokens.push_back(cur); }
	return tokens;
}

#ifdef _WIN32
/// AppUserModelID を設定する (--appid)。taskbar のグループ化/ピン留めの単位が exe パス
/// ではなく、この id になる。shell32 から動的に取得し、存在しない環境では何も知らせずに続行する。
void applyAppUserModelId(const std::string& id)
{
	const int wideLen = MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, nullptr, 0);
	if (wideLen <= 0) { return; }
	std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, id.c_str(), -1, wide.data(), wideLen);
	using SetAumidFn = HRESULT(WINAPI*)(PCWSTR);
	if (HMODULE shell32 = LoadLibraryW(L"shell32.dll"))
	{
		if (auto fn = reinterpret_cast<SetAumidFn>(
				GetProcAddress(shell32, "SetCurrentProcessExplicitAppUserModelID")))
		{
			fn(wide.c_str());
		}
	}
}
#endif

/// DLL の隣にある assets/ui/main.rml (RmlUi の UI 文書) を返す。存在しないか、RmlUi なしのビルドなら空。
std::string defaultUiDocumentFor(const std::filesystem::path& dllPath)
{
#if defined(MITIRU_HAS_RMLUI)
	std::error_code ec;
	const auto doc = dllPath.parent_path() / "assets" / "ui" / "main.rml";
	if (std::filesystem::exists(doc, ec))
	{
		const std::u8string u = std::filesystem::absolute(doc, ec).generic_u8string();
		return std::string(u.begin(), u.end());   // RmlUiHost はパスを UTF-8 で受ける
	}
#else
	(void)dllPath;
#endif
	return {};
}

struct CliArgs
{
	std::filesystem::path dllPath;
	std::string           title;               // --title <name>: window title (空=既定 DLL 名の stem)
	std::string           iconPath;            // --icon <f.ico>: window icon (空=既定 icon)
	std::string           appId;               // --appid <id>: AppUserModelID (taskbar 分離、空=設定しない)
	bool                  watch = false;
	std::string           watchAssetsDir;     // --watch-assets <dir>: 配下の .json/.baked/.talk/モデルの mtime を見て asset.reloaded を game へ通知 (モデルは描画も読み直す)
	bool                  configOrigins = false; // --config-origins: 設定値の由来を表で出して終了 (§3-3)
	bool                  helpRequested = false;
	bool                  helpAllRequested = false; // --help-all: printUsage() の全量版 (--help は既定 20 行に絞る, E8)
	int                   widthOverride  = 0;  // 0 = 既定 1280
	int                   heightOverride = 0;  // 0 = 既定 720
	int                   winPosX = (-2147483647 - 1);  // --window-pos X Y: 窓の初期座標 (既定=CW_USEDEFAULT)
	int                   winPosY = (-2147483647 - 1);  // 実画面に一瞬も出さず最初から指定位置へ (録画支援)
	int                   toolWinX = (-2147483647 - 1); // --tool-window-pos X Y: spawn する tool 窓の初期座標
	int                   toolWinY = (-2147483647 - 1);
	std::string           recordPath;          // --record <f>: .mtrr を書き出す
	std::string           replayPath;          // --replay-test/--replay <f>: 再実行 (ヘッドレス/GUI)
	bool                  replayGui = false;   // true なら --replay (window あり、verdict 無し、EOF で自動終了しない)
	std::string           ghostPath;           // --ghost <f>: 同じ DLL のもう1本の GameMemory に並走投入し、live の直前に半透明で描く (10-1)
	std::string           expectPath;          // --expect <f>: 最終 view.* 状態を検証
	std::string           stateDiffA;          // --state-diff <a> <b>: 2 録画の分岐 frame を報告
	std::string           stateDiffB;
	std::string           bakeInJson;          // --bake <in.json> <out.baked>: 配置 JSON を POD へ焼く (★4-1)
	std::string           bakeOutPath;
	std::string           parseError;         // 引数の形が壊れている時の説明 (空なら正常)
	bool                  backendSet = false; // --backend が指定された (既定値と同じ値でも由来は cli)
	bool                  speedSet   = false; // --speed が指定された
	std::string           fontMode;            // --font none (8x8 ビットマップ)。それ以外の値は既定の書体
	std::string           fontFace;            // --font-face normal|retro (空=normal=M+ Rounded)
	bool                  loFi = false;        // --lofi: 低解像+量子化+Bayerディザ
	int                   loFiW = 320, loFiH = 240; // --lofi-size WxH
	int                   loFiBitsR = 5, loFiBitsG = 6, loFiBitsB = 5; // --lofi-bits R,G,B (既定 RGB565)
	float                 loFiDither = 1.0f;   // --lofi-dither S
	bool                  loFiHard = false;    // --lofi-hard: 柔らか拡大を切りニアレストにする
	bool                  loFiVi = false;      // --lofi-vi: 映像出力段の de-dither + divot
	float                 loFiGamma = 1.0f;    // --lofi-gamma: 出力ガンマ (1 で素通し)
	mitiru::render::AntiAliasing3D antiAliasing3D = mitiru::render::AntiAliasing3D::MsaaFxaa;  // --aa fxaa|msaa|taa
	float                 motionBlur3D = 0.0f; // --motion-blur S: 3D の動きのぼけ (シャッターの割合 0..1)
	bool                  ssr3D = false;       // --ssr: 3D の画面の反射
	std::string           lightingBake;        // --lighting-bake <f>: mitiru_lightbake が焼いた光
	int                   httpPort = 0;        // --http-port <N>: EngineHttpServer を listen 開始
	bool                  console  = false;    // --console: HTTP + default browser で console.html 自動表示
	std::string           captureDir;          // --capture-dir <d>: 毎 N フレーム PNG を吐く先 (#43)
	int                   captureEvery = 0;    // --capture-every <N>: N フレームごとに 1 枚 (0=off)
	bool                  headless = false;    // --headless: ウィンドウ無しで走らせる (AI 自動回し)
	bool                  headlessGpu3D = false; // --headless-3d: headless でも実 GPU で 3D を描く (G2、--capture-dir と併用)
	mitiru::gfx::Backend  backend = mitiru::gfx::Backend::Auto;  // --backend <name>: 明示バックエンド指定
	std::string           replayGateDir;       // --replay-gate-dir <d>: O5 決定論ゲートの .mtrr 置き場
	bool                  oracleDeterminism = false;         // --oracle-determinism [N]: P14 決定論オラクル opt-in
	std::uint32_t         oracleDeterminismEveryFrames = 0;  // N (省略時 0 = engine 既定 120)
	bool                  oracleLog = false;   // --oracle-log: N1 の機械可読 "[oracle] ..." 行を stderr へ追加出力
	bool                  verbose = false;     // --verbose: 開発向けの詳細も端末に出す (MITIRU_LOG=verbose と同じ)
	bool                  synctest = false;    // --synctest [K]: GGPO SyncTest 相当。毎フレーム K フレーム前から再シミュレーション
	std::uint32_t         synctestFrames = 1;  // K (省略時 1)
	bool                  saveRoundtripTest = false; // --save-roundtrip-test: save→load→save が bit 一致するか検査
	float                 speed    = 1.0f;     // --speed <N>: time scale 倍率 (固定 dt × N で早回し)
	int                   maxFrames = 0;       // --max-frames <N>: N フレーム走ったら自動終了 (0=無制限)
	int                   rewindFrames = 0;    // --rewind-frames <N>: 巻き戻せるフレーム数 (0=既定 300)
	int                   rewindMb = -1;       // --rewind-mb <N>: 巻き戻しリングの予算 (MB、0=無制限=非圧縮)。
	                                           // -1=未指定 (ゲームの MITIRU_REWIND_BUDGET 宣言 > 既定 512MB)
	int                   rewindRawMb = -1;    // --rewind-raw-mb <N>: 生で持つ上限 (MB、0=予算まで生)。-1=既定 64MB
	int                   recordStateEvery = 60; // --record-state-every <N>: --record で state blob を書く間隔 (0=毎フレーム)
	bool                  fixedSize = false;   // --fixed-size: ユーザのウィンドウリサイズを禁止 (#44)
	bool                  noPauseUnfocused = false; // --no-pause-unfocused: 非フォーカスでもフルレート継続 (vsync off)
	bool                  noVsync = false;     // --no-vsync: present の vsync 待ちを切る (素のフレームコスト計測, #53)
	bool                  perf    = false;     // --perf: 実フレーム時間の統計を定期表示 (#53)
	std::string           frameTimes;          // --frame-times <f>: 各 host frame の実時間 (ms) を 1 行 1 値で書く
	std::string           perfLog;             // --perf-log <f>: フレームごとの CPU 時間・確保回数・GPU のパス別時間 (TSV)
	std::string           loads;               // --loads async|settled: 読み込み中の資産を描かずに進むか (既定は人が見る窓だけ async)
	std::string           preloadList;         // --preload <f>: 最初のフレームの前に読む資産の一覧
	std::vector<std::string> stages;           // --stage <f>@<n>: n フレーム目にステージを一覧 f に切り替える (計測用)
	std::string           bakeList;            // --bake-caches <f>: 一覧 f の資産を読んで cache を作り、止める (mitiru dist が使う)
	std::string           checkI18nDir;        // --check-i18n <d>: d の下の strings.json の訳の抜けと書体に無い字を TSV で出す (mitiru dist --check が使う)
	std::string           inputScript;         // --input-script <f>: in-process 入力注入 (#43-1)
	std::string           gameName;            // --game-name <name>: セーブと設定を %APPDATA%/<name>/ に置く
	std::string           saveDir;             // --save-dir <d>: セーブスロットの置き場
	std::string           settingsPath;        // --settings <f>: 利用者の設定ファイル
	std::string           stateTrace;          // --state-trace <f>: 毎フレーム reflect 状態を JSONL 出力 (offset read = non-POD でも安全)
	std::string           audioCapture;        // --audio-capture <wav>: headless の音を固定ステップごとに WAV へ (HostAudioCapture.hpp)
	std::string           windowShot;          // --window-shot <png>: 本物の窓を人の見ない場所に出して撮る (HostWindowShot.hpp)
	std::string           inputRecordPath;     // --input-record <f>: 実入力を input-script 形式で録画 (#45)
	std::vector<mitiru::Tool> openTools;       // --inspect <name>: 起動時に開くツール独立窓
	std::vector<std::string>  openToolArgs;    // openTools と同じ並び。"scene?tab=memory" のクエリを --page ごと渡す (無ければ空)
	std::string           errorFile;           // --error-file <f>: mitiru watch のビルドエラー帯 (存在中だけ表示)
	std::string           pauseControl;        // --pause-control <f>: ファイルが "1" の間だけ pause (録画支援, フォーカス不要)
	std::string           inputFreezeControl;  // --input-freeze-control <f>: "1" の間だけ入力を無効化 (プレイヤー静止・世界は進行, 録画支援)
	bool                  noToolWindows = false; // --no-tool-windows: hud.open 等のツール窓 spawn を全無効 (録画/CI でメイン画面への割込防止)
	bool                  noActivate = false;  // --no-activate: 窓を画面外・非アクティブで出す (--input-script 等でも自動で立つ)
	bool                  unpaced = false;     // --unpaced: 台本実行の 60Hz ペーシングを外す (#72、素の到達フレームレート計測用)
	int                   paceFps = 60;        // --pace-fps N: 台本実行のペーシング目標 (#72、リプレイの固定ステップと同じ 60 が既定)
	bool                  jsonOutput = false;  // --json: --replay-test の verdict を stdout に 1 行 JSON でも出す (CLI 側の正規表現パースを置換)
	bool                  bugRingSave = false; // --bug-ring-save: 起動直後に「昨日のバグ」リングを即保存 (P1、通常は F11 ホットキー)
	std::string           packOverride;        // --pack <file.mtpak>: 自動探索 (assets.mtpak) より優先する明示パック (P12)
	std::string           collisionPath;       // --collision <terrain.json>: 物理問い合わせ job (v37) が答える静的な地形 (箱・三角形メッシュ)
	std::string           unknownOption;       // 未知の --option (非空 = 起動拒否。typo / 廃止 flag を黙殺しない)
	mitiru::host::OnlineArgs online;           // --net / --net-host / --net-join ほか (HostOnline.hpp)
};

/// 省略できる数の引数 (`--synctest [K]`) は数字だけの時に取る。DLL のパスを K と取り違えない。
[[nodiscard]] bool isOptionalCount(const char* arg) noexcept
{
	if (arg == nullptr || *arg == '\0') { return false; }
	for (const char* p = arg; *p != '\0'; ++p) { if (*p < '0' || *p > '9') { return false; } }
	return true;
}

CliArgs parseArgs(int argc, char* argv[])
{
	CliArgs out;
	if (argc < 2) { out.helpRequested = true; return out; }

	for (int i = 1; i < argc; ++i)
	{
		std::string_view a{argv[i]};
		if (a == "--help" || a == "-h")
		{
			out.helpRequested = true;
		}
		else if (a == "--help-all")
		{
			out.helpRequested = true;
			out.helpAllRequested = true;
		}
		else if (a == "--watch")
		{
			out.watch = true;
		}
		else if (a == "--watch-assets")
		{
			if (i + 1 < argc) { out.watchAssetsDir = argv[++i]; }
			else { out.parseError = "--watch-assets にはフォルダを指定してください。"; }
		}
		else if (a == "--config-origins")
		{
			// §3-3: 「この設定値はどこで決まったか」を表で出し、すぐに終了する診断フラグ。
			out.configOrigins = true;
		}
		else if (a == "--title")
		{
			if (i + 1 < argc) { out.title = argv[++i]; }
		}
		else if (a == "--icon")
		{
			if (i + 1 < argc) { out.iconPath = argv[++i]; }
		}
		else if (a == "--appid")
		{
			if (i + 1 < argc) { out.appId = argv[++i]; }
		}
		else if (a == "--record")
		{
			if (i + 1 < argc) { out.recordPath = argv[++i]; }
		}
		else if (a == "--replay-test")
		{
			if (i + 1 < argc) { out.replayPath = argv[++i]; out.replayGui = false; }
		}
		else if (a == "--replay")
		{
			// GUI 再生: --replay-test と同じ決定的な再実行だが、window あり・ツール窓も開ける・
			// --expect 判定なし・EOF で自動終了せず、最後のフレームで停止する (デモ撮影用)。
			if (i + 1 < argc) { out.replayPath = argv[++i]; out.replayGui = true; }
		}
		else if (a == "--json")
		{
			out.jsonOutput = true;
		}
		else if (a == "--ghost")
		{
			if (i + 1 < argc) { out.ghostPath = argv[++i]; }
		}
		else if (a == "--expect")
		{
			if (i + 1 < argc) { out.expectPath = argv[++i]; }
		}
		else if (a == "--state-diff")
		{
			if (i + 2 < argc) { out.stateDiffA = argv[++i]; out.stateDiffB = argv[++i]; }
		}
		else if (a == "--bake")
		{
			if (i + 2 < argc) { out.bakeInJson = argv[++i]; out.bakeOutPath = argv[++i]; }
			else { out.parseError = "--bake には <in.json> <out.baked> の 2 つを指定してください。"; }
		}
		else if (a == "--font")
		{
			if (i + 1 < argc) { out.fontMode = argv[++i]; }
		}
		else if (a == "--font-face")
		{
			if (i + 1 < argc) { out.fontFace = argv[++i]; }
		}
		else if (a == "--http-port")
		{
			if (i + 1 < argc)
			{
				try { out.httpPort = std::stoi(argv[++i]); }
				catch (...) { out.httpPort = 0; }
			}
		}
		else if (a == "--console")
		{
			out.console = true;
		}
		else if (a == "--capture-dir")
		{
			if (i + 1 < argc) { out.captureDir = argv[++i]; }
		}
		else if (a == "--capture-every")
		{
			if (i + 1 < argc) { try { out.captureEvery = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--headless")
		{
			out.headless = true;
		}
		else if (a == "--headless-3d")
		{
			out.headless = true;
			out.headlessGpu3D = true;
		}
		else if (a == "--backend")
		{
			out.backendSet = true;
			// --backend <auto|dx11|dx12>: --headless-3d の windowless 3D 経路 (G2、
			// gfx::createWindowlessDevice3D) が Dx11 固定だったため、明示的に指定できるように
			// する。Vulkan/OpenGL/WebGL/WebGPU は windowless 3D に対応していない (GfxFactory.hpp 参照)
			// ため受け付けない (綴り違い・未対応の名前では auto のままとし、その旨を stderr に出す)。
			if (i + 1 < argc)
			{
				const std::string name{argv[++i]};
				if      (name == "auto") { out.backend = mitiru::gfx::Backend::Auto; }
				else if (name == "dx11") { out.backend = mitiru::gfx::Backend::Dx11; }
				else if (name == "dx12") { out.backend = mitiru::gfx::Backend::Dx12; }
				else
				{
					mitiru::console::noticef("--backend %s は使えないので auto で起動します。使える値は auto / dx11 / dx12 です。",
					                         name.c_str());
				}
			}
		}
		else if (a == "--no-pause-unfocused")
		{
			out.noPauseUnfocused = true;
		}
		else if (a == "--no-vsync")
		{
			out.noVsync = true;
		}
		else if (a == "--perf")
		{
			out.perf = true;
		}
		else if (a == "--perf-log")
		{
			if (i + 1 < argc) { out.perfLog = argv[++i]; }
			else { out.parseError = "--perf-log には書き出す先の .tsv を指定してください。"; }
		}
		else if (a == "--loads")
		{
			if (i + 1 < argc) { out.loads = argv[++i]; }
			if (out.loads != "async" && out.loads != "settled") { out.parseError = "--loads には async か settled を指定してください。"; }
		}
		else if (a == "--preload")
		{
			if (i + 1 < argc) { out.preloadList = argv[++i]; }
			else { out.parseError = "--preload には資産の一覧のファイルを指定してください。"; }
		}
		else if (a == "--stage")
		{
			if (i + 1 < argc) { out.stages.emplace_back(argv[++i]); }
			else { out.parseError = "--stage には <一覧のファイル>@<フレーム番号> を指定してください。"; }
		}
		else if (a == "--check-i18n")
		{
			if (i + 1 < argc) { out.checkI18nDir = argv[++i]; }
			else { out.parseError = "--check-i18n には strings.json を探すフォルダを指定してください。"; }
		}
		else if (a == "--bake-caches")
		{
			// 窓を出さずに DX12 で読み、2 フレーム目に止める。読み込みは待つので cache は止める前に全部できている
			if (i + 1 < argc) { out.bakeList = argv[++i]; }
			else { out.parseError = "--bake-caches には資産の一覧のファイルを指定してください。"; }
			out.headless = true;
			out.headlessGpu3D = true;
			out.backend = mitiru::gfx::Backend::Dx12;
			out.backendSet = true;
			out.noToolWindows = true;
			out.loads = "settled";
			out.maxFrames = 3;
		}
		else if (a == "--frame-times")
		{
			if (i + 1 < argc) { out.frameTimes = argv[++i]; }
			else { out.parseError = "--frame-times には書き出す先のファイルを指定してください。"; }
		}
		else if (a == "--speed")
		{
			if (i + 1 < argc) { try { out.speed = std::stof(argv[++i]); out.speedSet = true; } catch (...) {} }
		}
		else if (a == "--max-frames")
		{
			if (i + 1 < argc) { try { out.maxFrames = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--fixed-size")
		{
			out.fixedSize = true;
		}
		else if (a == "--rewind-frames")
		{
			if (i + 1 < argc) { try { out.rewindFrames = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--rewind-mb")
		{
			if (i + 1 < argc) { try { out.rewindMb = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--rewind-raw-mb")
		{
			if (i + 1 < argc) { try { out.rewindRawMb = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--oracle-determinism")
		{
			// --oracle-determinism [everyFrames]: P14 の決定論オラクル (resim ring + memcmp) を
			// opt-in する。N の省略時は EngineConfig::oracleDeterminismEveryFrames=0 のまま
			// (engine 側が既定の 120 フレーム間隔を使う)。
			out.oracleDeterminism = true;
			if (i + 1 < argc && isOptionalCount(argv[i + 1]))
			{
				try
				{
					const unsigned long v = std::stoul(argv[++i]);
					if (v > 0xFFFFFFFFul) { out.parseError = "--oracle-determinism の間隔が大きすぎます。4294967295 以下にしてください。"; }
					else { out.oracleDeterminismEveryFrames = static_cast<std::uint32_t>(v); }
				}
				catch (...) { out.parseError = "--oracle-determinism の間隔には数を指定してください。"; }
			}
		}
		else if (a == "--oracle-log")
		{
			out.oracleLog = true;
		}
		else if (a == "--verbose")
		{
			out.verbose = true;
		}
		else if (a == "--synctest")
		{
			// GGPO SyncTest 相当 (Developer Guide の ggpo_start_synctest)。既存の決定論オラクル
			// (resim ring + memcmp) を K=1 (毎フレーム) で実行するだけなので、oracle-determinism の
			// 単純な別名として実装する。K の省略時は 1。
			out.synctest = true;
			if (i + 1 < argc && isOptionalCount(argv[i + 1]))
			{
				try { out.synctestFrames = static_cast<std::uint32_t>(std::stoul(argv[++i])); }
				catch (...) { out.parseError = "--synctest の K が大きすぎます。4294967295 以下にしてください。"; }
			}
		}
		else if (a == "--save-roundtrip-test")
		{
			out.saveRoundtripTest = true;
		}
		else if (a == "--record-state-every")
		{
			if (i + 1 < argc) { try { out.recordStateEvery = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--input-script")
		{
			if (i + 1 < argc) { out.inputScript = argv[++i]; }
		}
		else if (a == "--game-name")
		{
			if (i + 1 < argc) { out.gameName = argv[++i]; }
		}
		else if (a == "--save-dir")
		{
			if (i + 1 < argc) { out.saveDir = argv[++i]; }
		}
		else if (a == "--settings")
		{
			if (i + 1 < argc) { out.settingsPath = argv[++i]; }
		}
		else if (a == "--window-shot")
		{
			if (i + 1 < argc) { out.windowShot = argv[++i]; }
			else { out.parseError = "--window-shot には書き出す先の .png を指定してください。"; }
		}
		else if (a == "--audio-capture")
		{
			if (i + 1 < argc) { out.audioCapture = argv[++i]; }
			else { out.parseError = "--audio-capture には書き出す先の .wav を指定してください。"; }
		}
		else if (a == "--state-trace")
		{
			if (i + 1 < argc) { out.stateTrace = argv[++i]; }
		}
		else if (a == "--input-record")
		{
			if (i + 1 < argc) { out.inputRecordPath = argv[++i]; }
		}
		else if (a == "--error-file")
		{
			if (i + 1 < argc) { out.errorFile = argv[++i]; }
		}
		else if (a == "--pause-control")
		{
			if (i + 1 < argc) { out.pauseControl = argv[++i]; }
		}
		else if (a == "--input-freeze-control")
		{
			if (i + 1 < argc) { out.inputFreezeControl = argv[++i]; }
		}
		else if (a == "--no-tool-windows")
		{
			out.noToolWindows = true;
		}
		else if (a == "--no-activate")
		{
			out.noActivate = true;
		}
		else if (a == "--unpaced")
		{
			out.unpaced = true;
		}
		else if (a == "--pace-fps")
		{
			if (i + 1 < argc) { out.paceFps = std::atoi(argv[++i]); }
			if (out.paceFps <= 0) { out.parseError = "--pace-fps には 1 以上を指定してください。ペースを外すなら --unpaced を使います。"; }
		}
		else if (a == "--bug-ring-save")
		{
			out.bugRingSave = true;
		}
		else if (a == "--pack")
		{
			if (i + 1 < argc) { out.packOverride = argv[++i]; }
		}
		else if (a == "--collision")
		{
			if (i + 1 < argc) { out.collisionPath = argv[++i]; }
			else { out.parseError = "--collision には地形の JSON を指定してください。"; }
		}
		else if (a == "--replay-gate-dir")
		{
			// ADR 0035 O5: POST /api/ai/commit の直後に実行する決定論ゲートの録画の保存場所
			// (既定 tests/replay_golden、Engine_Http.hpp::runReplayGate の env 名と一致)。
			if (i + 1 < argc) { out.replayGateDir = argv[++i]; }
		}
		else if (a == "--window-pos")
		{
			// --window-pos X Y: 窓を最初からこの座標に表示する (負の X = 仮想ディスプレイなど)
			if (i + 2 < argc)
			{
				try { out.winPosX = std::stoi(argv[i + 1]); out.winPosY = std::stoi(argv[i + 2]); i += 2; }
				catch (...) {}
			}
		}
		else if (a == "--tool-window-pos")
		{
			// --tool-window-pos X Y: spawn するツール窓もこの座標に出す (録画で実画面に出さない)
			if (i + 2 < argc)
			{
				try { out.toolWinX = std::stoi(argv[i + 1]); out.toolWinY = std::stoi(argv[i + 2]); i += 2; }
				catch (...) {}
			}
		}
		else if (a == "--inspect")
		{
			// --inspect [inspector|input|rewind] (省略時 inspector)。
			// host を書く人が「この窓を使う」と決めたものだけを開く。
			mitiru::Tool t = mitiru::Tool::Inspector;
			std::string extraArgs;
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				const std::string full{argv[++i]};
				// "scene?tab=memory" の ? 以降はページへのクエリ。名前解決には使わず、
				// mitiru_tool へ --page ごと渡し、ページが読む (mitiru run --learn)。
				const std::string name = full.substr(0, full.find('?'));
				if (name.size() < full.size()) { extraArgs = "--page " + full; }
				if      (name == "input")      { t = mitiru::Tool::InputMonitor; }
				else if (name == "rewind")     { t = mitiru::Tool::Rewind; }
				else if (name == "scene")      { t = mitiru::Tool::SceneTree; }
				else if (name == "perf")       { t = mitiru::Tool::Perf; }
				else if (name == "mixer")      { t = mitiru::Tool::AudioMixer; }
				else if (name == "scene_view") { t = mitiru::Tool::SceneView; }
				else if (name == "why_view")   { t = mitiru::Tool::WhyView; }
				else if (name == "frame_view") { t = mitiru::Tool::FrameView; }
				else if (name == "side_state") { t = mitiru::Tool::SideState; }
				else if (name == "ai")         { t = mitiru::Tool::Ai; }
				else if (name == "nav")        { t = mitiru::Tool::Nav; }
				else if (name == "anim")       { t = mitiru::Tool::Anim; }
				else if (name == "story")      { t = mitiru::Tool::Story; }
				else if (name != "inspector")
				{
					// 綴り違いを何も知らせずに既定値として扱うと、要求した窓とは別の窓が開いたまま
					// 気づけない。開くものは変えず、その旨だけを伝える
					mitiru::console::noticef("--inspect %s という窓は無いので inspector を開きます。使える名前は input / rewind / "
					                         "scene / perf / mixer / scene_view / why_view / frame_view / side_state / ai / nav / anim / story です。",
					                         name.c_str());
				}
			}
			out.openTools.push_back(t);
			out.openToolArgs.push_back(extraArgs);
		}
		else if (a == "--size")
		{
			// 形式: WxH、例 "800x500"。両方とも正の整数であること。
			if (i + 1 < argc)
			{
				std::string s{argv[++i]};
				auto x = s.find('x');
				if (x == std::string::npos) { x = s.find('X'); }
				if (x != std::string::npos)
				{
					try {
						int w = std::stoi(s.substr(0, x));
						int h = std::stoi(s.substr(x + 1));
						if (w > 0 && h > 0) {
							out.widthOverride  = w;
							out.heightOverride = h;
						}
					} catch (...) {}
				}
			}
		}
		else if (a == "--lofi")
		{
			out.loFi = true;
		}
		else if (a == "--lofi-hard")
		{
			out.loFiHard = true;
			out.loFi = true;
		}
		else if (a == "--lofi-vi")
		{
			out.loFiVi = true;
			out.loFi = true;
		}
		else if (a == "--lofi-gamma")
		{
			if (i + 1 < argc) { out.loFiGamma = std::strtof(argv[++i], nullptr); }
			out.loFi = true;
		}
		else if (a == "--lofi-size")
		{
			if (i + 1 < argc)
			{
				std::string s{argv[++i]};
				auto x = s.find('x'); if (x == std::string::npos) x = s.find('X');
				if (x != std::string::npos)
				{
					try {
						int w = std::stoi(s.substr(0, x)), h = std::stoi(s.substr(x + 1));
						if (w > 0 && h > 0) { out.loFiW = w; out.loFiH = h; out.loFi = true; }
					} catch (...) {}
				}
			}
		}
		else if (a == "--lofi-bits")
		{
			// 形式: R,G,B、例 "5,6,5"(RGB565) / "3,3,2"(256 色相当)
			if (i + 1 < argc)
			{
				std::string s{argv[++i]};
				try {
					auto c1 = s.find(','), c2 = s.find(',', c1 + 1);
					if (c1 != std::string::npos && c2 != std::string::npos) {
						out.loFiBitsR = std::stoi(s.substr(0, c1));
						out.loFiBitsG = std::stoi(s.substr(c1 + 1, c2 - c1 - 1));
						out.loFiBitsB = std::stoi(s.substr(c2 + 1));
						out.loFi = true;
					}
				} catch (...) {}
			}
		}
		else if (a == "--aa")
		{
			const std::string_view v = (i + 1 < argc) ? std::string_view(argv[++i]) : std::string_view{};
			if (v == "fxaa") { out.antiAliasing3D = mitiru::render::AntiAliasing3D::MsaaFxaa; }
			else if (v == "msaa") { out.antiAliasing3D = mitiru::render::AntiAliasing3D::Msaa; }
			else if (v == "taa") { out.antiAliasing3D = mitiru::render::AntiAliasing3D::Taa; }
			else if (out.parseError.empty()) { out.parseError = "--aa には fxaa / msaa / taa のどれかを指定してください。"; }
		}
		else if (a == "--motion-blur")
		{
			if (i + 1 < argc) { try { out.motionBlur3D = std::stof(argv[++i]); } catch (...) {} }
		}
		else if (a == "--ssr") { out.ssr3D = true; }
		else if (a == "--lighting-bake" && i + 1 < argc) { out.lightingBake = argv[++i]; }
		else if (a == "--lofi-dither")
		{
			if (i + 1 < argc) { try { out.loFiDither = std::stof(argv[++i]); out.loFi = true; } catch (...) {} }
		}
		else if (mitiru::host::parseOnlineArg(a, argc, argv, i, out.online))
		{
		}
		else if (a.rfind("--", 0) == 0)
		{
			// 未知の --option は positional として扱わず拒否する (typo / 廃止された flag を何も知らせずに無視することを防ぐ)。
			if (out.unknownOption.empty()) { out.unknownOption = std::string(a); }
		}
		else if (out.dllPath.empty())
		{
			out.dllPath = a;
		}
		else if (out.parseError.empty())
		{
			out.parseError = "DLL の後ろに置ける引数は -- で始まるものだけです (" + std::string(a) + " は使えません)。";
		}
	}

	// 窓のある実行では音をスピーカーへ出すので、同じ engine から書き出しへは回せない。
	if (!out.audioCapture.empty() && !out.headless && out.parseError.empty())
	{
		out.parseError = "--audio-capture は --headless か --headless-3d と一緒に使ってください。";
	}
	if (!out.windowShot.empty())
	{
		if (out.headless && out.parseError.empty())
		{
			out.parseError = "--window-shot は本物の窓を撮るので、--headless と一緒には使えません。";
		}
		out.noActivate = true;  // 人が見ていない実行。前に出さない・フォーカスを取らない・60Hz に揃える
		if (out.maxFrames <= 0) { out.maxFrames = 60; }
	}

	// 環境変数 MITIRU_WATCH=1 は --watch と同じ。
	if (const char* envWatch = std::getenv("MITIRU_WATCH");
	    envWatch && envWatch[0] != '\0' && std::string{envWatch} != "0")
	{
		out.watch = true;
	}

	// オンラインでは手元の入力だけでは結果が決まらないので、記録・再生・差し替え・手元の巻き戻しの検査と合わせない。
	if (out.online.any() && out.parseError.empty()
	    && (!out.recordPath.empty() || !out.replayPath.empty() || out.watch || !out.ghostPath.empty() || out.synctest))
	{
		out.parseError = "--net で始まる引数は --record / --replay / --watch (MITIRU_WATCH) / --ghost / --synctest と一緒には使えません。";
	}
	return out;
}

/// 既定の --help (E8: 55 行は多すぎるため、日常的に使う一部だけに絞る。全量は --help-all)。
void printUsageShort()
{
	std::fprintf(stderr,
		"usage: mitiru_host <game.dll> [options]\n"
		"\n"
		"common options:\n"
		"  --watch          hot-reload the DLL when it is rebuilt\n"
		"  --size WxH       override window size (e.g. --size 800x500)\n"
		"  --headless       ウィンドウ無しで走らせる (AI 自動プレイ/CI)\n"
		"  --record F       毎フレームの入力と state を .mtrr F に記録する\n"
		"  --replay-test F  .mtrr F をヘッドレスで再実行し bit-exact 照合 (CI ゲート)\n"
		"  --replay F       .mtrr F を window ありで GUI 再生 (デモ撮影用)\n"
		"  --input-script F 入力スクリプト F を in-process 注入 (OS 入力を経由しない)\n"
		"  --inspect [name] ツール独立窓を起動時に開く\n"
		"  --http-port N    HTTP API を 127.0.0.1:N で開始\n"
		"  --json           --replay-test の verdict を stdout に 1 行 JSON でも出す\n"
		"  --verbose        開発向けの詳細 (GPU・読み込み・フレーム時間) も端末に出す\n"
		"  --help, -h       this message\n"
		"  --help-all       全オプション・hotkeys・environment を表示\n"
		"\n"
		"keys while running: F7 step  F8 pause  F9 speed  F10 lo-fi  F11 save the last 30 s  F12 screenshot\n"
		"\n"
		"docs/FIRST_TOUCH.md に最初の一歩。全オプションは --help-all で。\n");
}

void printUsage(bool full)
{
	if (!full) { printUsageShort(); return; }
	std::fprintf(stderr,
		"usage: mitiru_host <game.dll> [options]\n"
		"\n"
		"options:\n"
		"  --watch          hot-reload the DLL when it is rebuilt\n"
		"  --watch-assets D 配下の .json/.baked/.talk/.gltf/.glb/.obj/.fbx が変わったら game に asset.reloaded {path,hash} を届ける\n"
		"                   (録画に乗る)。モデルは描画側も次の drawModel で読み直す\n"
		"  --title <name>   window title (既定 = DLL ファイル名の stem。配布時は mitiru dist が project 名を書く)\n"
		"  --icon <f.ico>   window icon を .ico ファイルで差し替え (Windows)\n"
		"  --game-name N    セーブを %%APPDATA%%/N/saves、設定を %%APPDATA%%/N/settings.json に置く (DLL の隣の ship.json でも決まる)\n"
		"  --save-dir D     セーブスロット (hud.save) の置き場 (既定 = 作業フォルダの save/)\n"
		"  --settings F     利用者の設定ファイル (画面・音量・キー割り当て・アクセシビリティ)\n"
		"  --appid <id>     AppUserModelID を設定 (taskbar のグループ/ピン留めをゲーム単位に分離)\n"
		"  --size WxH       override window size (e.g. --size 800x500)\n"
		"  --no-pause-unfocused  keep running at full rate when window is unfocused\n"
		"  --no-vsync       present の vsync 待ちを切る (フレームコストの素を計測する用)\n"
		"  --perf           実フレーム時間の統計 (avg/p50/p95/max) を 600 フレームごとに表示\n"
		"                   GPU 実機の描画コスト計測は windowed + --perf --no-vsync で\n"
		"  --frame-times F  各フレームの実時間 (ms) を 1 行 1 値で F に書く (統計は tools/selfcheck.py)\n"
		"  --perf-log F     フレームごとの CPU 時間・確保回数・GPU のパス別時間を TSV で F に書く\n"
		"                   (GPU のタイムスタンプを打つので少し重くなる。集計は tools/perf_bench.py)\n"
		"  --loads M        async = 読み込み中の資産を描かずに進む / settled = 描く前に読み終える\n"
		"                   (既定は窓のある普通の実行だけ async。headless・撮影・台本・リプレイは settled)\n"
		"  --preload F      最初のフレームの前に F の資産を読み、パイプラインも作っておく (docs/STREAMING.md)\n"
		"  --stage F@N      N フレーム目にステージを F の一覧へ切り替える (前だけの資産は手放す。計測用、何度でも)\n"
		"  --bake-caches F  F の資産を窓なしで読み、変換と DDS とシェーダーの cache を作って止める。読めない資産があれば exit 4\n"
		"                   (MITIRU_SHADER_CACHE で置き場を決める。exe の隣の shader_cache は読むだけの置き場になる)\n"
		"  --check-i18n D   D の下の strings.json を読み、訳の抜けと書体に無い字を 1 行 1 件の TSV で出して止める (DLL は要らない)\n"
		"  --font <mode>    none = 8x8 ビットマップで描く (既定は同梱の書体。latin/kana/japanese も既定と同じ)\n"
		"  --font-face <f>  normal|retro — 普通(M+ Rounded) / レトロ(PixelMplus) (既定 normal)\n"
		"  --lofi           低解像描画+パレット量子化+Bayerディザ (DX12, DirectX5期の質感)\n"
		"  --lofi-size WxH  内部解像度 (既定 320x240)\n"
		"  --lofi-bits R,G,B  量子化ビット数 (既定 5,6,5=RGB565 / 3,3,2=256色相当)\n"
		"  --lofi-dither S  ディザ強度 (既定 1.0, 0=ディザ無し)\n"
		"  --http-port N    HTTP API を 127.0.0.1:N で開始 (実行中のゲームを外部から操作/観測)\n"
		"  --console        HTTP 起動 + 既定ブラウザで control panel を自動表示 (port 既定 8090)\n"
		"  --capture-dir D  毎 N フレームのフレームを PNG 連番で D に吐く (自動の見た目検証用)\n"
		"  --capture-every N  上記の間隔 (フレーム数。--capture-dir 指定時の既定 30)\n"
		"  --headless       ウィンドウ無しで走らせる (AI 自動プレイの裏回し)\n"
		"  --headless-3d    --headless でも実 GPU で 3D を描く (既定は NullDevice で 2D のみ、\n"
		"                   --capture-dir で 3D シーンの見た目を撮りたい時に使う。G2、既定 backend は DX11、\n"
		"                   --backend dx12 で DX12 windowless に切り替え可)\n"
		"  --backend NAME   グラフィックスバックエンドを明示指定する (auto|dx11|dx12。既定 auto)\n"
		"  --speed N        time scale 倍率 (固定 dt × N で早回し。長いプレイの自動回し用)\n"
		"  --max-frames N   N フレーム走ったら自動終了 (--headless 自動回しの停止条件)\n"
		"  --fixed-size     ユーザのウィンドウリサイズを禁止 (固定解像度運用)\n"
		"  --record F       毎フレームの入力と state を .mtrr F に記録する (--replay-test と排他)\n"
		"  --record-state-every N  --record 時、state blob (GameMemory 全体) を N フレームごとに\n"
		"                   記録する (既定 60。0=毎フレーム。save/load を跨ぐフレームは周期を無視して必ず記録)\n"
		"  --replay-test F  .mtrr F をヘッドレスで再実行し、記録済み GameMemory と bit-exact 照合する\n"
		"                   (CI リグレッションゲート。相違があれば非ゼロ終了)\n"
		"  --oracle-log     組込オラクル違反を機械可読な \"[oracle] ...\" 行として stderr に追加出力する\n"
		"                   (mitiru hunt の ScanOracleLines 用。既定 OFF)\n"
		"  --verbose        開発向けの詳細 (GPU のアダプタ、読み込み、フレーム時間の跳ね、各機能の準備) も\n"
		"                   端末に出す。何も指定しない普通の起動は何も出さない (MITIRU_LOG=verbose と同じ)\n"
		"  --rewind-frames N  巻き戻せるフレーム数 (既定 300 = 約 5 秒。60fps 基準)\n"
		"  --rewind-mb N    巻き戻しリングのメモリ予算 (MB、既定 512。ゲームが\n"
		"                   MITIRU_REWIND_BUDGET を宣言していれば未指定時はそちらが優先。\n"
		"                   0=無制限=非圧縮)。\n"
		"                   予算に収まらない古いフレームから捨てる\n"
		"  --rewind-raw-mb N  巻き戻しリングを生で持つ上限 (MB、既定 64)。超えると差分で\n"
		"                   圧縮する (0=予算まで生)。半分も縮まない状態は自動で生に戻す\n"
		"  --oracle-determinism [N]  決定論オラクル (N1e) を opt-in。K フレーム前から\n"
		"                   resim ring で再シミュレーションし、現在の GameMemory と memcmp する\n"
		"                   (N=実行間隔フレーム数、省略時 既定 120)。既定 OFF (GameMemory サイズに\n"
		"                   比例したコストがかかるため明示指定のみ有効)\n"
		"  --ghost F        .mtrr F を同じ DLL のもう1本の GameMemory に並走投入し、\n"
		"                   live の直前に半透明で描く (タイムアタックのゴースト、10-1)。\n"
		"                   --replay と併用すれば ghost vs live の同時再生になる。\n"
		"                   F の入力が尽きたら最後のフレームで静止する\n"
		"  --input-script F 入力スクリプト F を in-process 注入 (OS 入力を経由せず他アプリに漏れない)\n"
		"                   行の形式: <frame> <KEY> <down|up> / <frame> move <dx> <dy> [frames] /\n"
		"                   <frame> pos <x> <y> [frames] (カーソルの絶対座標。frames で直線移動)\n"
		"                   形式: 1 行 '<frame> <down|up> <KEY>' (# でコメント)。KEY=Left/Right/Up/Down/\n"
		"                   Space/Enter/Escape/英数字1字/MouseL/MouseR/MouseM/生 VK 整数。\n"
		"                   '<frame> move <dx> <dy> [frames]' でマウス delta (FPS 視線) も注入できる。\n"
		"                   パッドは '<frame> pad[N] <A|B|X|Y|LB|RB|Start|Back|LS|RS|Up|Down|Left|Right> <down|up>'\n"
		"                   と '<frame> axis[N] <LX|LY|RX|RY|LT|RT> <値>' (N=1..4、実機なしで届く)。\n"
		"                   実キーボード・実マウスは無視される\n"
		"  --state-trace F  MITIRU_REFLECT で申告したフィールド値を毎フレーム JSONL で F に出力。\n"
		"                   --input-script と併用し、プレイの軌跡や HP 推移を後から解析できる\n"
		"  --audio-capture F  headless の音を 48kHz ステレオの WAV F に書く (1 ステップ = 1/60 秒で同期)。\n"
		"                   鳴らした音の記録は F の拡張子を .events.jsonl にしたファイル。tools/audio_report.py で読む\n"
		"  --input-record F 実プレイの入力を input-script 形式で F に録画 (--input-script で再生可)\n"
		"  --error-file F   ビルドエラーファイル F を監視し、存在する間だけ画面上部に帯を表示\n"
		"                   (mitiru watch が自動指定。直して保存 → ビルド成功で帯が消える)\n"
		"  --pause-control F ファイル F が \"1\" の間だけ pause (dt=0, 描画継続)。フォーカス不要。\n"
		"                   録画で「編集中は静止、ビルド後に再開」を作るのに使う\n"
		"  --input-freeze-control F  ファイル F が \"1\" の間だけ入力を無効化 (--input-script と併用)。\n"
		"                   プレイヤーは静止するが engine は進む (星などは動く)。録画支援\n"
		"  --no-tool-windows  hud.open 等のツール窓の spawn を全無効。録画/CI で\n"
		"                   望まない窓がメイン画面に出るのを防ぐ\n"
		"  --window-pos X Y ゲーム窓を最初からこの座標に出す (実画面に一瞬も出さない)。\n"
		"                   負の X = 仮想ディスプレイ等。録画支援\n"
		"  --no-activate    窓を画面外・非アクティブで出し、フォーカスを一度も奪わない。\n"
		"                   --input-script / --capture-dir / --headless-3d のいずれかが\n"
		"                   指定されていれば自動で立つ (spawn するツール窓にも継承する)\n"
		"  --window-shot F  本物の窓を仮想ディスプレイ (無ければ画面外) に前に出さずに開き、--max-frames\n"
		"                   (既定 60) の最後に DWM が合成した絵を PNG F に撮って終わる。窓が前面に出たら exit 8\n"
		"  --unpaced        上記の台本実行で自動的に入る 60Hz ペーシングを外す。\n"
		"                   素の到達フレームレートを測る時だけ使う (GPU を占有する)\n"
		"  --pace-fps N     ペーシングの目標 (既定 60 = リプレイの固定ステップ)。\n"
		"                   1 フレームが 1/N 秒より重い台本では、裏で動画を見ている人に\n"
		"                   GPU を返すために N を下げる\n"
		"  --replay F       .mtrr F を window ありで GUI 再生 (--inspect と併用可)。\n"
		"                   --replay-test と違い verdict 判定はせず、EOF で自動終了せず\n"
		"                   最後のフレームで止まる (デモ撮影・目視確認用)\n"
		"  --inspect [name] ツール独立窓を起動時に開く\n"
		"                   (name=inspector|input|rewind|scene|perf|mixer|scene_view|why_view|frame_view,\n"
		"                     既定 inspector)\n"
		"                   ※ host を書く人が main.cpp で mitiru::debug::openTool(Tool::X) と\n"
		"                     直接書けば、欲しい窓だけコードで指定できる\n"
		"  --tool-window-pos X Y  --inspect 等で spawn するツール窓の初期座標\n"
		"                   (--window-pos と別。メイン窓と重ねたくない録画で使う)\n"
		"  --net            オンライン協力プレイの待合室を開き、部屋を作るか参加するかを画面で選ぶ (docs/ONLINE.md)\n"
		"  --net-host P     port P で部屋を作る (全部の口で待つ)。127.0.0.1:P なら同じ PC からだけ受ける\n"
		"  --net-join A     住所 A (ip:port) か参加コードの部屋に参加する\n"
		"  --net-players N  部屋の人数 (2..4、既定 2)    --net-delay N  入力遅延 (既定 2 フレーム)\n"
		"  --net-mode M     部屋の方式: rollback (既定) か authority (host だけが進め、状態を配る。重い場面用)\n"
		"  --net-rate N     authority で状態を配る回数 (毎秒、60 の約数、既定 20)\n"
		"  --net-sim L,J,P  送る packet に遅延 L ms・揺れ J ms・落ち P %% を足す (試験用)\n"
		"  --net-frames N   frame N が確定したら checksum を出して終わる (食い違いがあれば exit 1)\n"
		"  --bug-ring-save  起動直後に「昨日のバグ」リング (P1) を即 .mtrr 保存。\n"
		"                   通常は実行中に F11 で保存する (このフラグは自動化/CI 用)\n"
		"  --pack F         資産パック F (.mtpak) を自動探索より優先してマウントする (P12)\n"
		"  --collision F    物理問い合わせ job (hud.raycast 等) が答える静的な地形の JSON (v37)\n"
		"                   [{min,max,layer}] の箱か {vertices,indices,layer} の三角形メッシュの列\n"
		"  --replay-gate-dir D  分岐エディタ「残す」直後に回す決定論ゲートの .mtrr 置き場\n"
		"                   (既定 tests/replay_golden。ADR 0035 O5)\n"
		"  --expect F       --replay-test 併用: 最終 view.* state を F (JSON) と diff し、\n"
		"                   不一致があれば非ゼロ終了 (CI リグレッションの期待値照合)\n"
		"  --state-diff A B 2 つの .mtrr A/B を比較し、GameMemory が分岐した最初の\n"
		"                   frame を報告して終了 (再生はしない)\n"
		"  --json           --replay-test の verdict (PASS/FAIL・diverged frame・diff)を\n"
		"                   stdout に 1 行 JSON でも出す (CLI 側の正規表現パース置換用)\n"
		"  --lofi-hard      --lofi の柔らか拡大を切りニアレスト拡大にする\n"
		"  --lofi-vi        --lofi の映像出力段に de-dither + divot を掛ける\n"
		"  --lofi-gamma G   --lofi 出力段のガンマ補正 (既定 1.0 = 素通し)\n"
		"  --aa M           3D の AA: fxaa (既定、MSAA 4x + FXAA) / msaa (MSAA 4x だけ) / taa (MSAA 4x + TAA)\n"
		"  --motion-blur S  3D の動きのぼけ。S はシャッターの割合 0..1 (既定 0 = 無効)\n"
		"  --ssr            3D の画面の反射 (PBR の材質と水面)\n"
		"  --lighting-bake F  mitiru_lightbake が焼いた光 (*.lighting.bin) で 3D の間接光を描く\n"
		"  --config-origins 起動時設定 (backend/size/speed/http-port/pack/rewind/...) の値と\n"
		"                   由来 (既定/CLI/環境変数) を表で出して終了する (§3-3)\n"
		"  --help, -h       this message (既定 20 行。--help-all で全部)\n"
		"  --help-all       このヘルプの全量 (全オプション・hotkeys・environment)\n"
		"\n"
		"hotkeys (runtime, Windows):\n"
		"  F7               paused 時に 1 フレーム step\n"
		"  F8               pause / play toggle (on_update dt=0 化、描画は継続)\n"
		"  F9               time-scale cycle (1x→0.5x→0.25x→2x→4x→1x)\n"
		"  F10              lo-fi post-FX toggle (DX12)\n"
		"  F11              bug ring (P1) を今すぐ .mtrr 保存\n"
		"  F12              screenshot — ./screenshots/frame_YYYYMMDD_HHMMSS.png + clipboard\n"
		"\n"
		"environment:\n"
		"  MITIRU_WATCH=1   same as --watch\n"
		"  MITIRU_LOG=verbose  same as --verbose (ゲームの DLL が出す詳細にも効く)\n"
		"  MITIRU_CRASH_DIR=D  game が落ちた時の報告 (.txt) と minidump (.dmp) の置き場\n"
		"                   (既定 %%LOCALAPPDATA%%\\MitiruEngine\\crashes。docs/CRASH_REPORTS.md)\n"
		"\n"
		"exit code: 0=正常 1/2=引数・ファイル不備 3=DLL を読めない 4/5=GPU を失った 6=game が落ちて止まった\n"
		"\n"
		"The host loads the game DLL via Engine::loadModule and drives the\n"
		"main loop. The DLL must export mitiru_module_load (see\n"
		"docs/adr/0005-host-game-c-abi-signal-flow.md).\n");
}

/// @brief §3-3: 「この設定値はどこで決まったか」を追跡する 1 行。`EngineConfig` は変更せず
/// (ABI/決定論の都合で Config.hpp は変更禁止)、host の引数解析側だけで由来を記録する。
struct ConfigOriginRow
{
	const char* name;    ///< EngineConfig 側のフィールド名 (表示用)
	std::string value;   ///< 実際に採用された値 (文字列化済み)
	const char* origin;  ///< "default" / "cli(--xxx)" / "env(XXX)" のいずれか
};

/// @brief `--backend` の enum を人が読める名前へ戻す (UE の CVar 一覧のように表示するため)。
inline const char* backendDisplayName(mitiru::gfx::Backend b) noexcept
{
	switch (b)
	{
		case mitiru::gfx::Backend::Auto: return "auto";
		case mitiru::gfx::Backend::Dx11: return "dx11";
		case mitiru::gfx::Backend::Dx12: return "dx12";
		default: return "other";
	}
}

/// @brief `cfg`/`args` から代表的な設定値の「値と由来」を集め、表として stdout へ出す。
/// UE の CVar 優先順位 (console > commandline > ini > constructor) と同じ考え方を、
/// Mitiru では「録画に含まれない state を増やさない」方針のもと、host の起動時設定だけに絞って示す。
void printConfigOrigins(const CliArgs& args, const mitiru::EngineConfig& cfg,
	const std::string& resolvedPackPath, bool aiOptIn)
{
	std::vector<ConfigOriginRow> rows;
	rows.push_back({ "gfxBackend", backendDisplayName(cfg.gfxBackend),
		args.backendSet ? "cli(--backend)" : "default" });
	rows.push_back({ "windowWidth/Height",
		std::to_string(cfg.windowWidth) + "x" + std::to_string(cfg.windowHeight),
		(args.widthOverride > 0 || args.heightOverride > 0) ? "cli(--size)" : "default" });
	rows.push_back({ "timeScale", std::to_string(cfg.timeScale),
		args.speedSet ? "cli(--speed)" : "default" });
	rows.push_back({ "enableHttpApi/httpApiPort",
		cfg.enableHttpApi ? std::to_string(cfg.httpApiPort) : "off",
		args.httpPort > 0 ? "cli(--http-port)"
			: (args.console ? "cli(--console)"
			: (aiOptIn ? "env(MITIRU_AI/MITIRU_AI_PORT)" : "default(off)")) });
	// host が pack を見つけられなかった場合は、Engine 側が MITIRU_PACK 環境変数を読む (Engine_Module_Loader)。
	const char* packEnv = std::getenv("MITIRU_PACK");
	const bool packFromEnv = resolvedPackPath.empty() && packEnv != nullptr && packEnv[0] != '\0';
	rows.push_back({ "packPath",
		packFromEnv ? std::string(packEnv) : (resolvedPackPath.empty() ? "(none)" : resolvedPackPath),
		!args.packOverride.empty() ? "cli(--pack)"
			: (packFromEnv ? "env(MITIRU_PACK)"
			: (resolvedPackPath.empty() ? "default(none)" : "auto-discovered")) });
	rows.push_back({ "timeTravelBufferFrames", std::to_string(cfg.timeTravelBufferFrames),
		args.rewindFrames > 0 ? "cli(--rewind-frames)" : "default" });
	rows.push_back({ "oracleDeterminism", cfg.oracleDeterminism ? "on" : "off",
		args.oracleDeterminism ? "cli(--oracle-determinism)"
			: (args.synctest ? "cli(--synctest)" : "default(off)") });
	rows.push_back({ "headless", cfg.headless ? "on" : "off",
		args.headless ? "cli(--headless)" : "default(off)" });

	std::fprintf(stdout, "config origin (default < env < cli, 高い方が勝つ):\n");
	for (const auto& r : rows)
	{
		std::fprintf(stdout, "  %-24s = %-20s [%s]\n", r.name, r.value.c_str(), r.origin);
	}
}

#ifdef _WIN32
/// RGBA8 のトップダウン pixel buffer を Windows clipboard に CF_DIB として格納する。
inline bool copyRgbaToClipboard(const std::uint8_t* rgba, int w, int h)
{
	if (!rgba || w <= 0 || h <= 0) { return false; }

	const std::size_t headerSize = sizeof(BITMAPINFOHEADER);
	const std::size_t pixelBytes = static_cast<std::size_t>(w) * h * 4;
	HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelBytes);
	if (!hDib) { return false; }

	auto* p = static_cast<std::uint8_t*>(GlobalLock(hDib));
	if (!p) { GlobalFree(hDib); return false; }

	BITMAPINFOHEADER hdr{};
	hdr.biSize        = static_cast<DWORD>(headerSize);
	hdr.biWidth       = w;
	hdr.biHeight      = -h;  // 負 = top-down (本 buffer の row 順と一致)
	hdr.biPlanes      = 1;
	hdr.biBitCount    = 32;
	hdr.biCompression = BI_RGB;
	hdr.biSizeImage   = static_cast<DWORD>(pixelBytes);
	std::memcpy(p, &hdr, headerSize);

	// RGBA → BGRA (DIB は B,G,R,A 順)
	auto* dst = p + headerSize;
	const std::size_t pixels = static_cast<std::size_t>(w) * h;
	for (std::size_t i = 0; i < pixels; ++i)
	{
		dst[i * 4 + 0] = rgba[i * 4 + 2];
		dst[i * 4 + 1] = rgba[i * 4 + 1];
		dst[i * 4 + 2] = rgba[i * 4 + 0];
		dst[i * 4 + 3] = rgba[i * 4 + 3];
	}
	GlobalUnlock(hDib);

	if (!OpenClipboard(nullptr)) { GlobalFree(hDib); return false; }
	EmptyClipboard();
	HANDLE set = SetClipboardData(CF_DIB, hDib);
	CloseClipboard();
	if (!set) { GlobalFree(hDib); return false; }
	// 成功時は clipboard が hDib の所有権を持つ。free しない。
	return true;
}
#endif

#ifdef _WIN32
/// 1 VK ごとに down 状態を保持し、押された瞬間を検出して返す。GetAsyncKeyState を毎フレーム
/// 1 回呼ぶ前提。
inline bool justPressed(int vk)
{
	static bool wasDown[256] = {};
	if (vk < 0 || vk >= 256) { return false; }
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool jp = down && !wasDown[vk];
	wasDown[vk] = down;
	return jp;
}

/// F12 で window のバックバッファを PNG に保存 + clipboard にコピー。
inline void doScreenshot(mitiru::Engine& engine)
{
	const int w = engine.captureWidth();
	const int h = engine.captureHeight();
	if (w <= 0 || h <= 0) { return; }

	const auto rgba = engine.capture();
	if (rgba.empty()) { return; }

	const auto path = mitiru::render::saveTimestampedFrameToPng(
		rgba.data(), w, h, "screenshots", "frame");
	const bool clipOk = copyRgbaToClipboard(rgba.data(), w, h);

	if (!path.empty() && clipOk)
	{
		mitiru::console::noticef("画面を %s に保存し、クリップボードにも写しました。", path.c_str());
	}
	else if (!path.empty())
	{
		mitiru::console::noticef("画面を %s に保存しました。クリップボードへの写しは失敗しました。", path.c_str());
	}
	else if (clipOk)
	{
		mitiru::console::notice("画面をクリップボードに写しました。screenshots フォルダへの保存は失敗しました。");
	}
	else
	{
		mitiru::console::notice("画面の保存に失敗しました。screenshots フォルダに書き込めるかを確かめてください。");
	}
}
#endif

/// onFrameStart から毎フレーム呼ぶ host の hotkey 処理。Windows のみ。
inline void pollHostHotkeys(mitiru::Engine& engine)
{
#ifdef _WIN32
	// host の hotkey は GetAsyncKeyState (グローバル) で読むため、自プロセスのウィンドウが
	// 前面にあるときだけ処理する。そうしないと、別アプリでの作業中に F7-F12 を処理してしまう
	// (ゲーム入力自体は WM_KEYDOWN によりフォーカス中に限定されているが、ここだけグローバルだった)。
	HWND fg = GetForegroundWindow();
	DWORD fgPid = 0;
	GetWindowThreadProcessId(fg, &fgPid);
	if (fgPid != GetCurrentProcessId())
	{
		return;
	}
	if (justPressed(VK_F12))
	{
		doScreenshot(engine);
	}
	// カットシーン中 (ゲームの hud.cinematic、ABI v50) は止める・コマ送り・速さの切り替えを効かせない
	const bool f7 = justPressed(VK_F7);
	const bool f8 = justPressed(VK_F8);
	const bool f9 = justPressed(VK_F9);
	const bool cinematic = engine.moduleCinematicActive();
	if (cinematic && (f7 || f8 || f9))
	{
		mitiru::console::notice("カットシーンの間は F7 / F8 / F9 が効きません。");
	}
	if (f8 && !cinematic)
	{
		engine.setPauseKind(mitiru::EngineConfig::kPauseKindDebug);
		engine.togglePaused();
		mitiru::console::verbose(engine.isPaused() ? "一時停止しました。" : "再開しました。");
	}
	if (f7 && !cinematic)
	{
		if (engine.isPaused())
		{
			engine.stepOneFrame();
			mitiru::console::verbose("1 フレーム進めました。");
		}
	}
	if (f9 && !cinematic)
	{
		// 1x → 0.5x → 0.25x → 2x → 4x → 1x の順で切り替える
		static constexpr float kScales[] = { 1.0f, 0.5f, 0.25f, 2.0f, 4.0f };
		static constexpr int N = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
		static int idx = 0;
		idx = (idx + 1) % N;
		engine.setTimeScale(kScales[idx]);
		mitiru::console::noticef("速さを %.2f 倍にしました。", kScales[idx]);
	}
	if (justPressed(VK_F10))
	{
		engine.toggleLofi();
		mitiru::console::verbose(engine.isLofiEnabled() ? "lo-fi の描画にしました。" : "lo-fi の描画をやめました。");
	}
	if (justPressed(VK_F11))
	{
		// P1: 「昨日のバグ」リングを今すぐ保存する。Engine 側がワンショットで消費する。
		engine.mutableConfig().bugRingSaveRequested = true;
	}
#else
	(void)engine;  // host hotkeys は今のところ Windows 専用
#endif
}

inline std::string genericUtf8(const std::filesystem::path& p)
{
	const auto u8 = p.generic_u8string();
	return std::string(u8.begin(), u8.end());
}

/// --watch-assets: 変更されたファイルごとに asset.reloaded を追加する。モデルは描画側の読み込み済みも捨てる
/// (asset/AssetReload.hpp)。パスは UTF-8 で渡す。
inline void notifyAssetsReloaded(mitiru::asset::FileWatcher& watcher, const std::filesystem::path& dir,
                                 mitiru::Engine& engine)
{
	for (const auto& changed : watcher.poll())
	{
		const auto kind = mitiru::asset::classifyAssetChange(changed);
		if (kind == mitiru::asset::AssetChange::Derived) { continue; }
		std::error_code ec;
		const std::string key = genericUtf8(changed);
		const std::string rel = genericUtf8(std::filesystem::relative(changed, dir, ec));
		if (kind == mitiru::asset::AssetChange::Model && engine.reloadModelAsset(key) > 0)
		{
			mitiru::console::verbosef("モデル %s を読み直しました。", key.c_str());
		}
		const std::string payload = nlohmann::json{
			{"path", ec ? key : rel},
			{"hash", mitiru::module::spawnSourceFileHash(changed.filename().string())}}.dump();
		if (!engine.pushModuleActionEvent("asset.reloaded", payload))
		{
			mitiru::console::noticef("%s の変更をゲームに知らせられませんでした。同じフレームに届いた通知が多すぎます。", key.c_str());
		}
		else
		{
			mitiru::console::verbosef("%s の変更をゲームに知らせました。", key.c_str());
		}
	}
}

/// --watch: DLL が書き換えられたら、GameMemory を保持したまま差し替える (L3 ホットリロード)。
/// リンカがまだ使用中で拒否された (busy) 版は、FileWatcher が再度は通知しないため、次のフレームで再試行する。
struct DllReloadState
{
	std::filesystem::path busyRetry;   ///< 空 = 試し直す版なし
};

inline void reloadDll(const std::filesystem::path& dll, mitiru::Engine& engine, DllReloadState& st)
{
	if (engine.reloadModule(dll))
	{
		mitiru::console::verbose("DLL を読み込み直しました。");
		st.busyRetry.clear();
		return;
	}
	if (engine.moduleLoadBusy())
	{
		if (st.busyRetry.empty())
		{
			mitiru::console::verbose("DLL がまだ書き込み中なので、次のフレームで読み込み直します。");
		}
		st.busyRetry = dll;
		return;
	}
	st.busyRetry.clear();
	const std::string why = engine.moduleLoadError();
	mitiru::console::noticef("新しい DLL を読み込めなかったので、前のコードのまま続けます。ゲームの状態はそのままです。%s%s%s"
		"ビルドエラーを直して保存すると、もう一度読み込みます。",
		why.empty() ? "" : "(", why.c_str(), why.empty() ? "" : ") ");
}

inline void reloadChangedDll(mitiru::asset::FileWatcher& watcher, mitiru::Engine& engine, DllReloadState& st)
{
	bool changed = false;
	for (const auto& dll : watcher.poll())
	{
		mitiru::console::verbose("DLL が変わったので読み込み直します。");
		reloadDll(dll, engine, st);
		changed = true;
	}
	if (!changed && !st.busyRetry.empty())
	{
		reloadDll(st.busyRetry, engine, st);
	}
}

/// 巻き戻し scrub の適用を Engine::IFrameListener 経由に移した実装 (1-6)。
/// 別窓 (inspector) が書く scrub command を毎フレーム読み、engine の scrub-hold へ適用する。
/// main.cpp の onFrameStart から scrub 専用ロジックを移し、登録だけにするのが目的。
/// command を書くのはツール窓だけなので、ツール窓が見ていない間はファイルを見に行かない。
struct ScrubApplyListener final : mitiru::IFrameListener
{
	mitiru::observe::ScrubControlReader reader;  // 自プロセス pid 宛 (inspector が host pid に書く)

	void onBeforeUpdate(mitiru::Engine& engine) override
	{
		if (!engine.toolsWatching())
		{
			// 止めた窓が閉じたり落ちたりしたら、▶ を押せる者がいないので再生に戻す。
			if (engine.scrubHoldActive()) { engine.clearScrubHold(); }
			return;
		}
		const auto cmd = reader.pollCommand();
		if (!cmd) { return; }
		if (cmd->resume) { engine.clearScrubHold(); }
		else { engine.setScrubHold(cmd->offsetFromNewest); }
	}
};

// ── 入力スクリプト (#43-1, in-process 注入) ────────────────────────────────
// OS 入力 (SendInput) を経由せず InputSnapshot を直接書き換えるので、他アプリにキーが
// 漏れない・headless でも効く・決定的。実キーボードはスクリプト実行中は無視される。

/// PlayerError → 表示名 (--replay-test の FAIL 理由の表示用)。
inline const char* playerErrorName(mitiru::replay::PlayerError e)
{
	using PE = mitiru::replay::PlayerError;
	switch (e)
	{
	case PE::None:              return "問題なし";
	case PE::FileNotOpen:       return "ファイルを開けません";
	case PE::HeaderTooShort:    return "録画ファイルとして短すぎます";
	case PE::MagicMismatch:     return ".mtrr の録画ファイルではありません";
	case PE::VersionMismatch:   return "対応していない版の録画です";
	case PE::FrameSizeMismatch: return "入力の形が今のエンジンと違います";
	case PE::FrameTruncated:    return "途中で切れています";
	case PE::ChecksumMismatch:  return "中身が壊れています";
	}
	return "原因が分かりません";
}

/// gfx::Backend → --record の envTag に含める短い識別子。
inline const char* gfxBackendTag(mitiru::gfx::Backend b)
{
	using B = mitiru::gfx::Backend;
	switch (b)
	{
	case B::Auto:   return "auto";
	case B::Dx11:   return "dx11";
	case B::Dx12:   return "dx12";
	case B::Vulkan: return "vulkan";
	case B::OpenGL: return "opengl";
	case B::WebGL:  return "webgl";
	case B::WebGPU: return "webgpu";
	case B::Null:   return "null";
	}
	return "unknown";
}

/// --record の envTag を組み立てる ("backend|arch|tracy 有無")。GPU adapter 名は IDevice から
/// 取得する経路がなく、追加するにはすべての backend の interface を変更する必要があるため見送り、
/// 確実に取得できる backend の種別と CPU arch だけを含める。
/// 7-1: Tracy 計装ビルドかどうかも含める。この録画を後から Tracy キャプチャと照合する
/// (`docs/PROFILING_GUIDE.md`) とき、計装済みビルドで録画したかを envTag だけで判別できる。
inline std::string buildRecordEnvTag(mitiru::gfx::Backend backend)
{
	std::string tag = std::string(gfxBackendTag(backend)) + "|x64";
	tag += mitiru::debug::TracyHelper::isAvailable() ? "|tracy" : "|no-tracy";
	return tag;
}

}  // namespace

namespace
{
mitiru::host::HostGuiLog g_guiLog;
bool                     g_headlessRun = false;
}  // namespace

static int hostMain(int argc, char* argv[])
{
#ifdef _WIN32
	// ログと警告は UTF-8 で書いている (ソースが /utf-8)。コンソールの既定は CP932 なので、
	// そのままでは日本語がすべて文字化けする。出力コードページだけを UTF-8 へ切り替え、終了時に
	// 元へ戻す (同じ窓で続けて動く他のツールの表示に影響させないため)。
	static const UINT s_prevConsoleCP = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);
	std::atexit([] { SetConsoleOutputCP(s_prevConsoleCP); });
#endif
#ifdef _WIN32
	// CRT が渡す argv は ANSI (日本語環境では CP932) へ変換済みで、UTF-8 前提の
	// エンジンへ流すと日本語の引数が化ける (--title など)。UTF-16 の原本から取り直す。
	//
	// 実体は utf8Args が持つ。main の最後まで生きるので、useArgv が指し続けても安全。
	std::vector<std::string> utf8Args = mitiru::platform::commandLineUtf8Args();
	std::vector<char*>       utf8ArgPtr;
	if (!utf8Args.empty())
	{
		for (auto& s : utf8Args) { utf8ArgPtr.push_back(s.data()); }
		argc = static_cast<int>(utf8ArgPtr.size());
		argv = utf8ArgPtr.data();
	}
#endif
	anchorCwdToExeDir(argc > 0 ? argv[0] : nullptr);

	// 引数なしで起動した場合 (ダブルクリック / Steam) は、sidecar <exe>.mtargs から
	// argv を補う。これにより mitiru_host を <game>.exe にリネームして配布できる。
	std::vector<std::string> synthArgs;
	std::vector<char*>       synthPtr;
	int                      useArgc = argc;
	char**                   useArgv = argv;
	if (argc < 2)
	{
		auto side = readSidecarArgs(argc > 0 ? argv[0] : nullptr);
		if (!side.empty())
		{
			synthArgs.push_back(argc > 0 ? std::string(argv[0]) : std::string("mitiru_host"));
			for (auto& s : side) { synthArgs.push_back(s); }
			for (auto& s : synthArgs) { synthPtr.push_back(s.data()); }
			useArgc = static_cast<int>(synthPtr.size());
			useArgv = synthPtr.data();
		}
	}

	CliArgs args = parseArgs(useArgc, useArgv);
	if (!args.parseError.empty())
	{
		mitiru::console::notice(args.parseError);
		return 2;
	}
	if (args.verbose)
	{
		// ゲームの DLL は自分の static を持つので、DLL の側の詳細も出るよう環境変数でも伝える
		mitiru::console::setVerbose(true);
#ifdef _WIN32
		_putenv_s("MITIRU_LOG", "verbose");
#else
		setenv("MITIRU_LOG", "verbose", 1);
#endif
	}
	if (args.helpRequested) { printUsage(args.helpAllRequested); return args.dllPath.empty() ? 1 : 0; }
	if (!args.unknownOption.empty())
	{
		mitiru::console::noticef("%s という引数はありません。使える引数は --help で見られます。",
		                         args.unknownOption.c_str());
		return 1;
	}
	g_headlessRun = args.headless || args.headlessGpu3D;
	g_guiLog.attachIfOrphaned(!args.gameName.empty() ? args.gameName
		: (args.dllPath.empty() ? std::string("mitiru_host") : args.dllPath.stem().string()));
	mitiru::debug::setHostCrashLogFile(g_guiLog.path());
	// 配布物は --game-name で起動するので、ここで決めれば起動の途中で落ちても次の起動が拾う置き場に残る
	if (!args.gameName.empty()) { mitiru::debug::setCrashGameName(args.gameName); }
	// --perf の env 版。host を自分で構成しないツール (KaeruCrepe の shot.py など) から
	// フレームレートを確認するために必要。子プロセスへ継承されることも意図している。
	if (const char* perfEnv = std::getenv("MITIRU_PERF");
	    perfEnv != nullptr && perfEnv[0] != '\0' && std::string_view{perfEnv} != "0")
	{
		args.perf = true;
	}
	// ADR 0035 O5: Engine_Http.hpp::runReplayGate が同名の env を読む (未指定なら
	// tests/replay_golden が既定のまま)。
	if (!args.replayGateDir.empty())
	{
#ifdef _WIN32
		_putenv_s("MITIRU_REPLAY_GATE_DIR", args.replayGateDir.c_str());
#else
		setenv("MITIRU_REPLAY_GATE_DIR", args.replayGateDir.c_str(), 1);
#endif
	}

#ifdef _WIN32
	// --appid: window を生成する前 (最初期) に設定しないと、taskbar の分離が機能しない。
	if (!args.appId.empty()) { applyAppUserModelId(args.appId); }
#endif

	// --record と --replay-test は排他 (replay 側が record callback を上書きし、
	// 録画が何も知らせずに無効になるため。併用する意図は成立しない)。
	if (!args.recordPath.empty() && !args.replayPath.empty())
	{
		mitiru::console::notice("--record と --replay-test は一緒に使えません。再生している間は録画しないためです。");
		return 1;
	}

	// --input-script (#43-1): OS を通さず InputSnapshot へキーを注入する。replay (--replay) の使用時は
	// そちらの moduleInputOverride が優先されるため読まない。窓やツールを開く前に読み、読めない行が
	// あればそこで止める (誤った台本のまま最後まで流すと、何も押さなかった結果をゲームの不具合と取り違える)。
	mitiru::input::InputScriptPlayer scriptPlayer;
	const bool scriptLoaded = args.replayPath.empty() && !args.inputScript.empty();
	if (scriptLoaded)
	{
		std::string scriptError;
		if (!mitiru::input::loadInputScript(args.inputScript, scriptPlayer, scriptError))
		{
			mitiru::console::noticef("--input-script の台本を読めません。%s", scriptError.c_str());
			return 2;
		}
		mitiru::console::verbosef("台本 %s から %zu 件の入力を読みました。", args.inputScript.c_str(),
		                          scriptPlayer.events.size() + scriptPlayer.pad.events.size());
	}

	// --state-diff A B: DLL は不要。2 つの .mtrr の GameMemory blob を byte 単位で比較し、
	// 「同じ入力・異なるコードでどの frame から分岐したか」を 1 行の JSON で報告する。
	if (!args.stateDiffA.empty() && !args.stateDiffB.empty())
	{
		const auto d = mitiru::replay::Player::diffState(args.stateDiffA, args.stateDiffB);
		if (d.totalFrames == 0)
		{
			mitiru::console::notice("--state-diff の 2 つの録画を比べられません。どちらかが .mtrr として読めないか、中身が空です。");
			return 2;
		}
		if (d.diverged)
		{
			std::fprintf(stdout, "{\"diverged\":true,\"firstDivergentFrame\":%u,\"totalFrames\":%u}\n",
			             d.firstDivergentFrame, d.totalFrames);
			return 1;
		}
		std::fprintf(stdout, "{\"diverged\":false,\"totalFrames\":%u}\n", d.totalFrames);
		return 0;
	}

	// --check-i18n D: DLL は不要。書体の並びは、配布物の中でもこの exe から探した同梱の書体から始める
	if (!args.checkI18nDir.empty())
	{
		const auto report = mitiru::text::checkTranslations(std::filesystem::u8path(args.checkI18nDir), mitiru::text::engineFontDir());
		std::fputs(mitiru::text::translationReportTsv(report).c_str(), stdout);
		return 0;
	}

	std::error_code ec;
	if (!std::filesystem::exists(args.dllPath, ec) || ec)
	{
		// 相対パスは cwd で見つからなければ exe の隣も探す (exe と別ディレクトリから
		// 手で `mitiru_host.exe ..\build\Game.dll` を叩くケースに対応、E9)。
		bool resolved = false;
#ifdef _WIN32
		if (args.dllPath.is_relative())
		{
			wchar_t exeBuf[MAX_PATH];
			DWORD   len = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
			if (len > 0 && len < MAX_PATH)
			{
				const auto exeDir    = std::filesystem::path(exeBuf).parent_path();
				const auto besideExe = exeDir / args.dllPath;
				if (std::filesystem::exists(besideExe, ec) && !ec)
				{
					args.dllPath = besideExe;
					resolved     = true;
				}
			}
		}
#endif
		if (!resolved)
		{
			mitiru::console::noticef("ゲームの DLL %s が見つかりません。今いるフォルダか mitiru_host のフォルダからのパスで指定してください。",
			                         args.dllPath.string().c_str());
			return 2;
		}
	}

	// --bake <in.json> <out.baked>: DLL の reflect/spawner スキーマを使い、配置 JSON を POD へ
	// 変換するだけの経路 (★4-1)。window/GPU は一切初期化しない。game 側が `MITIRU_BAKE_ASSETS()`
	// を export していない (未対応) 場合は理由を出して終了する (host 側で ad-hoc に変換すると DLL の
	// 実際の sizeof(T) とずれるおそれがあるため、必ず DLL 自身に処理させる)。
	if (!args.bakeInJson.empty() && !args.bakeOutPath.empty())
	{
		mitiru::module::ModuleHost bakeHost;
		if (!bakeHost.load(args.dllPath))
		{
			mitiru::console::noticef("--bake でゲームの DLL を読めません (%s)。", bakeHost.lastError().c_str());
			return 2;
		}
		const auto bakeFn = bakeHost.bakeAssetsFn();
		if (bakeFn == nullptr)
		{
			mitiru::console::notice("この DLL は焼き込みに対応していません。ゲームのコードに MITIRU_BAKE_ASSETS() を書いてください。");
			return 2;
		}
		const bool ok = bakeFn(args.bakeInJson.c_str(), args.bakeOutPath.c_str());
		if (!ok)
		{
			mitiru::console::noticef("%s を %s に焼き込むのに失敗しました。", args.bakeInJson.c_str(), args.bakeOutPath.c_str());
			return 2;
		}
		mitiru::console::verbosef("%s を %s に焼き込みました。", args.bakeInJson.c_str(), args.bakeOutPath.c_str());
		return 0;
	}

	// dev の vfs 相対パス解決の基準 = game DLL の隣 (cwd は exe の位置に固定済みのため、
	// deploy dir と exe dir が異なる配置でも "assets/..." が game の assets を参照できるようにする)。
	// pack と同様に env で共有する。host と game DLL の両方に適用される。
	{
		const auto absDll = std::filesystem::absolute(args.dllPath, ec);
		const std::string rootEnv =
			(ec ? args.dllPath : absDll).parent_path().string();
#ifdef _WIN32
		_putenv_s("MITIRU_ASSET_ROOT", rootEnv.c_str());
#else
		setenv("MITIRU_ASSET_ROOT", rootEnv.c_str(), 1);
#endif
	}

	// 秘匿配布: まず自分の exe に連結されたパックを探し、なければ DLL の隣にある
	// assets.mtpak を使う。exe に連結されていれば、配布物からパックのファイルがなくなり、exe 1 つに
	// 資産が入る。
	// 実際の open/mount は Engine 側 (`EngineConfig::packPath` → `mountModulePackIfConfigured`、
	// `Engine_Module_Loader.hpp`) に統一した。host 側で AssetPack::open して
	// vfs::mountGlobal する経路は二重 mount になるため廃止し、host は「どの pack を使うか」
	// の発見 (self-exe 連結 / assets.mtpak 隣接 / --pack 明示) だけを担う。env は
	// `MITIRU_PACK` に統一 (旧 `MITIRU_ASSET_PACK` は Engine 側が後方互換のため読むだけで、
	// host からはもう書かない)。
	std::string resolvedPackPath;  // 見つかった pack の絶対パス。下で cfg.packPath へ渡す
	{
		auto packPath =
			std::filesystem::path(args.dllPath).parent_path() / "assets.mtpak";
		const auto selfExe = std::filesystem::absolute(
			std::filesystem::path(argv[0]), ec);
		if (!ec && mitiru::vfs::AssetPack::open(selfExe).has_value())
		{
			packPath = selfExe;
		}
		if (!args.packOverride.empty())
		{
			// --pack: 自動探索 (自己連結 / assets.mtpak 隣接) より優先する明示指定 (P12)。
			packPath = std::filesystem::path(args.packOverride);
		}
		if (std::filesystem::exists(packPath, ec) && !ec
			&& mitiru::vfs::AssetPack::open(packPath).has_value())
		{
			// cfg.packPath は下で mitiru::EngineConfig を構築した後に設定する (このブロックの時点では
			// cfg は未構築)。ここでは発見結果だけを変数に残す。
			const auto absPack = std::filesystem::absolute(packPath, ec);
			const std::string packEnv = (ec ? packPath : absPack).string();
			resolvedPackPath = packEnv;
#ifdef _WIN32
			_putenv_s("MITIRU_PACK", packEnv.c_str());
#else
			setenv("MITIRU_PACK", packEnv.c_str(), 1);
#endif
			mitiru::console::verbosef("資産パック %s を使います。", packPath.string().c_str());
		}
	}

	mitiru::EngineConfig cfg;
	// P12 1 ファイル配布: 上の発見結果を Engine 側 (mountModulePackIfConfigured) へそのまま渡す。
	// host はもう自分で AssetPack::open/mountGlobal しない (二重 mount の回避、env は MITIRU_PACK に統一)。
	cfg.packPath        = resolvedPackPath;
	cfg.collisionPath   = args.collisionPath;
	cfg.gfxBackend      = args.backend;  // --backend (既定 Auto)。--headless-3d の windowless 経路にも効く
	// --title の未指定時は DLL ファイル名の stem (例 scene3d)。何のゲームか一目で分かる外観
	cfg.title           = args.title.empty() ? args.dllPath.stem().string() : args.title;
	cfg.windowWidth     = args.widthOverride  > 0 ? args.widthOverride  : 1280;
	cfg.windowHeight    = args.heightOverride > 0 ? args.heightOverride :  720;
	cfg.windowX         = args.winPosX;   // --window-pos (既定 INT_MIN = OS 任せ)
	cfg.windowY         = args.winPosY;
	std::unique_ptr<mitiru::host::HostWindowShot> windowShot;
	if (!args.windowShot.empty())
	{
		windowShot = std::make_unique<mitiru::host::HostWindowShot>(args.windowShot, args.maxFrames);
		if (args.winPosX == (-2147483647 - 1))
		{
			const auto at = mitiru::host::HostWindowShot::placement(cfg.windowWidth, cfg.windowHeight);
			if (at) { cfg.windowX = at->x; cfg.windowY = at->y; }
			if (at) { mitiru::console::verbosef("撮る窓を仮想ディスプレイの (%d, %d) に置きます。", cfg.windowX, cfg.windowY); }
			else { mitiru::console::verbose("置ける仮想ディスプレイが無いので、撮る窓を画面の外に置きます。"); }
		}
	}
#ifdef _WIN32
	// 台本で動かす起動は人が見ていない。窓が前面に出るとユーザーの作業を妨げるだけなので、
	// --no-activate の指定を待たずに有効にする。env は spawn するツール窓 (別プロセス) へ継承される。
	if (args.noActivate || !args.inputScript.empty() || !args.captureDir.empty()
	    || args.headlessGpu3D)
	{
		mitiru::Win32Window::setProcessNoActivate(true);
		_putenv_s("MITIRU_NO_ACTIVATE", "1");
	}
#ifdef _DEBUG
	// 人が遊んでいる最中の device-lost は再現しないことが多く、手元に残るのはこの起動の出力だけ (#79)。
	// 台本・リプレイ・画面外での起動は再現用なので、必要なら呼び出し側が自分で有効にする。
	const bool interactiveLaunch = !args.noActivate && args.inputScript.empty() && args.captureDir.empty()
		&& !args.headless && !args.headlessGpu3D && args.replayPath.empty();
	if (interactiveLaunch && std::getenv("MITIRU_D3D12_DRED") == nullptr)
	{
		_putenv_s("MITIRU_D3D12_DRED", "1");
	}
#endif
#endif
	// resize の安全対策: 要求サイズの半分を floor にする (極端に小さくなることだけを防ぎ、指定サイズは
	// 超えない)。game 窓 / launcher / 760x80 の companion bar を同じ host が起動するため、
	// 固定値ではなく要求サイズを基準にし、「floor > 指定」で窓を開けない事態を避ける。
	{
		const int floorW = cfg.windowWidth  / 2;
		const int floorH = cfg.windowHeight / 2;
		cfg.minWindowWidth  = floorW > 200 ? floorW : 200;
		cfg.minWindowHeight = floorH > 48  ? floorH : 48;
	}
	cfg.vsync           = true;
	if (args.noPauseUnfocused) { cfg.vsync = false; }  // 背面でもフルレート (present の vsync 待ちを回避)
	if (args.noVsync)          { cfg.vsync = false; }  // --no-vsync: 素のフレームコスト計測 (#53)
	cfg.timeScale       = args.speed;    // --speed: 固定 dt × N 早回し (#43)
	cfg.errorBannerFile = args.errorFile; // --error-file: mitiru watch のビルドエラー帯 (空=OFF)
	cfg.bugRingSaveRequested = args.bugRingSave; // --bug-ring-save: 起動直後に即保存 (通常は F11)
	if (args.fixedSize) { cfg.windowResizable = false; }   // --fixed-size: リサイズ禁止 (#44)
	if (args.rewindFrames > 0) { cfg.timeTravelBufferFrames = static_cast<std::uint32_t>(args.rewindFrames); }   // --rewind-frames: 巻き戻しバッファ長
	// --rewind-mb: 巻き戻しリングの予算 (MB)。明示指定 (>=0、0=無制限=非圧縮) の場合だけ
	// explicit フラグを立てる。未指定 (-1) の場合は engine 側 (recordModuleMemoryFrame) が
	// game の MITIRU_REWIND_BUDGET 宣言 > 既定 512MB の順で決める。
	if (args.rewindMb >= 0)
	{
		cfg.timeTravelBudgetBytes = static_cast<std::size_t>(args.rewindMb) * 1024ull * 1024ull;
		cfg.timeTravelBudgetBytesExplicit = true;
	}
	if (args.rewindRawMb >= 0)
	{
		cfg.timeTravelRawLimitBytes = static_cast<std::size_t>(args.rewindRawMb) * 1024ull * 1024ull;
	}
	// --oracle-determinism [N]: P14 決定論オラクルの opt-in (既定 OFF、resim ring + memcmp の
	// コストが GameMemory のサイズに比例するため、明示指定時だけ有効にする)。
	if (args.oracleDeterminism)
	{
		cfg.oracleDeterminism = true;
		cfg.oracleDeterminismEveryFrames = args.oracleDeterminismEveryFrames;
	}
	// --synctest [K]: oracle-determinism の単純な別名 (K=1 が既定で毎フレーム検査)。両方の指定時は、
	// より短い間隔 (小さい方) を採用する。
	if (args.synctest)
	{
		cfg.oracleDeterminism = true;
		cfg.oracleDeterminismEveryFrames = cfg.oracleDeterminismEveryFrames == 0
			? args.synctestFrames
			: std::min(cfg.oracleDeterminismEveryFrames, args.synctestFrames);
	}
	cfg.oracleMachineLog = args.oracleLog;  // --oracle-log: mitiru-cli の ScanOracleLines 用
	cfg.saveRoundtripTest = args.saveRoundtripTest;  // --save-roundtrip-test: save→load→save の bit 一致検査
	if (args.headless)                   // --headless: 窓なし自動回し。vsync を切って最速で (#43)
	{
		cfg.headless  = true;
		cfg.vsync     = false;
		cfg.deterministic = true;  // 固定 clock で run 間を決定的に (1 host frame = 1 fixed-step)
	}
	if (args.headlessGpu3D)
	{
		// G2: Engine_Init_Lifecycle.hpp の GPU device 生成が参照する opt-in 環境変数。
		// EngineConfig にフィールドを追加せず (ABI 非対象ファイルのため)、環境変数で渡す。
#ifdef _WIN32
		_putenv_s("MITIRU_HEADLESS_GPU3D", "1");
#else
		setenv("MITIRU_HEADLESS_GPU3D", "1", 1);
#endif
	}
	// EngineHttpServer と AI Lens: --http-port > 0 / --console / 環境変数
	// MITIRU_AI が設定されていれば HTTP listen を開始する (127.0.0.1 限定)。MITIRU_AI は、AI が zero-config で
	// /api/ai/state・/diff・/branch を呼べるようにする opt-in (port は MITIRU_AI_PORT、既定 8090)。
	const char* aiEnv = std::getenv("MITIRU_AI");
	const bool  aiOptIn = (aiEnv != nullptr && aiEnv[0] != '\0' && std::string{aiEnv} != "0");
	if (args.httpPort > 0 || args.console || aiOptIn)
	{
		cfg.enableHttpApi = true;
		cfg.httpApiPort   = (args.httpPort > 0) ? args.httpPort : 8090;
		if (aiOptIn && args.httpPort <= 0)
		{
			if (const char* aiPort = std::getenv("MITIRU_AI_PORT"); aiPort != nullptr && aiPort[0] != '\0')
			{
				try { cfg.httpApiPort = std::stoi(aiPort); } catch (...) { /* 既定 8090 のまま */ }
			}
		}
	}
	if (args.configOrigins)
	{
		// engine/window を作らず、ここまでに確定した値だけを使ってすぐに終了する (§3-3)。
		printConfigOrigins(args, cfg, resolvedPackPath, aiOptIn);
		return 0;
	}
	// --console: HTTP server は engine.run() 内で起動するため、初回フレームで listen の成功を
	// 確認してからブラウザを開く (init 失敗時は開かない、H-10 と整合)。onFrameStart で処理する。
	bool consolePending  = args.console;
	const int consolePort = cfg.httpApiPort;
	// --icon: window は engine.runModule 内で生成されるため、初回フレームで適用する (onFrameStart で処理)。
	bool iconPending = !args.iconPath.empty();
	// フォント: 既定では同梱の日本語書体を読み、native draw (drawTextInRect など) でも
	// 日本語を表示できる。字形は使われた分だけ生成するため、起動は軽い。8x8 ビットマップ ASCII
	// で起動したい場合は --font none。
	if (args.fontMode == "none")
	{
		cfg.skipDefaultFont = true;
	}
	else
	{
		cfg.skipDefaultFont = false;
		// フォントフェイス: 既定の normal = M+ Rounded 1c (通常の丸ゴシック)、
		// retro = PixelMplus (ファミコン風ピクセル)。exe の隣にある同梱フォントを解決する。
		const std::string faceFile = (args.fontFace == "retro")
			? "assets/fonts/PixelMplus12-Regular.ttf"
			: "assets/fonts/MPLUSRounded1c-Regular.ttf";
		cfg.fontPath = mitiru::resource::AssetPath::resolve(faceFile);
	}
	// Mitiru Saturn の標準背景。シルバーグレー (#c8c8c8)。エンジン同梱のすべての surface
	// (hello_game / launcher / companion) はこのシルバーの背景に HUD を描き、Saturn の
	// 統一感を出す。別の背景が必要な game はインスタンス単位で上書きできる。
	cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};

	// ローファイ・ポスト FX: 低解像度での描画 + パレット量子化 + Bayer ディザ (DX12)
	if (args.loFi)
	{
		cfg.loFi.enabled       = true;
		cfg.loFi.internalWidth = args.loFiW;
		cfg.loFi.internalHeight= args.loFiH;
		cfg.loFi.colorBitsR    = args.loFiBitsR;
		cfg.loFi.colorBitsG    = args.loFiBitsG;
		cfg.loFi.colorBitsB    = args.loFiBitsB;
		cfg.loFi.ditherStrength= args.loFiDither;
		cfg.loFi.softUpscale   = !args.loFiHard;
		cfg.loFi.viFilter      = args.loFiVi;
		cfg.loFi.gamma         = args.loFiGamma;
	}
	cfg.antiAliasing3D = args.antiAliasing3D;
	cfg.motionBlur3D   = args.motionBlur3D;
	cfg.screenSpaceReflections3D = args.ssr3D;
	cfg.lightingBake3D = args.lightingBake;
	cfg.gpuPassTiming  = !args.perfLog.empty();
	// UI の層: DLL の隣に assets/ui/main.rml があれば RmlUi で描画する (ADR 0051)。
	cfg.uiDocument = defaultUiDocumentFor(args.dllPath);

	// 出荷に要る host の仕事 (セーブの置き場、利用者の設定、キー割り当て、Steamworks)。何も指定しなければ何もしない。
	mitiru::host::HostShip ship;
	const bool interactiveRun = !args.headless && !args.headlessGpu3D && args.inputScript.empty()
		&& args.replayPath.empty() && args.captureDir.empty();
	if (!ship.configure(args.dllPath, { args.gameName, args.saveDir, args.settingsPath, interactiveRun }, cfg)) { return 0; }
	mitiru::host::HostOnline online;
	const bool onlineRequests = args.recordPath.empty() && args.replayPath.empty();
	if (std::string onlineError; !online.configure(args.online, interactiveRun, onlineRequests, cfg, onlineError))
	{
		mitiru::console::notice(onlineError);
		return 2;
	}

	// 読み込みの速さを絵に出してよいのは、人が窓を見ている普通の実行だけ (docs/STREAMING.md)
	const bool unattended = args.headless || !args.captureDir.empty() || !args.inputScript.empty() || !args.replayPath.empty();
	cfg.asyncLoads = args.loads.empty() ? !unattended : (args.loads == "async");
	mitiru::host::HostStreaming hostStreaming;
	{
		std::string streamError;
		bool streamOk = args.preloadList.empty() || hostStreaming.setPreload(args.preloadList, streamError);
		streamOk = streamOk && (args.bakeList.empty() || hostStreaming.setBake(args.bakeList, streamError));
		for (const auto& spec : args.stages) { streamOk = streamOk && hostStreaming.addStage(spec, streamError); }
		if (!streamOk)
		{
			mitiru::console::notice(streamError);
			return 2;
		}
	}

	// MITIRU_AUTOTEST_FRAMES は autotest でスクリーンショットを撮るまでのフレーム数を変える。
	// Engine::run の applyAutoTestEnv() の既定は 120 (約 2s)。UI の遷移が終わった後を撮りたい時に延ばす。
	if (const char* envFrames = std::getenv("MITIRU_AUTOTEST_FRAMES");
	    envFrames && envFrames[0] != '\0')
	{
		try
		{
			const int n = std::stoi(envFrames);
			if (n > 0)
			{
				cfg.autoTestMode   = true;
				cfg.autoTestFrames = n;
				cfg.autoTestExitAfter = true;
			}
		}
		catch (...) {}
	}

	// onFrameStart: 2 つの常駐ジョブを 1 つのコールバックで処理する。
	//   (1) F12 → スクリーンショット (常時 on、Windows のみ)
	//   (2) --watch 時のみ: DLL の書き換えを検知して L3 ホットリロード
	// キャプチャした監視器はこのスタックフレームに置く。Engine::runModule が
	// ループ終了までブロックするため、lifetime に問題はない。
	// --capture-dir/--capture-every (#43): 既定値を補い、ディレクトリを作る。
	// 片方だけの指定でも有効にする（dir 省略→"captures"、every 省略→30）。
	std::string captureDir = args.captureDir;
	int captureEvery = args.captureEvery;
	if (!captureDir.empty() && captureEvery <= 0) { captureEvery = 30; }
	if (captureEvery > 0 && captureDir.empty()) { captureDir = "captures"; }
	const bool captureOn = (captureEvery > 0 && !captureDir.empty());
	// capture 中は実時間の dt を使わず、固定 dt にする (Engine::initialize が captureActive を
	// 参照して deterministic を強制する)。描画コスト (PNG 保存など) がゲーム内時間に影響し、
	// --input-script のフレーム番号と実時刻がずれることを防ぐ (G1)。
	cfg.captureActive = captureOn;
	// #53: headless では capture が読むフレームだけ SW ラスタライズする (観測フレーム gating)。
	// capture なしの自動実行では on-demand のみ (HTTP screenshot などには 1 フレーム遅れて追従)。
	// CPU ラスタライズはピクセル数に比例して重く、これを省くと --speed による高速実行が実時間でも速くなる。
	if (args.headless)
	{
		cfg.swRasterizeEvery = captureOn ? captureEvery : 0;
	}
	if (captureOn)
	{
		std::error_code cec;
		std::filesystem::create_directories(captureDir, cec);
		if (cec)
		{
			// 何も知らせずに起動すると、PNG が 0 枚のまま exit 0 になり得る (DoD の必須経路がなくなる)。
			mitiru::console::noticef("--capture-dir のフォルダ %s を作れません (%s)。", captureDir.c_str(), cec.message().c_str());
			return 2;
		}
		mitiru::console::verbosef("%d フレームごとに %s/ へ画面を書き出します。", captureEvery, captureDir.c_str());
	}
	int captureFrame = 0;   // 経過フレーム数 (onFrameStart クロージャが進める)
	int captureSeq = 0;     // 保存連番
	bool captureSaveFailed = false;  // PNG 保存失敗の初回報告済みフラグ (以降は黙る)
	int totalFrame = 0;     // 総フレーム数 (--max-frames 判定用)
	// PNG エンコード (zlib 圧縮) を専用スレッドへ移し、host frame を待たせない (G7)。
	// スコープを出るとき (関数 return / 例外) に、デストラクタが残りのキューを書き終えてから join する。
	std::unique_ptr<mitiru::render::AsyncPngWriter> pngWriter;
	if (captureOn) { pngWriter = std::make_unique<mitiru::render::AsyncPngWriter>(); }

	// --perf (#53): onFrameStart の間隔 = 1 host frame の実時間。600 フレームごとに統計を出す。
	struct PerfStats
	{
		std::vector<double> samples;                    // 当ウィンドウのフレーム時間 (ms)
		std::vector<double> all;                        // --frame-times: 全フレーム分
		bool keepAll = false;
		std::chrono::steady_clock::time_point last{};
		bool hasLast = false;

		void report()
		{
			if (samples.empty()) { return; }
			std::vector<double> s = samples;
			std::sort(s.begin(), s.end());
			double sum = 0.0;
			for (const double v : s) { sum += v; }
			const auto pct = [&s](double p) {
				return s[static_cast<std::size_t>(p * static_cast<double>(s.size() - 1))];
			};
			const double avg = sum / static_cast<double>(s.size());
			mitiru::console::noticef("perf: %zu frames  avg %.2f ms (%.1f fps)  p50 %.2f  p95 %.2f  max %.2f",
				s.size(), avg, 1000.0 / avg, pct(0.50), pct(0.95), s.back());
			samples.clear();
		}
	};
	PerfStats perfStats;
	perfStats.keepAll = !args.frameTimes.empty();
	if (perfStats.keepAll) { perfStats.all.reserve(args.maxFrames > 0 ? static_cast<std::size_t>(args.maxFrames) + 2 : 36000); }
	std::unique_ptr<mitiru::host::HostPerfLog> perfLog;
	if (!args.perfLog.empty())
	{
		perfLog = std::make_unique<mitiru::host::HostPerfLog>(args.maxFrames > 0 ? static_cast<std::size_t>(args.maxFrames) : 36000);
	}
	// FrameArena (2-1) の使用量を毎フレーム反映する。オーバーレイは既定で非表示
	// (F11 相当のトグルは未配線。engine.frameArena() の値を外部から見えるようにする処理のみ)。
	mitiru::debug::FrameBudget frameBudget;
	if (args.perf && cfg.vsync)
	{
		mitiru::console::notice("perf の時間には垂直同期の待ちが入ります。描画だけの時間を見るときは --no-vsync も付けてください。");
	}

	std::filesystem::path assetWatchDir;
	std::unique_ptr<mitiru::asset::FileWatcher> assetWatcher;
	if (!args.watchAssetsDir.empty())
	{
		std::error_code aec;
		assetWatchDir = std::filesystem::absolute(args.watchAssetsDir, aec);
		assetWatcher = std::make_unique<mitiru::asset::FileWatcher>();
		if (aec || !assetWatcher->watchDirectory(assetWatchDir, mitiru::asset::watchedAssetExtensions()))
		{
			mitiru::console::noticef("--watch-assets のフォルダ %s が見つかりません。", args.watchAssetsDir.c_str());
			return 2;
		}
		mitiru::console::verbosef("%s の資産の変更を見ます。", assetWatchDir.string().c_str());
	}

	std::unique_ptr<mitiru::asset::FileWatcher> dllWatcher;
	DllReloadState dllReload;
	if (args.watch)
	{
		// 絶対パスに解決し、cwd の変更後にリロードする場合でもファイルを見つけられるようにする。
		std::error_code rc;
		auto dllPath = std::filesystem::absolute(args.dllPath, rc);
		if (rc) { dllPath = args.dllPath; }
		dllWatcher = std::make_unique<mitiru::asset::FileWatcher>();
		if (!dllWatcher->watchFile(dllPath))
		{
			mitiru::console::noticef("--watch で %s のフォルダを見張れません。DLL のパスを確かめてください。",
			                         dllPath.string().c_str());
			return 2;
		}
		mitiru::console::verbosef("%s の変更を見ます。", dllPath.string().c_str());
	}

	// time-travel scrub: rewind のツール窓 (mitiru_tool --page rewind) が書く scrub command を
	// 毎フレーム読み、過去フレームの GameMemory へ巻き戻す (click-to-scrub)。
	// reader は host 側 = rewind は host の責務 (game DLL は pure を保つ)。適用処理は
	// ScrubApplyListener (IFrameListener) に移してあり、ここでは登録だけを行う (1-6)。
	ScrubApplyListener scrubListener;

	// ドッキング: ツール窓 (シークバーなど) が吸着・追従できるよう、自窓の画面矩形を broadcast する。
	mitiru::observe::DockWriter dockWriter{mitiru::observe::detail::scrubThisPid()};
	int dockLastX = INT_MIN, dockLastY = INT_MIN, dockLastW = 0, dockLastH = 0;

	// --pause-control (録画支援): ファイルが "1" の間だけ engine を pause する (dt=0, 描画継続)。
	// フォーカスは不要で、scrub-control と同じ考え方。自動録画で「編集中は静止」を作るために使う。
	const std::string pauseControlFile = args.pauseControl;
	int pauseControlTick = 0;
	char pauseControlLast = '\0';   // 前回読み値 (変化時のみ setPaused = F8/HTTP pause と共存)

	// --ghost (10-1):.mtrr を開く処理だけをここで行う (module load は engine の構築後、
	// 下の方で行う)。onFrameStart のクロージャがこの 3 つを参照で捕捉する。
	mitiru::replay::Player ghostPlayer;
	bool                   ghostActive = false;
	bool                   ghostEof    = false;
	if (!args.ghostPath.empty())
	{
		if (!ghostPlayer.open(args.ghostPath))
		{
			mitiru::console::noticef("--ghost の録画 %s を開けません (%s)。",
			                         args.ghostPath.c_str(), playerErrorName(ghostPlayer.lastError()));
			return 2;
		}
	}

	// 台本で動かす窓は画面外にあるか隠れており、DXGI は occluded な swap chain の Present を待たせない。
	// そのままでは描画可能な限り描画して GPU を占有し、別の動画を見ているユーザーの再生が途切れる。
	// リプレイが使う固定ステップと同じ 60Hz で待機させる。dt は元から固定なので、再現性には影響しない
	// (待機するだけ)。元の到達フレームレートを測りたい場合は --unpaced で無効にする (#72)。
	// --headless は NullDevice で GPU を使わないため対象外 (待機させても利用可能になる資源がない)。
	// オンラインは相手と同じ速さで進めないと先へ出た側が待つだけになるので、headless でも 60Hz に揃える。
	const bool paceScripted = !args.unpaced
		&& (args.online.any()
		    || ((!args.headless || args.headlessGpu3D)
		        && (args.noActivate || !args.inputScript.empty() || !args.captureDir.empty() || args.headlessGpu3D)));
	const auto paceStep = std::chrono::microseconds(1000000 / args.paceFps);
	std::chrono::steady_clock::time_point paceLastFrame{};

	// game が停止したとき、人が見ていない実行 (headless / 台本 / リプレイ照合 / キャプチャ) では、待っても
	// 誰も修正しないため、停止して exit 6 にする。GUI と --watch は停止の通知を表示したまま、新しい DLL を待つ。
	const bool stopOnFault = !args.watch
		&& (args.headless || !args.inputScript.empty() || captureOn
		    || (!args.replayPath.empty() && !args.replayGui));
	std::uint32_t handledFaults = 0;
	mitiru::debug::CrashReporter crashReporter;
	crashReporter.start();
	{
		auto& crashCtx = mitiru::debug::crashContext();
		mitiru::debug::setCrashContextText(crashCtx.replayPath, args.replayPath);
		mitiru::debug::setCrashContextText(crashCtx.inputScriptPath,
		                                   args.replayPath.empty() ? args.inputScript : std::string{});
	}

	cfg.onFrameStart = [paceScripted, paceStep, &paceLastFrame,
	                    &dllWatcher, &dllReload, &assetWatcher, &assetWatchDir,
	                    &ghostPlayer, &ghostActive, &ghostEof,
	                    captureOn, captureEvery, captureDir, &captureFrame, &captureSeq,
	                    &captureSaveFailed, &pngWriter,
	                    maxFrames = args.maxFrames, &totalFrame,
	                    perfOn = args.perf, &perfStats, &perfLog, &frameBudget,
	                    &pauseControlFile, &pauseControlTick, &pauseControlLast,
	                    &consolePending, consolePort,
	                    &iconPending, iconPath = args.iconPath,
	                    &dockWriter, &dockLastX, &dockLastY, &dockLastW, &dockLastH,
	                    stopOnFault, &handledFaults, &crashReporter, &windowShot, &ship, &hostStreaming, &online]
	                   (mitiru::Engine& engine)
	{
		ship.onFrame(engine);
		online.onFrame(engine);
		if (engine.moduleFaultCount() != handledFaults)
		{
			handledFaults = engine.moduleFaultCount();
			crashReporter.captureGameFault(engine.moduleFault(), engine.moduleCrashReportPath());
			// reload 直後の猶予中に停止して差し替え前へ戻った場合は停止状態ではないため、そのまま実行を続ける。
			if (stopOnFault && engine.moduleFaulted())
			{
				mitiru::console::verbose("人が見ていない実行なので、ゲームが止まったところで終了します。");
				engine.requestStop();
			}
		}

		// ペーシング (#72)。--perf が測る onFrameStart の間隔に待機時間も含まれるよう、
		// このコールバックの先頭で待つ。前フレームの開始を起点にするため、1 刻みを超えた
		// フレームの後はすぐに次へ進む (遅れを取り戻すために最高速度で実行することはない)。
		if (paceScripted)
		{
			if (paceLastFrame.time_since_epoch().count() != 0)
			{
				mitiru::util::waitUntil(paceLastFrame + paceStep);
			}
			paceLastFrame = std::chrono::steady_clock::now();
		}

		if (assetWatcher) { notifyAssetsReloaded(*assetWatcher, assetWatchDir, engine); }

		// --icon: window 生成後の初回フレームで一度だけ適用する (headless では no-op)。
		if (iconPending)
		{
			iconPending = false;
			engine.setWindowIcon(iconPath);
		}

		// --perf / --frame-times: 1 つ手前の onFrameStart からの実時間 = 1 host frame のコスト。
		// --frame-times は全フレーム分を残し、統計は tools/selfcheck.py が取る。
		if (perfOn || perfStats.keepAll)
		{
			const auto now = std::chrono::steady_clock::now();
			if (perfStats.hasLast)
			{
				const double ms = std::chrono::duration<double, std::milli>(now - perfStats.last).count();
				if (perfStats.keepAll) { perfStats.all.push_back(ms); }
				if (perfOn)
				{
					perfStats.samples.push_back(ms);
					if (perfStats.samples.size() >= 600) { perfStats.report(); }
				}
			}
			perfStats.last = now;
			perfStats.hasLast = true;
		}

		if (perfLog) { perfLog->onFrame(engine); }
		pollHostHotkeys(engine);

		// FrameArena (2-1): 前フレームの使用量をオーバーレイ用に反映する
		// (reset は次の tickOneFrame の先頭で行われるため、ここで読むのは直前のフレームの値)。
		frameBudget.setArenaUsage(engine.frameArena().used(), engine.frameArena().capacity());

		// --ghost (10-1): 1 host frame ごとに .mtrr から InputSnapshot を 1 件読み、処理を進める。
		// EOF に達した後は読まない (ghost は直前の GameMemory のまま静止して描画され続ける)。
		if (ghostActive && !ghostEof)
		{
			mitiru::module::InputSnapshot gsnap{};
			std::uint32_t                 gidx = 0;
			if (ghostPlayer.readNext(gsnap, gidx)) { engine.stepGhost(gsnap); }
			else { ghostEof = true; }
		}

		// --console: 初回フレームで HTTP listen を確認してからブラウザを開く (init 失敗時は開かない)。
		if (consolePending)
		{
			consolePending = false;
			if (engine.httpServer() != nullptr)
			{
				const std::string url = "http://127.0.0.1:" + std::to_string(consolePort) + "/";
				mitiru::console::verbosef("操作パネル %s をブラウザで開きます。", url.c_str());
#ifdef _WIN32
				ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
			}
			else
			{
				mitiru::console::notice("HTTP API を始められなかったので、--console の操作パネルを開けません。");
			}
		}

		// --pause-control: 数フレームごとにファイルを確認し、pause 状態を反映する (録画で編集中を静止させる)。
		// 値が前回の読み取りから変化した場合だけ上書きする (F8 / HTTP pause を毎 tick 上書きしない)。
		if (!pauseControlFile.empty() && (++pauseControlTick % 4 == 0))
		{
			std::ifstream pf(pauseControlFile);
			if (pf)
			{
				char c = 0;
				pf.get(c);
				if (c != pauseControlLast)
				{
					pauseControlLast = c;
					engine.setPauseKind(mitiru::EngineConfig::kPauseKindDebug);
					engine.setPaused(c == '1');
				}
			}
		}

		// listener 群 (scrub の適用など) は Engine_Frame.hpp の fixed-step ループが呼ぶ
		// (1-6)。host からここで重ねて呼ぶと二重に実行されるため、呼ばない。

		// ドック窓の追従用: 自窓の画面矩形が変わったら broadcast する (ツール窓が読み取って追従する)。
		{
			int wx = 0, wy = 0, ww = 0, wh = 0;
			if (engine.gameWindowRect(wx, wy, ww, wh) &&
			    (wx != dockLastX || wy != dockLastY || ww != dockLastW || wh != dockLastH))
			{
				dockLastX = wx; dockLastY = wy; dockLastW = ww; dockLastH = wh;
				const long long hwnd = engine.window()
					? static_cast<long long>(engine.window()->nativeHandle()) : 0;
				dockWriter.write({{"x", wx}, {"y", wy}, {"w", ww}, {"h", wh},
				                  {"hwnd", hwnd}, {"active", true}, {"min", false}});
			}
		}

		// --max-frames (#43): 指定フレーム数に達したら停止を要求する (headless 自動実行の終了条件)。
		// カウント自体は maxFrames の未指定時も進める (E10: headless 終了時の frames 要約に使う)。
		++totalFrame;
		if (maxFrames > 0 && totalFrame > maxFrames) { engine.requestStop(); }
		if (windowShot) { windowShot->onFrame(engine, totalFrame); }
		if (hostStreaming.active()) { hostStreaming.onFrame(engine, static_cast<long long>(totalFrame)); }

		// --capture-every (#43): N フレームごとに直近のフレームを PNG の連番として出力する。
		// onFrameStart は描画前なので「前フレームの提示結果」を保存する (AI による視覚検証には十分)。
		// 最初の onFrameStart には提示済みのフレームがまだないため数えない (1 枚目が真っ白になる)
		if (captureOn && totalFrame > 1 && (captureFrame++ % captureEvery == 0))
		{
			const int w = engine.captureWidth();
			const int h = engine.captureHeight();
			auto rgba = engine.capture();
			if (w > 0 && h > 0 && !rgba.empty() && pngWriter)
			{
				char name[32];
				std::snprintf(name, sizeof(name), "frame_%06d.png", captureSeq++);
				std::string path = captureDir + "/" + name;
				pngWriter->enqueue(path, std::move(rgba), w, h);
				// 失敗は非同期に判明するため、フレーム側では「初回検出時」に報告する
				// (エンコード自体は 1 フレーム以上遅れて完了する)。
				if (pngWriter->hadError() && !captureSaveFailed)
				{
					captureSaveFailed = true;   // 初回のみ報告 (毎フレーム spam しない)
					mitiru::console::noticef("画面を %s に保存するのに失敗しました。", pngWriter->lastErrorPath().c_str());
				}
			}
		}

		if (dllWatcher) { reloadChangedDll(*dllWatcher, engine, dllReload); }
	};

	mitiru::Engine engine;
	ship.attach(engine);
	online.attach(engine);
	engine.addFrameListener(&scrubListener);
	if (perfLog) { engine.addFrameListener(perfLog.get()); }
	engine.setSuppressToolWindows(args.noToolWindows);  // --no-tool-windows: 録画/CI でツール窓を出さない
	engine.setToolWindowPos(args.toolWinX, args.toolWinY);  // --tool-window-pos: 観察窓も実画面に出さない
	// 自動実行 (script 駆動 / replay / headless / capture) では、game の wantMouseLock を
	// OS へ適用しない。画面外のウィンドウが実カーソルを捕捉する事故を防ぐ
	engine.setAllowCursorCapture(args.inputScript.empty() && args.replayPath.empty()
	                             && !args.headless && args.captureDir.empty() && !args.noActivate);

	// SoundIntents を実際に再生するため、audio engine を接続する。id は
	// game の配置先である assets/audio/ ディレクトリ (DLL の隣) に対して解決する。
	// headless (自動テスト / AI 実行) では音が不要で、かつ #52 の音声スレッド競合を避けるため、
	// audio engine を作らない (sound intent は no-op、決定的な sim には影響しない)。
	// --audio-capture の時だけ、デバイスを開かない engine をステップに同期して読む (音声スレッドは無い)。
	const auto audioDir = std::filesystem::path(args.dllPath).parent_path() / "assets" / "audio";
	std::unique_ptr<mitiru::host::HostAudioCapture> audioCapture;
	if (!args.headless)
	{
		engine.setAudioEngine(std::make_shared<mitiru::host::FileAudioEngine>(audioDir));
	}
	else if (!args.audioCapture.empty())
	{
		auto offline = std::make_shared<mitiru::host::FileAudioEngine>(audioDir, mitiru::audio::MiniaudioEngine::Offline{
			mitiru::host::HostAudioCapture::kSampleRate, mitiru::host::HostAudioCapture::kChannels});
		audioCapture = std::make_unique<mitiru::host::HostAudioCapture>(
			[offline](float* out, ma_uint64 frames) { return offline->renderOffline(out, frames); });
		if (!audioCapture->open(args.audioCapture))
		{
			mitiru::console::noticef("--audio-capture の書き出し先 %s を開けません。", args.audioCapture.c_str());
			return 2;
		}
		engine.setAudioEngine(offline);
		engine.addFrameListener(audioCapture.get());
		mitiru::console::verbosef("音を %s に、鳴らした音の記録を %s に書きます。",
		                          args.audioCapture.c_str(), audioCapture->eventsPath().c_str());
	}

	// ── replay-as-test (軸 4) ──────────────────────────────────────────
	// --record: 毎フレームの InputSnapshot と game が push した view.* 状態を .mtrr に
	//   追記する。--replay-test: .mtrr をヘッドレスで再投入し DLL に bit-exact 再現させ、
	//   最終 view.* 状態を検証する。
	mitiru::replay::Recorder recorder;
	mitiru::replay::Player   player;
	std::uint32_t            frameIdx = 0;

	if (!args.recordPath.empty())
	{
		// header の seed と、毎フレームの snapshot の rngSeed を一致させる。
		if (!recorder.open(args.recordPath, cfg.randomSeed))
		{
			mitiru::console::noticef("--record の書き出し先 %s を開けません。", args.recordPath.c_str());
			return 2;
		}
		recorder.writeEnvTag(buildRecordEnvTag(cfg.gfxBackend));
		mitiru::console::verbosef("%s に録画します (状態は %d フレームごと)。", args.recordPath.c_str(), args.recordStateEvery);
		// --record-state-every N: state blob (GameMemory 全体) は N フレームごとにだけ書く
		// (0 = 毎フレーム = 従来)。1 MB/F で 1.4ms、8 MB で 12.5ms かかる録画コストを減らすため。
		// stateLen=0 のフレームは v4 format でも有効 (既存の Player がすでに許容する) なので、
		// format version は上げない。save/load intent が関係するフレームだけは周期にかかわらず
		// 必ず state を書く (replay 側での save/load の代用が周期の間で失敗しないようにする)。
		const int stateEvery = args.recordStateEvery;
		cfg.onModuleFrameRecorded =
			[&recorder, &frameIdx, &engine, stateEvery, blob = std::vector<std::uint8_t>{},
			 side = std::vector<std::uint8_t>{}](const mitiru::module::InputSnapshot& snap,
			                                    const mitiru::module::FrameIntents& fi) mutable
			{
				const bool saveOrLoad = fi.loadRequest != 0 || fi.saveRequest != 0;
				const bool wantState = stateEvery <= 0 ||
					(static_cast<int>(frameIdx) % stateEvery) == 0 || saveOrLoad;

				const std::uint32_t memSize = engine.moduleMemorySize();
				const void*         mem     = engine.moduleMemory();
				if (!wantState)
				{
					recorder.record(frameIdx++, snap, nullptr, 0);
				}
				else if (memSize > 0 && mem != nullptr && engine.moduleHasSideState())
				{
					// GameMemory の外に持つ状態は、照合用に hash だけを後ろに付ける。ロードを代用するフレームは
					// 戻せるよう bytes ごと付ける (observe/SideStateImage.hpp の形)。
					(void)engine.captureModuleSideState(side, saveOrLoad);
					blob.assign(static_cast<const std::uint8_t*>(mem), static_cast<const std::uint8_t*>(mem) + memSize);
					blob.insert(blob.end(), side.begin(), side.end());
					recorder.record(frameIdx++, snap, blob.data(), static_cast<std::uint32_t>(blob.size()));
				}
				else if (memSize > 0 && mem != nullptr)
				{
					// GameMemory が申告されていれば「唯一の state」を opaque のまま記録する
					// (bit-exact diffState 用)。未申告 (v≤8) の場合は、観測した view.* JSON にフォールバックする。
					recorder.record(frameIdx++, snap, mem, memSize);
				}
				else
				{
					std::string blob;
					if (auto* store = engine.moduleStateStore()) { blob = store->snapshotJson(); }
					recorder.record(frameIdx++, snap, blob.data(),
					                static_cast<std::uint32_t>(blob.size()));
				}
			};
	}

	// --input-record (#45): 実プレイの入力エッジを input-script 形式で書き出す。
	// `--input-script` で再生でき、#43 の headless+capture と組み合わせれば完全自動の回帰テストになる。
	// 既存の onModuleFrameRecorded (--record) があれば chain する。lifetime は runModule 内。
	std::ofstream inputRecOut;
	std::uint32_t inputRecFrame = 0;
	bool          inputRecHasPos = false;
	int           inputRecPosX = 0, inputRecPosY = 0;
	if (!args.inputRecordPath.empty())
	{
		inputRecOut.open(args.inputRecordPath, std::ios::binary);
		if (!inputRecOut)
		{
			mitiru::console::noticef("--input-record の書き出し先 %s を開けません。", args.inputRecordPath.c_str());
			return 2;
		}
		inputRecOut << "# mitiru input-script (--input-record). 形式: <frame> <KEY> <down|up> / <frame> pos <x> <y>\n";
		mitiru::console::verbosef("入力を %s に録ります。", args.inputRecordPath.c_str());
		auto prev = cfg.onModuleFrameRecorded;   // --record と併用時は chain
		cfg.onModuleFrameRecorded =
			[&inputRecOut, &inputRecFrame, &inputRecHasPos, &inputRecPosX, &inputRecPosY, prev](const mitiru::module::InputSnapshot& snap,
			                                     const mitiru::module::FrameIntents& fi)
			{
				if (prev) { prev(snap, fi); }
				for (int vk = 0; vk < 256; ++vk)
				{
					if (snap.keysJustPressed[vk])
						inputRecOut << inputRecFrame << ' ' << mitiru::input::keyNameFromVk(vk) << " down\n";
					if (snap.keysJustReleased[vk])
						inputRecOut << inputRecFrame << ' ' << mitiru::input::keyNameFromVk(vk) << " up\n";
				}
				// カーソル位置は動いたフレームだけ、ボタンより先に書く (再生時に「その位置で押した」状態になる)。
				const int px = static_cast<int>(snap.mouseX), py = static_cast<int>(snap.mouseY);
				if (!inputRecHasPos || px != inputRecPosX || py != inputRecPosY)
				{
					inputRecOut << inputRecFrame << " pos " << px << ' ' << py << '\n';
					inputRecPosX = px; inputRecPosY = py; inputRecHasPos = true;
				}
				static const char* const kBtnNames[3] = { "MouseL", "MouseR", "MouseM" };
				for (int b = 0; b < 3; ++b)
				{
					if (snap.mouseButtonsJustPressed[b])  inputRecOut << inputRecFrame << ' ' << kBtnNames[b] << " down\n";
					if (snap.mouseButtonsJustReleased[b]) inputRecOut << inputRecFrame << ' ' << kBtnNames[b] << " up\n";
				}
				++inputRecFrame;
			};
	}

	// GameMemory の再現検証 (flat POD game のみ)。replay 中に on_update 後の
	// live GameMemory を記録値と byte 単位で照合し、単一の state channel を test oracle にする。
	std::vector<std::uint8_t> recordedMem;
	std::uint32_t replayFrame     = 0;
	std::uint32_t memDivergeFrame = 0;
	bool          memDiverged     = false;
	bool          memCompared     = false;
	bool          memSizeMismatch = false;   // 録画時と GameMemory サイズが違う (struct 変更)
	std::size_t   memSizeRecorded = 0;
	std::size_t   memSizeCurrent  = 0;
	bool          frameHasRecord  = false;  // この frame に対応する記録 state を読めたか (EOF frame 除外)
	bool          keyframeRestored = false; // P1: bug_*.mtrr の frame 0 キーフレームを初回だけ復元したか
	bool          keyframeRejected = false; // P1: キーフレームを書き戻せなかった (形の違う DLL・窓口の image が無い)
	// P1: `observe::saveBugRing` (BugRing.hpp) が書く bug_*.mtrr は、frame 0 の state blob に
	// 「入力の窓に残る一番古い post-update 状態 (GameMemory + 窓口の image) のキーフレーム」を含める (通常の
	// --record-state-every による周期スナップショットと wire format 上は区別できないため、
	// 命名規約 (既定 prefix "bug_") で判定する)。
	bool          isBugRingReplay = !args.replayPath.empty() &&
		std::filesystem::path(args.replayPath).filename().string().rfind("bug_", 0) == 0;
	bool          loadSubstFailed = false;  // replay 中の load 代用が不能 (blob 無し録画)
	std::uint32_t loadSubstFailFrame = 0;
	std::string   memDivergeDiff;           // divergence 時の field 単位 diff JSON (MITIRU_REFLECT 済みなら)
	std::string   memDivergeBlame;          // divergence byte を最後に書いた phase 名 (`mitiru why` opt-in game のみ)
	std::string   finalReflect;             // 終端時の reflect 状態 JSON (fuzz の不変条件チェック用)
	mitiru::module::InputSnapshot lastRec{}; // --replay (GUI): EOF 後に直前入力を維持してフリーズする用

	if (!args.replayPath.empty())
	{
		const bool opened = player.open(args.replayPath);
		// リネームされた bug ring も envTag の印で見分ける (prefix は人が確認するための目印にすぎない)
		if (opened && player.recordedEnvTag().rfind("bugring|", 0) == 0) { isBugRingReplay = true; }
		if (!opened)
		{
			mitiru::console::noticef("録画 %s を開けません (%s)。", args.replayPath.c_str(), playerErrorName(player.lastError()));
			if (player.lastError() == mitiru::replay::PlayerError::FrameSizeMismatch)
			{
				// 別の ABI 世代の録画は再生できない (InputSnapshot layout が異なる)。何も知らせずに
				// 途中で失敗させず、記録時の ABI を添えて入口で拒否する。header の値は
				// wire version (build 指紋を含む)。表示時は数値の ABI 番号へ分解する。
				const std::string recAbi = player.recordedAbiVersion() > 0
					? "v" + std::to_string(mitiru::module::wireAbiNumber(
						static_cast<std::uint32_t>(player.recordedAbiVersion())))
					: std::string{"不明"};
				mitiru::console::noticef("この録画は別の版のエンジンで録られています (録画は ABI %s で 1 フレーム %u bytes、"
				                         "今は ABI v%u で %zu bytes)。今のエンジンで --record して録り直してください。",
				                         recAbi.c_str(),
				                         player.recordedFrameSize(),
				                         mitiru::module::kCurrentApiVersion,
				                         sizeof(mitiru::module::InputSnapshot));
			}
			else if (player.lastError() == mitiru::replay::PlayerError::VersionMismatch)
			{
				mitiru::console::notice("今のエンジンで --record して録り直してください。");
			}
			return 2;
		}
		if (!args.replayGui)
		{
			cfg.headless  = true;   // ヘッドレス決定的再実行
		}
		cfg.swRasterizeEvery = 0;  // 照合は GameMemory のみで pixels は読まない (#53)
		cfg.moduleInputOverride =
			[&player, &engine, &recordedMem, &frameHasRecord, &finalReflect, &lastRec,
			 &keyframeRestored, &keyframeRejected, isBugRingReplay, guiReplay = args.replayGui]
			(mitiru::module::InputSnapshot& snap) -> bool
			{
				const auto onEof = [&]() -> bool
				{
					frameHasRecord = false;
					// module が有効な間に最終 reflect 状態を取得する (ループ後は解放される可能性がある)
					finalReflect = engine.reflectBlobJson(engine.moduleMemory());
					if (guiReplay)
					{
						// --replay (GUI): 自動終了せず、直前の入力を維持したまま pause し、
						// 最後のフレームで停止する (デモ撮影・目視確認用)。
						engine.setPaused(true);
						snap = lastRec;
						return true;
					}
					engine.requestStop();   // --replay-test: EOF → ヘッドレスループを終了
					return false;
				};

				std::uint32_t fidx = 0;
				mitiru::module::InputSnapshot rec{};
				if (!player.readNextWithState(rec, recordedMem, fidx))  // 記録 GameMemory を退避
				{
					return onEof();
				}
				mitiru::debug::crashContext().replayRecord.store(fidx, std::memory_order_relaxed);

				// P1: bug_*.mtrr の frame 0 は「on_update 適用済み」の状態を含むキーフレームで、
				// rec (frame 0 の入力) はすでにその状態へ反映済み。そのまま on_update へ渡すと
				// 二重に適用されるため、キーフレームを moduleMemory へ書き戻したうえで frame 0 は
				// 消費済みとして扱い、代わりに frame 1 の入力を読んで返す (以後は通常の replay と同じ)。
				if (!keyframeRestored)
				{
					keyframeRestored = true;
					if (isBugRingReplay && !recordedMem.empty())
					{
						// GameMemory の外に持つ状態を持つ game のキーフレームは、GameMemory の後ろに窓口の image が続く。
						if (!engine.rewindModuleMemory(recordedMem.data(), static_cast<std::uint32_t>(recordedMem.size())))
						{
							keyframeRejected = true;
							mitiru::console::notice("録画の最初の状態を今の DLL に戻せません。録ったときと GameMemory の形が違います。");
							return onEof();
						}
						std::uint32_t fidxNext = 0;
						mitiru::module::InputSnapshot recNext{};
						if (!player.readNextWithState(recNext, recordedMem, fidxNext))
						{
							// キーフレームだけで後続の入力がない異常な録画。EOF として終了する。
							return onEof();
						}
						rec = recNext;
						mitiru::debug::crashContext().replayRecord.store(fidxNext, std::memory_order_relaxed);
					}
				}

				frameHasRecord = true;
				snap = rec;
				lastRec = rec;
				return true;
			};
		// on_update 後の live GameMemory を、退避した記録値と照合する (frame の対応は確認済み)。
		// EOF frame には対応する記録がないため比較しない (古い recordedMem との誤検出を防ぐ)。
		mitiru::observe::sideStateReplayMarks().reset();
		cfg.onModuleFrameRecorded =
			[&engine, &recordedMem, &replayFrame, &memDivergeFrame, &memDiverged, &memCompared,
			 &frameHasRecord, &memSizeMismatch, &memSizeRecorded, &memSizeCurrent, &memDivergeDiff,
			 &memDivergeBlame, liveSide = std::vector<std::uint8_t>{}]
			(const mitiru::module::InputSnapshot&, const mitiru::module::FrameIntents&) mutable
			{
				if (!frameHasRecord) { return; }
				const std::uint32_t memSize = engine.moduleMemorySize();
				const void*         mem     = engine.moduleMemory();
				// GameMemory の外に持つ状態の hash は GameMemory の後ろに付いている (録画側の onModuleFrameRecorded)。
				const bool sideGame = engine.moduleHasSideState();
				const bool sizeOk = sideGame ? recordedMem.size() > static_cast<std::size_t>(memSize)
				                             : recordedMem.size() == static_cast<std::size_t>(memSize);
				// 食い違った後も窓口ごとの照合は続け、どの窓口がどのフレームでずれたかを残す
				if (memSize > 0 && mem != nullptr && sizeOk && sideGame)
				{
					const std::string diverged = replaySideDivergence(engine, recordedMem, memSize, replayFrame, liveSide);
					if (!diverged.empty() && !memDiverged)
					{
						memCompared     = true;
						memDiverged     = true;
						memDivergeFrame = replayFrame;
						memDivergeDiff  = nlohmann::json::array({nlohmann::json{{"sideState", diverged}}}).dump();
					}
				}
				if (memSize > 0 && mem != nullptr && sizeOk)
				{
					memCompared = true;
					if (!memDiverged && std::memcmp(mem, recordedMem.data(), memSize) != 0)
					{
						memDiverged     = true;
						memDivergeFrame = replayFrame;
						// どの field が録画値から変化したか (divergence report)。
						memDivergeDiff  = engine.reflectDiffBlobs(recordedMem.data(), mem);
						// `mitiru why`: 最初に異なる byte を最後に書いた phase を game へ問い合わせる
						// (opt-in game のみ。mitiru_why_blame_at を export していなければ空のまま)。
						const auto* pm = static_cast<const std::uint8_t*>(mem);
						for (std::uint32_t i = 0; i < memSize; ++i)
						{
							if (pm[i] != recordedMem[i])
							{
								if (const char* ph = engine.queryModuleWriteBlame(i)) { memDivergeBlame = ph; }
								break;
							}
						}
					}
				}
				else if (memSize > 0 && !recordedMem.empty() && !sizeOk)
				{
					// サイズ不一致 = GameMemory struct が録画時から変更された。何も知らせずに
					// スキップすると false-green (検証 0 件で exit 0) になるため、明示的に FAIL とする。
					memSizeMismatch = true;
					memSizeRecorded = recordedMem.size();
					memSizeCurrent  = memSize;
				}
				++replayFrame;
			};
		// replay 中の load intent ではファイルを読まず、該当フレームの記録済み GameMemory blob
		// で代用する。録画後にセーブファイルが上書きされても bit-exact を保てる。
		// blob がない録画 (旧 .mtrr / memorySize=0) では代用できないため、明示的に FAIL とする (A3 と同じ考え方)。
		engine.setSaveLoadOverride(
			[&engine, &recordedMem, &replayFrame, &frameHasRecord,
			 &loadSubstFailed, &loadSubstFailFrame](const char* /*slot*/) -> bool
			{
				// EOF 後のフレーム (対応する記録なし) は検証対象外。何も適用せずにスキップする。
				if (!frameHasRecord) { return true; }
				const std::uint32_t memSize = engine.moduleMemorySize();
				// GameMemory の外に持つ状態を持つ game は、GameMemory の後ろに続く image ごと書き戻す。
				const bool sizeOk = engine.moduleHasSideState()
					? recordedMem.size() > static_cast<std::size_t>(memSize)
					: recordedMem.size() == static_cast<std::size_t>(memSize);
				if (memSize == 0 || !sizeOk
				    || !engine.rewindModuleMemory(recordedMem.data(), static_cast<std::uint32_t>(recordedMem.size())))
				{
					if (!loadSubstFailed)
					{
						loadSubstFailed    = true;
						loadSubstFailFrame = replayFrame;
					}
					engine.requestStop();
					return true;  // replay 中は失敗してもファイル load にフォールバックしない
				}
				return true;
			});
	}

	// record と replay はどちらも固定 dt (1/targetTps) で実行し、dt 列を一致させる
	// → sim を bit-exact で再現する (timer 駆動の状態も含む)。
	if (!args.recordPath.empty() || !args.replayPath.empty())
	{
		cfg.deterministic = true;
	}

	// --ghost (10-1):.mtrr はすでに上で開いている。ここでは module だけを load する
	// (同じ DLL の別の GameMemory。--replay と組み合わせれば ghost vs live を同時に再生できる)。
	if (ghostPlayer.isOpen())
	{
		if (!engine.loadGhostModule(args.dllPath))
		{
			mitiru::console::notice("--ghost で走らせるゲームの DLL を読み込むのに失敗しました。");
			return 2;
		}
		ghostActive = true;
		mitiru::console::verbosef("%s をゴーストとして並べて再生します。", args.ghostPath.c_str());
	}

	// ── デバッグ独立窓のオプトイン (アトミックツール哲学) ──────────────
	// 「このデバッグ機能を使いたい」と host を書く人が決めた窓だけ開く。要らなければ
	// 何も書かない = 何も出ない (pulled UI)。game のキー入力とは無関係に host 側で制御。
	//
	//   例: 状態 inspector を常に開きたい host にするなら、ここに直接書く:
	//       mitiru::debug::openTool(mitiru::Tool::Inspector);
	//       mitiru::debug::openTool(mitiru::Tool::Rewind);
	//
	// この参照 host では CLI (--inspect <name>) で選べるようにしてある。新しいツール窓は
	// ToolRegistry.hpp の kToolTable に 1 行足せば、ここの呼び出しはそのまま使える。
	for (std::size_t k = 0; k < args.openTools.size(); ++k)
	{
		// producerPid=0 → この host プロセスを監視。extraArgs は "--page scene?tab=memory" のような上書き
		const std::string& extra = k < args.openToolArgs.size() ? args.openToolArgs[k] : std::string{};
		mitiru::debug::openTool(args.openTools[k], extra);
	}

	// --state-trace: 毎フレーム reflect 状態を JSONL で追記する。reflectBlobJson は offset read
	// のみ = non-POD GameMemory (std::vector など) でも安全 (recording/ring と異なり memcpy しない)。
	// replay 時は別経路なので無効。Stage Doctor などが「解の軌跡/HP 推移」を後から解析できる。
	std::ofstream stateTraceOut;
	if (args.replayPath.empty() && !args.stateTrace.empty())
	{
		stateTraceOut.open(args.stateTrace, std::ios::trunc);
		if (stateTraceOut) { mitiru::console::verbosef("毎フレームの状態を %s に書きます。", args.stateTrace.c_str()); }
		else { mitiru::console::noticef("--state-trace の書き出し先 %s を開けません。", args.stateTrace.c_str()); }
	}

	// 台本は実機のキーボードの代わりなので、キー割り当て (ship.remap) より前に注入する。録画は組み替えた後を
	// 残すので、台本で録った .mtrr も割り当てに左右されずに再生できる。replay 中は台本を使わない。
	// ゲーム DLL が export するアクションの表 (MITIRU_ACTIONS) は読み込んだ後でしか分からないので、常に付ける。
	{
		const std::string freezeFile = args.inputFreezeControl;
		cfg.moduleInputRemap =
			[&scriptPlayer, &ship, scriptLoaded, freezeFile, frzTick = 0, frozen = false]
			(mitiru::module::InputSnapshot& snap) mutable
			{
				if (scriptLoaded)
				{
					scriptPlayer.apply(snap);   // 実キーボードを上書き (注入のみ有効)
					// --input-freeze-control: "1" の間は入力をすべて消去 → プレイヤーを静止させる。
					if (!freezeFile.empty())
					{
						if (++frzTick % 4 == 0)
						{
							std::ifstream pf(freezeFile);
							if (pf) { char c = 0; pf.get(c); frozen = (c == '1'); }
						}
						if (frozen)
						{
							for (int v = 0; v < 256; ++v)
							{
								snap.keysDown[v] = 0; snap.keysJustPressed[v] = 0; snap.keysJustReleased[v] = 0;
							}
						}
					}
				}
				ship.remap(snap);
			};
	}

	// --state-trace: このフレームの reflect 状態を 1 行追記する (offset read = non-POD でも安全)。
	if (args.replayPath.empty() && stateTraceOut.is_open())
	{
		cfg.moduleInputOverride =
			[&engine, &stateTraceOut](mitiru::module::InputSnapshot&) -> bool
			{
				// この行は直前の update の後の状態なので、その update の印 (hud.mark、ABI v48) を同じ行に付ける
				std::string line = engine.reflectBlobJson(engine.moduleMemory());
				const auto* intents = engine.lastModuleIntents();
				const std::string marks = (intents != nullptr) ? mitiru::module::detail::marksJson(*intents) : std::string();
				if (!marks.empty() && line.size() >= 2 && line.back() == '}')
				{
					line.pop_back();
					line += std::string(line.size() > 1 ? "," : "") + "\"_marks\":" + marks + "}";
				}
				stateTraceOut << line << '\n';
				return false;
			};
	}

	// 実績の依頼 (ABI v48) とオンラインの依頼 (ABI v50) は update のたびに 1 回だけ受ける。replay の再生では受けない。
	if (args.replayPath.empty())
	{
		auto prevRecorded = cfg.onModuleFrameRecorded;
		cfg.onModuleFrameRecorded =
			[&ship, &online, prevRecorded](const mitiru::module::InputSnapshot& snap, const mitiru::module::FrameIntents& fi)
			{
				if (prevRecorded) { prevRecorded(snap, fi); }
				ship.onModuleFrame(fi);
				online.onModuleFrame(fi);
			};
	}

	const auto runStart = std::chrono::steady_clock::now();
	if (!engine.runModule(args.dllPath, cfg))
	{
		// module load の失敗 (MITIRU_GAME の入口がない場合など)。理由は runModule がすでに stderr に出している。
		// 非ゼロを返すと、ランチャー .bat が pause してユーザーがエラーを読める。
		return 3;
	}
	if (hostStreaming.bakeFailures() > 0) { return 4; }   // --bake-caches で読めない資産があった

	if (perfStats.keepAll)
	{
		std::ofstream ft(args.frameTimes, std::ios::trunc);
		for (const double ms : perfStats.all) { ft << ms << '\n'; }
		if (ft) { mitiru::console::verbosef("%zu フレームの時間を %s に書きました。", perfStats.all.size(), args.frameTimes.c_str()); }
		else { mitiru::console::noticef("--frame-times の書き出し先 %s に書けませんでした。", args.frameTimes.c_str()); }
	}

	if (perfLog)
	{
		const bool ok = perfLog->write(args.perfLog);
		if (ok) { mitiru::console::verbosef("%zu フレームの計測を %s に書きました。", perfLog->frames(), args.perfLog.c_str()); }
		else { mitiru::console::noticef("--perf-log の書き出し先 %s に書けませんでした。", args.perfLog.c_str()); }
	}

	if (windowShot)
	{
		if (const int rc = windowShot->finish(); rc != 0) { return rc; }
	}

	if (audioCapture)
	{
		audioCapture->close();
		mitiru::console::verbosef("%.2f 秒の音を %s に書きました。",
		                          static_cast<double>(audioCapture->steps()) / 60.0, args.audioCapture.c_str());
	}

	if (args.headless && totalFrame > 0)
	{
		const double elapsedMs =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runStart).count();
		mitiru::console::verbosef("窓なしで %d フレーム回しました (1 フレーム平均 %.2f ms)。", totalFrame, elapsedMs / totalFrame);
	}

	// --perf: 端数ウィンドウの統計を出してから終了処理へ進む (#53)。
	if (args.perf) { perfStats.report(); }

	// game が停止したまま終了した。リプレイの verdict は、途中で停止した実行には付けられない。
	if (engine.moduleFaulted())
	{
		mitiru::console::noticef("ゲームが止まったまま終了しました。報告は %s にあります。",
		                         mitiru::debug::pathToUtf8(engine.moduleCrashReportPath()).c_str());
		return mitiru::debug::kExitGameFaulted;
	}
	if (const int rc = online.exitCode(); rc != 0) { return rc; }
	// --synctest を CI に置いたとき、見つけても 0 で終わると素通りする。
	if (args.synctest || args.oracleDeterminism)
	{
		if (const std::uint32_t n = mitiru::observe::oracleRingFor(&engine).mismatchTotal(); n > 0)
		{
			mitiru::console::noticef("同じ入力からの計算のやり直しが、記録と %u 回食い違いました。", n);
			return 1;
		}
	}

	// Replay 検証: 観測可能な最終状態を出力する。--expect の指定時はキー単位で diff し、
	// 不一致があれば非ゼロで終了する (CI のリグレッションゲート)。--replay (GUI) は verdict の対象外。
	if (!args.replayPath.empty() && !args.replayGui)
	{
		std::string finalState = "{}";
		if (auto* store = engine.moduleStateStore()) { finalState = store->snapshotJson(2); }
		// 録画環境 (envTag) を検証結果に含める。他の機器で再現しない差分の切り分けに使う
		// (v4 録画では recordedEnvTag() が空文字を返す)。game の JSON が壊れていた場合は処理しない。
		try
		{
			nlohmann::json j = nlohmann::json::parse(finalState);
			j["envTag"] = player.recordedEnvTag();
			finalState  = j.dump(2);
		}
		catch (const std::exception&) {}
		std::fprintf(stdout, "%s\n", finalState.c_str());

		// --json: verdict を stdout に 1 行の JSON としても出す (CLI 側の正規表現による解析を置き換えるため、E4)。
		// 既存の stderr のテキスト行はそのまま残す (人が読む用途/既存ツールとの後方互換)。
		auto emitJsonVerdict = [&](bool pass, const char* reason)
		{
			if (!args.jsonOutput) { return; }
			nlohmann::json v;
			v["verdict"]        = pass ? "PASS" : "FAIL";
			v["reason"]         = reason;
			v["framesCompared"] = player.framesRead();
			v["totalFrames"]    = player.totalFrames();
			if (!pass && reason == std::string_view{"diverged"})
			{
				v["divergedAtFrame"] = memDivergeFrame;
				if (!memDivergeDiff.empty() && memDivergeDiff != "[]")
				{
					try { v["diff"] = nlohmann::json::parse(memDivergeDiff); }
					catch (const std::exception&) { v["diff"] = memDivergeDiff; }
				}
				if (!memDivergeBlame.empty()) { v["blame"] = memDivergeBlame; }
			}
			std::fprintf(stdout, "%s\n", v.dump().c_str());
			// 読む側が頼りにする 1 行なので、終了処理の経路に関係なく出し切る (パイプは全バッファ)
			std::fflush(stdout);
		};

		if (keyframeRejected)
		{
			std::fprintf(stderr, "replay state: FAIL 録画の最初の状態を今の DLL に戻せません。"
			             "録ったときと同じ DLL で再生してください。\n");
			emitJsonVerdict(false, "bug_ring_keyframe_rejected");
			return 1;
		}
		// replay 中の load を代用できない。検証 0 件のまま exit 0 にしない (false-green を防ぐ)。
		if (loadSubstFailed)
		{
			std::fprintf(stderr,
			             "replay state: FAIL %u フレーム目のロードを再現できません。録画にゲームの状態が入っていないためです。"
			             "ゲームが GameMemory を申告しているかを確かめ、今のエンジンで --record して録り直してください。\n",
			             loadSubstFailFrame);
			emitJsonVerdict(false, "load_intent_unavailable");
			return 1;
		}

		// GameMemory 再現の verdict (flat POD game のみ)。bit-exact なら軸④の構造保証を証明できる。
		if (memSizeMismatch)
		{
			// 比較 0 件のまま exit 0 にすると「検証されていないのに成功」と見える (false-green)。
			std::fprintf(stderr,
			             "replay state: FAIL GameMemory の大きさが録画のとき (%zu bytes) と今 (%zu bytes) で違うので、"
			             "この録画では確かめられません。--record で録り直してください。\n",
			             memSizeRecorded, memSizeCurrent);
			emitJsonVerdict(false, "state_size_mismatch");
			return 1;
		}
		// C-2: 破損 / 切断された .mtrr を false-green にしない。clean EOF 以外の read 失敗と
		// header の frame 数との不一致は、読み取れた分が bit-exact でも検証不成立として FAIL とする。
		if (player.lastError() != mitiru::replay::PlayerError::None)
		{
			std::fprintf(stderr,
			             "replay state: FAIL 録画が%s (%u フレームのうち %llu フレームを読めました)。"
			             "--record で録り直してください。\n",
			             playerErrorName(player.lastError()),
			             player.totalFrames(),
			             static_cast<unsigned long long>(player.framesRead()));
			emitJsonVerdict(false, "replay_file_corrupt");
			return 1;
		}
		if (player.framesRead() != player.totalFrames())
		{
			std::fprintf(stderr,
			             "replay state: FAIL 録画の %u フレームのうち %llu フレームしか再生していません。"
			             "録画が途中で終わっているか、--max-frames で止めています。\n",
			             player.totalFrames(),
			             static_cast<unsigned long long>(player.framesRead()));
			emitJsonVerdict(false, "frame_count_mismatch");
			return 1;
		}
		// 比較 0 件は「検証していないのに成功したように見える」false-green になるため、明示的に FAIL とする。
		if (!memCompared)
		{
			std::fprintf(stderr,
			             "replay state: FAIL 録画にゲームの状態が入っていないので、1 フレームも確かめられません。"
			             "ゲームが GameMemory を申告しているかを確かめ、今のエンジンで --record して録り直してください。\n");
			emitJsonVerdict(false, "no_frames_compared");
			return 1;
		}
		if (memCompared)
		{
			if (memDiverged)
			{
				std::fprintf(stderr,
				             "replay state: FAIL (diverged at frame %u of %u frames) ゲームの状態が録画からずれました。\n",
				             memDivergeFrame, replayFrame);
				// どの field が変化したか (MITIRU_REFLECT 済みの game のみ。"[]" は記述子なし)
				if (!memDivergeDiff.empty() && memDivergeDiff != "[]")
				{
					std::fprintf(stderr, "replay diff: %s\n", memDivergeDiff.c_str());
				}
				// 異なる byte を最後に書いた phase (`mitiru why` opt-in game のみ = 原因となった phase)。
				if (!memDivergeBlame.empty())
				{
					std::fprintf(stderr, "replay blame: %s\n", memDivergeBlame.c_str());
				}
				emitJsonVerdict(false, "diverged");
			}
			else
			{
				std::fprintf(stderr,
				             "replay state: PASS (bit-exact, %u frames) 全フレームでゲームの状態が録画と一致しました。\n",
				             replayFrame);
				// --expect がある場合は、後続の expect 判定が最終 verdict になる。
				// ここで出すと --json の stdout に 2 行の verdict が出力され、消費側がどちらを
				// 参照するか一意に決まらない。
				if (args.expectPath.empty()) { emitJsonVerdict(true, "bit_exact"); }
			}
		}
		// fuzz などが field 単位の不変条件を確認できるよう、最終 reflect 状態を出す。
		if (!finalReflect.empty() && finalReflect != "{}")
		{
			std::fprintf(stdout, "replay final: %s\n", finalReflect.c_str());
		}
		if (memDiverged) { return 1; }  // 再現失敗 = CI gate fail

		if (!args.expectPath.empty())
		{
			std::ifstream ef(args.expectPath);
			if (!ef.is_open())
			{
				mitiru::console::noticef("--expect のファイル %s を開けません。", args.expectPath.c_str());
				return 2;
			}
			nlohmann::json expected, actual;
			try { ef >> expected; actual = nlohmann::json::parse(finalState); }
			catch (const std::exception& e)
			{
				mitiru::console::noticef("--expect の JSON を読めません (%s)。", e.what());
				return 2;
			}
			int mismatches = 0;
			nlohmann::json mismatchDetail = nlohmann::json::array();
			for (auto it = expected.begin(); it != expected.end(); ++it)
			{
				if (!actual.contains(it.key()) || actual[it.key()] != it.value())
				{
					std::fprintf(stderr, "  MISMATCH %s: expected %s, got %s\n",
					             it.key().c_str(), it.value().dump().c_str(),
					             actual.contains(it.key()) ? actual[it.key()].dump().c_str() : "(absent)");
					mismatchDetail.push_back({
					    {"field", it.key()},
					    {"expected", it.value()},
					    {"actual", actual.contains(it.key()) ? actual[it.key()] : nlohmann::json(nullptr)},
					});
					++mismatches;
				}
			}
			if (mismatches > 0)
			{
				std::fprintf(stderr, "replay assert FAILED: %d mismatch(es)\n", mismatches);
				if (args.jsonOutput)
				{
					nlohmann::json v{{"verdict", "FAIL"}, {"reason", "expect_mismatch"}, {"mismatches", mismatchDetail}};
					std::fprintf(stdout, "%s\n", v.dump().c_str());
					std::fflush(stdout);
				}
				return 1;
			}
			std::fprintf(stderr, "replay assert OK: final state matches --expect\n");
			if (args.jsonOutput)
			{
				nlohmann::json v{{"verdict", "PASS"}, {"reason", "expect_match"}};
				std::fprintf(stdout, "%s\n", v.dump().c_str());
				std::fflush(stdout);
			}
		}
	}
	return 0;
}

int main(int argc, char* argv[])
{
	// game の callback の外 (host 本体や game のスレッド) で落ちても minidump と報告を残す (docs/CRASH_REPORTS.md)
	mitiru::debug::installHostCrashHandler();
	const int rc = hostMain(argc, argv);
	g_guiLog.reportExit(rc, g_headlessRun);
	return rc;
}
