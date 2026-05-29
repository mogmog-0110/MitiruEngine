// mitiru_host — game-as-DLL モジュール用の最小エンジンランチャ (v0.2.0 step 3-4)
//
// Argv:
//   argv[0]  = host exe のパス
//   argv[1]  = game DLL のパス (cwd 相対または絶対)
//   argv[2+] = 任意フラグ:
//                --watch         DLL ファイルの mtime を監視し変更時にリロード
//                --url <url>     CEF 起動 URL を上書き (既定: file:///./<dll_dir>/assets/scene.html)
//
// ADR 0005: エンジンを直接見るのは host のみ。game コードは DLL 内に閉じ、
// ModuleApi.hpp の C-only シグナルフロー経由でエンジンと通信する。
//
// `--watch` は L3 ホットリロード: mtime を約 250ms ごとに監視し、変化したら
// HelloGameMemory* を生かしたまま DLL を差し替える (状態を保持)。
//
// Runtime hotkeys (Windows): F7 = step, F8 = pause/play, F9 = time-scale,
// F10 = lo-fi toggle, F12 = screenshot (file + clipboard).
//
// 将来: `mitiru-cli run [--watch]` が project/module.toml を解決して
// 等価な argv を自動構築する。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <mitiru/Mitiru.hpp>
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>
#include <mitiru/replay/Player.hpp>
#include <mitiru/replay/Recorder.hpp>

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

namespace
{

// FileAudioEngine — 論理 sound id を game の assets/audio/ 配下のファイルに解決し
// miniaudio で再生する host 側 IAudioEngine (ADR 0008)。game は触れず SoundIntents を
// 書くだけ。未知の id は無言で失敗させず stderr に出す。
class FileAudioEngine final : public mitiru::audio::IAudioEngine
{
public:
	explicit FileAudioEngine(std::filesystem::path baseDir)
		: m_baseDir(std::move(baseDir)) {}

	void playSound(std::string_view id) override { playByIdEx(id, 1.0f, 1.0f, 0.0f, /*music=*/false, false); }
	void playSound(std::string_view id, float vol) override { playByIdEx(id, vol, 1.0f, 0.0f, false, false); }
	void stopSound(std::string_view) override {}      // SE 一括 stop: 個別 id 追跡は未対応
	void playMusic(std::string_view id) override { playByIdEx(id, 1.0f, 1.0f, 0.0f, /*music=*/true, true); }
	void playMusic(std::string_view id, float vol, bool loop) override { playByIdEx(id, vol, 1.0f, 0.0f, true, loop); }
	void stopMusic() override { m_engine.stopMusic(); }
	void setVolume(float v) override { m_engine.setMasterVolume(v); }
	[[nodiscard]] bool isPlaying(std::string_view) const override { return false; }

	// v6 拡張 (#19/#20): pitch / fade を route。
	void playSoundEx(std::string_view id, float vol, float pitch, float fadeIn) override
	{
		playByIdEx(id, vol, pitch, fadeIn, /*music=*/false, false);
	}
	void stopSoundFade(std::string_view, float) override
	{
		// SE は id 別追跡が無いので fade 無し stop と同じ振る舞い (id stop も v1 未対応)。
	}
	void playMusicEx(std::string_view id, float vol, bool loop, float fadeIn) override
	{
		playByIdEx(id, vol, 1.0f, fadeIn, /*music=*/true, loop);
	}
	void stopMusicFade(float fadeOutSec) override { m_engine.stopMusicFade(fadeOutSec); }

private:
	void playByIdEx(std::string_view id, float volume, float pitch, float fadeIn,
	                bool music, bool loop)
	{
		for (const char* ext : {".wav", ".ogg", ".mp3"})
		{
			const auto p = m_baseDir / (std::string(id) + ext);
			if (std::filesystem::exists(p))
			{
				if (music) { m_engine.playMusicEx(p.string(), volume, loop, fadeIn); }
				else
				{
					m_engine.playSoundEx(p.string(), volume, pitch, fadeIn);
					// #34 BGM ducking heuristic: 閾値超の大音量 SE で BGM を一瞬引っ込める。
					if (volume >= kDuckSeThreshold)
					{
						m_engine.duckMusic(kDuckMul, kDuckSec);
					}
				}
				return;
			}
		}
		std::fprintf(stderr, "[mitiru_host] sound id not found under %s: %.*s\n",
		             m_baseDir.string().c_str(),
		             static_cast<int>(id.size()), id.data());
	}

	// #34 ducking パラメータ。閾値以上の SE 音量で BGM を mul 倍にし、sec で復帰。
	static constexpr float kDuckSeThreshold = 0.7f;
	static constexpr float kDuckMul         = 0.5f;
	static constexpr float kDuckSec         = 0.4f;

	std::filesystem::path          m_baseDir;
	mitiru::audio::MiniaudioEngine m_engine;
};

}  // namespace

namespace
{

/// プロセスの cwd を argv[0] のディレクトリに固定する。EngineConfig 内の相対パス
/// (cefStartUrl 等) を、どのシェルから起動しても解決できるようにするため。
void anchorCwdToExeDir(const char* argv0)
{
	if (argv0 == nullptr) { return; }
	std::error_code ec;
	const auto canon = std::filesystem::weakly_canonical(
		std::filesystem::path(argv0), ec);
	if (ec) { return; }
	std::filesystem::current_path(canon.parent_path(), ec);
}

/// DLL パスから "file:///./<dll_dir>/assets/scene.html" を組み立てる。
/// scene.html を DLL の隣に置いた game がそのまま動くようにするため。
std::string defaultCefUrlFor(const std::filesystem::path& dllPath)
{
	std::error_code ec;
	const auto rel = std::filesystem::relative(dllPath.parent_path(),
	                                            std::filesystem::current_path(), ec);
	std::filesystem::path under = (ec || rel.empty()) ? dllPath.parent_path() : rel;
	auto asset = under / "assets" / "scene.html";
	std::string url = "file:///./";
	url += asset.generic_string();
	return url;
}

struct CliArgs
{
	std::filesystem::path dllPath;
	std::string           cefUrlOverride;
	bool                  watch = false;
	bool                  helpRequested = false;
	int                   widthOverride  = 0;  // 0 = 既定 1280
	int                   heightOverride = 0;  // 0 = 既定 720
	std::string           recordPath;          // --record <f>: .mtrr を書き出す
	std::string           replayPath;          // --replay-test <f>: ヘッドレス再実行
	std::string           expectPath;          // --expect <f>: 最終 view.* 状態を検証
	std::string           fontMode;            // --font none|latin|kana|japanese (空=none=フォント skip)
	bool                  loFi = false;        // --lofi: 低解像+量子化+Bayerディザ
	int                   loFiW = 320, loFiH = 240; // --lofi-size WxH
	int                   loFiBitsR = 5, loFiBitsG = 6, loFiBitsB = 5; // --lofi-bits R,G,B (既定 RGB565)
	float                 loFiDither = 1.0f;   // --lofi-dither S
	int                   httpPort = 0;        // --http-port <N>: EngineHttpServer を listen 開始 (ADR 0011)
	bool                  console  = false;    // --console: HTTP + default browser で console.html 自動表示
};

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
		else if (a == "--watch")
		{
			out.watch = true;
		}
		else if (a == "--url")
		{
			if (i + 1 < argc) { out.cefUrlOverride = argv[++i]; }
		}
		else if (a == "--record")
		{
			if (i + 1 < argc) { out.recordPath = argv[++i]; }
		}
		else if (a == "--replay-test")
		{
			if (i + 1 < argc) { out.replayPath = argv[++i]; }
		}
		else if (a == "--expect")
		{
			if (i + 1 < argc) { out.expectPath = argv[++i]; }
		}
		else if (a == "--font")
		{
			if (i + 1 < argc) { out.fontMode = argv[++i]; }
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
		else if (a == "--size")
		{
			// 形式: WxH 例 "800x500"。両方とも正の整数であること。
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
			// 形式: R,G,B 例 "5,6,5"(RGB565) / "3,3,2"(256色相当)
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
		else if (a == "--lofi-dither")
		{
			if (i + 1 < argc) { try { out.loFiDither = std::stof(argv[++i]); out.loFi = true; } catch (...) {} }
		}
		else if (out.dllPath.empty())
		{
			out.dllPath = a;
		}
		// DLL の後ろの位置引数 = 旧式 URL スロット。
		else if (out.cefUrlOverride.empty())
		{
			out.cefUrlOverride = a;
		}
	}

	// 環境変数 MITIRU_WATCH=1 は --watch と同じ。
	if (const char* envWatch = std::getenv("MITIRU_WATCH");
	    envWatch && envWatch[0] != '\0' && std::string{envWatch} != "0")
	{
		out.watch = true;
	}
	return out;
}

void printUsage()
{
	std::fprintf(stderr,
		"usage: mitiru_host <game.dll> [options]\n"
		"\n"
		"options:\n"
		"  --watch          poll DLL file mtime, hot-reload on change\n"
		"  --size WxH       override window size (e.g. --size 800x500)\n"
		"  --url <url>      override CEF start URL\n"
		"  --font <mode>    none|latin|kana|japanese — native draw 用フォント\n"
		"                   (既定 none = フォント skip・起動高速。日本語 native text を\n"
		"                    出すなら japanese)\n"
		"  --lofi           低解像描画+パレット量子化+Bayerディザ (DX12, DirectX5期の質感)\n"
		"  --lofi-size WxH  内部解像度 (既定 320x240)\n"
		"  --lofi-bits R,G,B  量子化ビット数 (既定 5,6,5=RGB565 / 3,3,2=256色相当)\n"
		"  --lofi-dither S  ディザ強度 (既定 1.0, 0=ディザ無し)\n"
		"  --http-port N    EngineHttpServer を 127.0.0.1:N で開始 (runtime コントロール, ADR 0011)\n"
		"  --console        HTTP 起動 + 既定ブラウザで control panel を自動表示 (port 既定 8090)\n"
		"  --help, -h       this message\n"
		"\n"
		"hotkeys (runtime, Windows):\n"
		"  F7               paused 時に 1 フレーム step\n"
		"  F8               pause / play toggle (on_update dt=0 化、描画は継続)\n"
		"  F9               time-scale cycle (1x→0.5x→0.25x→2x→4x→1x)\n"
		"  F10              lo-fi post-FX toggle (DX12)\n"
		"  F12              screenshot — ./screenshots/frame_YYYYMMDD_HHMMSS.png + clipboard\n"
		"\n"
		"environment:\n"
		"  MITIRU_WATCH=1   same as --watch\n"
		"\n"
		"The host loads the game DLL via Engine::loadModule and drives the\n"
		"main loop. The DLL must export mitiru_module_load (see\n"
		"docs/adr/0005-host-game-c-abi-signal-flow.md).\n");
}

#ifdef _WIN32
/// RGBA8 トップダウン pixel buffer を Windows clipboard に CF_DIB として置く。
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
/// 1 VK あたり、down 状態を保持して立ち下がり検出を返す。GetAsyncKeyState を毎フレーム
/// 1 回叩く前提。
inline bool justPressed(int vk)
{
	static bool wasDown[256] = {};
	if (vk < 0 || vk >= 256) { return false; }
	const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	const bool jp = down && !wasDown[vk];
	wasDown[vk] = down;
	return jp;
}

/// F12 で window バックバッファを PNG 保存 + clipboard コピー。
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
		std::fprintf(stderr, "[mitiru_host] screenshot: %s (also on clipboard)\n", path.c_str());
	}
	else if (!path.empty())
	{
		std::fprintf(stderr, "[mitiru_host] screenshot: %s (clipboard copy failed)\n", path.c_str());
	}
	else if (clipOk)
	{
		std::fprintf(stderr, "[mitiru_host] screenshot on clipboard (file save failed)\n");
	}
	else
	{
		std::fprintf(stderr, "[mitiru_host] screenshot failed (file + clipboard)\n");
	}
}
#endif

/// onFrameStart から毎フレーム呼ぶ host hotkey 処理。Windows のみ。
inline void pollHostHotkeys(mitiru::Engine& engine)
{
#ifdef _WIN32
	if (justPressed(VK_F12))
	{
		doScreenshot(engine);
	}
	if (justPressed(VK_F8))
	{
		engine.togglePaused();
		std::fprintf(stderr, "[mitiru_host] %s\n", engine.isPaused() ? "PAUSED" : "PLAYING");
	}
	if (justPressed(VK_F7))
	{
		if (engine.isPaused())
		{
			engine.stepOneFrame();
			std::fprintf(stderr, "[mitiru_host] step 1 frame\n");
		}
	}
	if (justPressed(VK_F9))
	{
		// 1x → 0.5x → 0.25x → 2x → 4x → 1x で巡回
		static constexpr float kScales[] = { 1.0f, 0.5f, 0.25f, 2.0f, 4.0f };
		static constexpr int N = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
		static int idx = 0;
		idx = (idx + 1) % N;
		engine.setTimeScale(kScales[idx]);
		std::fprintf(stderr, "[mitiru_host] time-scale %.2fx\n", kScales[idx]);
	}
	if (justPressed(VK_F10))
	{
		engine.toggleLofi();
		std::fprintf(stderr, "[mitiru_host] lofi %s\n", engine.isLofiEnabled() ? "ON" : "OFF");
	}
#else
	(void)engine;  // host hotkeys は今のところ Windows 専用
#endif
}

/// ファイル監視状態 — onFrameStart クロージャにキャプチャされる。
struct WatcherState
{
	std::filesystem::path           dllPath;
	std::filesystem::file_time_type lastMtime{};
	int                             pollTick   = 0;
	int                             pollEvery  = 15;       // frame 数; 60fps で約 250ms
	bool                            initialized = false;
};

}  // namespace

int main(int argc, char* argv[])
{
	anchorCwdToExeDir(argc > 0 ? argv[0] : nullptr);

	const CliArgs args = parseArgs(argc, argv);
	if (args.helpRequested) { printUsage(); return args.dllPath.empty() ? 1 : 0; }

	std::error_code ec;
	if (!std::filesystem::exists(args.dllPath, ec) || ec)
	{
		std::fprintf(stderr, "mitiru_host: DLL not found: %s\n",
		             args.dllPath.string().c_str());
		return 2;
	}

	mitiru::EngineConfig cfg;
	cfg.title           = "mitiru_host";
	cfg.windowWidth     = args.widthOverride  > 0 ? args.widthOverride  : 1280;
	cfg.windowHeight    = args.heightOverride > 0 ? args.heightOverride :  720;
	cfg.vsync           = true;
	cfg.enableCef       = true;
	// EngineHttpServer (ADR 0011): --http-port > 0 か --console で HTTP listen を開始。
	// 127.0.0.1 限定。--console は既定ブラウザで control panel HTML を自動表示する (phase 3)。
	if (args.httpPort > 0 || args.console)
	{
		cfg.enableHttpApi = true;
		cfg.httpApiPort   = (args.httpPort > 0) ? args.httpPort : 8090;
	}
#ifdef _WIN32
	if (args.console)
	{
		// HTTP server は engine.run() 内で起動するので、開く側は少し遅らせる必要があるが、
		// ShellExecute は非同期だしブラウザ起動も時間がかかるので、現実には間に合う。
		const std::string url = "http://127.0.0.1:" + std::to_string(cfg.httpApiPort) + "/";
		std::fprintf(stderr, "[mitiru_host] control panel: %s (opening default browser)\n", url.c_str());
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
#endif
	// フォント: 既定はスキップ (起動高速。HTML/CEF UI なら日本語もそちらで出せる)。
	// native draw (drawTextInRect 等) で日本語/かなを描きたい game は
	// --font japanese|kana を指定する (エンジンが該当 glyph の SDF atlas を生成)。
	using FontAtlas = mitiru::EngineConfig::FontAtlas;
	if (args.fontMode.empty() || args.fontMode == "none")
	{
		cfg.skipDefaultFont = true;
	}
	else
	{
		cfg.skipDefaultFont = false;
		if      (args.fontMode == "latin") { cfg.fontAtlasRanges = FontAtlas::Latin; }
		else if (args.fontMode == "kana")  { cfg.fontAtlasRanges = FontAtlas::Kana; }
		else                                { cfg.fontAtlasRanges = FontAtlas::Japanese; }
	}
	// Mitiru Saturn 標準背景 — シルバーグレー (#c8c8c8)。エンジン同梱の全 surface
	// (hello_game / launcher / companion) はこのシルバー地に HUD を描き、Saturn の
	// 統一感を出す。別の背景が欲しい game はインスタンス単位で上書きできる。
	cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};

	// ローファイ・ポストFX: 低解像描画 + パレット量子化 + Bayer ディザ (DX12)
	if (args.loFi)
	{
		cfg.loFi.enabled       = true;
		cfg.loFi.internalWidth = args.loFiW;
		cfg.loFi.internalHeight= args.loFiH;
		cfg.loFi.colorBitsR    = args.loFiBitsR;
		cfg.loFi.colorBitsG    = args.loFiBitsG;
		cfg.loFi.colorBitsB    = args.loFiBitsB;
		cfg.loFi.ditherStrength= args.loFiDither;
	}
	cfg.cefStartUrl     = !args.cefUrlOverride.empty()
		? args.cefUrlOverride
		: defaultCefUrlFor(args.dllPath);

	// MITIRU_AUTOTEST_FRAMES は autotest の猶予を延ばし、スクショ発火前に CEF が
	// scene.html を読み込む時間を確保する。Engine::run の applyAutoTestEnv() は既定
	// 120 (約 2s) だが、CEF コールドブート時はまだ scene.html が描けていないことがある。
	// HUD オーバーレイを検証するスモークテストでは 600 (約 10s) に上げる。
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
	//   (2) --watch 時のみ: DLL mtime を polling して L3 ホットリロード
	// キャプチャした WatcherState はこのスタックフレームに置く。Engine::runModule が
	// ループ終了までブロックするので lifetime は問題ない。
	WatcherState watcher;
	if (args.watch)
	{
		watcher.dllPath = args.dllPath;
		// 絶対パスに解決し、cwd 変更後のリロードでもファイルを見つけられるようにする。
		std::error_code rc;
		auto abs = std::filesystem::absolute(args.dllPath, rc);
		if (!rc) { watcher.dllPath = abs; }

		std::fprintf(stderr, "[mitiru_host] watch mode: polling %s\n",
		             watcher.dllPath.string().c_str());
	}

	cfg.onFrameStart = [&watcher, watchOn = args.watch](mitiru::Engine& engine)
	{
		pollHostHotkeys(engine);

		if (!watchOn) { return; }
		if (++watcher.pollTick < watcher.pollEvery) { return; }
		watcher.pollTick = 0;

		std::error_code mtimeEc;
		const auto mtime =
			std::filesystem::last_write_time(watcher.dllPath, mtimeEc);
		if (mtimeEc) { return; }

		if (!watcher.initialized)
		{
			watcher.lastMtime   = mtime;
			watcher.initialized = true;
			return;
		}
		if (mtime > watcher.lastMtime)
		{
			watcher.lastMtime = mtime;
			std::fprintf(stderr, "[mitiru_host] DLL changed — reloading\n");
			const bool ok = engine.reloadModule(watcher.dllPath);
			if (!ok)
			{
				std::fprintf(stderr,
					"[mitiru_host] reload FAILED — continuing with old code\n");
			}
			else
			{
				std::fprintf(stderr, "[mitiru_host] reload OK\n");
			}
		}
	};

	mitiru::Engine engine;

	// SoundIntents (ADR 0008) を実際に鳴らすため audio engine を接続する。id は
	// game の配置先 assets/audio/ ディレクトリ (DLL の隣) に対して解決する。
	const auto audioDir =
		std::filesystem::path(args.dllPath).parent_path() / "assets" / "audio";
	engine.setAudioEngine(std::make_shared<FileAudioEngine>(audioDir));

	// ── replay-as-test (軸 4) ──────────────────────────────────────────
	// --record: 毎フレームの InputSnapshot と game が push した view.* 状態を .mtrr に
	//   追記する。--replay-test: .mtrr をヘッドレスで再投入し DLL に bit-exact 再現させ、
	//   最終 view.* 状態を検証する。
	mitiru::replay::Recorder recorder;
	mitiru::replay::Player   player;
	std::uint32_t            frameIdx = 0;

	if (!args.recordPath.empty())
	{
		if (!recorder.open(args.recordPath))
		{
			std::fprintf(stderr, "mitiru_host: cannot open record file: %s\n",
			             args.recordPath.c_str());
			return 2;
		}
		std::fprintf(stderr, "[mitiru_host] recording → %s\n", args.recordPath.c_str());
		cfg.onModuleFrameRecorded =
			[&recorder, &frameIdx, &engine](const mitiru::module::InputSnapshot& snap,
			                                const mitiru::module::FrameIntents&)
			{
				std::string blob;
				if (auto* store = engine.moduleStateStore()) { blob = store->snapshotJson(); }
				recorder.record(frameIdx++, snap, blob.data(),
				                static_cast<std::uint32_t>(blob.size()));
			};
	}

	if (!args.replayPath.empty())
	{
		if (!player.open(args.replayPath))
		{
			std::fprintf(stderr, "mitiru_host: cannot open replay file: %s\n",
			             args.replayPath.c_str());
			return 2;
		}
		cfg.enableCef = false;   // ヘッドレス決定的再実行
		cfg.headless  = true;
		cfg.moduleInputOverride =
			[&player, &engine](mitiru::module::InputSnapshot& snap) -> bool
			{
				mitiru::module::InputSnapshot rec{};
				std::uint32_t fidx = 0;
				if (!player.readNext(rec, fidx))   // state blob は破棄。入力のみ再投入する
				{
					engine.requestStop();   // EOF → ヘッドレスループを終了
					return false;
				}
				snap = rec;
				return true;
			};
	}

	// record と replay はどちらも固定 dt (1/targetTps) で走らせ、dt 列を一致させる
	// → sim が bit-exact 再現する (timer 駆動の状態も含む)。
	if (!args.recordPath.empty() || !args.replayPath.empty())
	{
		cfg.deterministic = true;
	}

#ifdef _WIN32
	std::fprintf(stderr,
		"[mitiru_host] hotkeys: F7=step F8=pause/play F9=time-scale F10=lofi F12=screenshot+clipboard\n");
#endif

	engine.runModule(args.dllPath, cfg);

	// Replay 検証: 観測可能な最終状態を出力する。--expect 指定時はキー単位で diff し、
	// 不一致があれば非ゼロ終了する (CI リグレッションゲート)。
	if (!args.replayPath.empty())
	{
		std::string finalState = "{}";
		if (auto* store = engine.moduleStateStore()) { finalState = store->snapshotJson(2); }
		std::fprintf(stdout, "%s\n", finalState.c_str());

		if (!args.expectPath.empty())
		{
			std::ifstream ef(args.expectPath);
			if (!ef.is_open())
			{
				std::fprintf(stderr, "mitiru_host: cannot open --expect file: %s\n",
				             args.expectPath.c_str());
				return 2;
			}
			nlohmann::json expected, actual;
			try { ef >> expected; actual = nlohmann::json::parse(finalState); }
			catch (const std::exception& e)
			{
				std::fprintf(stderr, "mitiru_host: replay assert: bad JSON: %s\n", e.what());
				return 2;
			}
			int mismatches = 0;
			for (auto it = expected.begin(); it != expected.end(); ++it)
			{
				if (!actual.contains(it.key()) || actual[it.key()] != it.value())
				{
					std::fprintf(stderr, "  MISMATCH %s: expected %s, got %s\n",
					             it.key().c_str(), it.value().dump().c_str(),
					             actual.contains(it.key()) ? actual[it.key()].dump().c_str() : "(absent)");
					++mismatches;
				}
			}
			if (mismatches > 0)
			{
				std::fprintf(stderr, "replay assert FAILED: %d mismatch(es)\n", mismatches);
				return 1;
			}
			std::fprintf(stderr, "replay assert OK: final state matches --expect\n");
		}
	}
	return 0;
}
