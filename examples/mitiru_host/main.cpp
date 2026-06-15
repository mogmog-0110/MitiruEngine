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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// アンブレラ廃止 (リファクタ P2) — 使うものだけ明示 include
#include <mitiru/core/Engine.hpp>
#include <mitiru/resource/AssetPath.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/asset/AssetPack.hpp> // vfs: pack mount / readGlobal (ADR 0016)
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/observe/ScrubControlChannel.hpp>  // time-travel click-to-scrub (ADR 0017)
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

	// 毎フレームの定期掃除 (終了 SE voice 回収 + fade-out 完了 music の解放、#51)。
	void update() override { m_engine.update(); }

	// 再生中 voice のメーターを miniaudio backend からそのまま中継 (mitiru_mixer 窓用)。
	[[nodiscard]] std::vector<mitiru::audio::ChannelMeter> meterChannels() const override
	{
		return m_engine.meterChannels();
	}

	// マスター再生クロック (秒) を miniaudio から中継。game が音声クロック基準で判定するため。
	[[nodiscard]] double masterTimeSec() const noexcept override { return m_engine.masterTimeSec(); }

private:
	void playByIdEx(std::string_view id, float volume, float pitch, float fadeIn,
	                bool music, bool loop)
	{
		for (const char* ext : {".wav", ".ogg", ".mp3"})
		{
			const auto        p   = m_baseDir / (std::string(id) + ext);
			const std::string key = p.generic_string();

			// 再生に渡す実ファイルパスを決める。pack mount 時はパックから取り出した
			// temp ファイル、dev (未 mount) 時は disk のファイル (ADR 0016)。
			std::string playPath;
			if (mitiru::vfs::hasGlobalMount())
			{
				playPath = materializeFromPack(key, ext);
			}
			else if (std::filesystem::exists(p))
			{
				playPath = p.string();
			}
			if (playPath.empty()) { continue; }

			if (music) { m_engine.playMusicEx(playPath, volume, loop, fadeIn); }
			else
			{
				m_engine.playSoundEx(playPath, volume, pitch, fadeIn);
				// #34 BGM ducking heuristic: 閾値超の大音量 SE で BGM を一瞬引っ込める。
				if (volume >= kDuckSeThreshold)
				{
					m_engine.duckMusic(kDuckMul, kDuckSec);
				}
			}
			return;
		}
		std::fprintf(stderr, "[mitiru_host] sound id not found under %s: %.*s\n",
		             m_baseDir.string().c_str(),
		             static_cast<int>(id.size()), id.data());
	}

	/// pack 中の音声 (key) を %TEMP% に一度だけ取り出し、その path を返す。
	/// 配布物にバラ音声を置かないための経路 (ADR 0016)。pack に無ければ空。
	std::string materializeFromPack(const std::string& key, const char* ext)
	{
		if (auto it = m_audioTemp.find(key); it != m_audioTemp.end()) { return it->second; }
		const auto bytes = mitiru::vfs::readGlobal(key);
		if (!bytes) { return {}; }
		// key の FNV-1a で安定した temp 名を作る (run 跨ぎで再利用)。
		std::uint64_t h = 1469598103934665603ULL;
		for (unsigned char c : key) { h = (h ^ c) * 1099511628211ULL; }
		char name[64];
		std::snprintf(name, sizeof(name), "mitiru_aud_%016llx%s",
		              static_cast<unsigned long long>(h), ext);
		const auto out = std::filesystem::temp_directory_path() / name;
		std::error_code ec;
		if (!std::filesystem::exists(out, ec))
		{
			std::ofstream f(out, std::ios::binary | std::ios::trunc);
			if (!f) { return {}; }
			if (!bytes->empty())
			{
				f.write(reinterpret_cast<const char*>(bytes->data()),
				        static_cast<std::streamsize>(bytes->size()));
			}
		}
		const std::string s = out.string();
		m_audioTemp.emplace(key, s);
		return s;
	}

	// #34 ducking パラメータ。閾値以上の SE 音量で BGM を mul 倍にし、sec で復帰。
	static constexpr float kDuckSeThreshold = 0.7f;
	static constexpr float kDuckMul         = 0.5f;
	static constexpr float kDuckSec         = 0.4f;

	std::filesystem::path                        m_baseDir;
	mitiru::audio::MiniaudioEngine               m_engine;
	std::unordered_map<std::string, std::string> m_audioTemp;  ///< pack→temp 取り出しキャッシュ
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

/// exe と同じ場所の <exeStem>.mtargs があれば、その中身を argv[1..] 相当の
/// token 列として読む (引数なし起動 = ダブルクリック / Steam 用)。空白区切りで
/// 分割し、先頭 token は通常 game DLL の相対パス。`mitiru dist --exe` が生成する。
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
	std::string tok;
	while (f >> tok) { tokens.push_back(tok); }
	return tokens;
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
	// 不在は従来完全沈黙だった (HUD だけ出ない画面になり原因が追えない)。起動時に 1 行だけ知らせる。
	if (!std::filesystem::exists(dllPath.parent_path() / "assets" / "scene.html", ec))
	{
		std::fprintf(stderr,
			"[mitiru_host] note: HUD 用の scene.html が見つかりません: %s\n"
			"  (HUD 無しでゲーム本体は動きます。HTML/CSS の HUD を使う場合はこの場所に置いてください)\n",
			(dllPath.parent_path() / "assets" / "scene.html").string().c_str());
	}
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
	std::string           stateDiffA;          // --state-diff <a> <b>: 2 録画の分岐 frame を報告
	std::string           stateDiffB;
	std::string           fontMode;            // --font none|latin|kana|japanese (空=既定=かな)
	std::string           fontFace;            // --font-face normal|retro (空=normal=M+ Rounded)
	bool                  loFi = false;        // --lofi: 低解像+量子化+Bayerディザ
	int                   loFiW = 320, loFiH = 240; // --lofi-size WxH
	int                   loFiBitsR = 5, loFiBitsG = 6, loFiBitsB = 5; // --lofi-bits R,G,B (既定 RGB565)
	float                 loFiDither = 1.0f;   // --lofi-dither S
	int                   httpPort = 0;        // --http-port <N>: EngineHttpServer を listen 開始 (ADR 0011)
	bool                  console  = false;    // --console: HTTP + default browser で console.html 自動表示
	bool                  noCef    = false;    // --no-cef: CEF を起動しない (完全ネイティブ描画の game 用、起動軽量化)
	std::string           captureDir;          // --capture-dir <d>: 毎 N フレーム PNG を吐く先 (#43)
	int                   captureEvery = 0;    // --capture-every <N>: N フレームごとに 1 枚 (0=off)
	bool                  headless = false;    // --headless: ウィンドウ無しで走らせる (AI 自動回し)
	float                 speed    = 1.0f;     // --speed <N>: time scale 倍率 (固定 dt × N で早回し)
	int                   maxFrames = 0;       // --max-frames <N>: N フレーム走ったら自動終了 (0=無制限)
	bool                  fixedSize = false;   // --fixed-size: ユーザのウィンドウリサイズを禁止 (#44)
	bool                  noPauseUnfocused = false; // --no-pause-unfocused: 非フォーカスでもフルレート継続 (vsync off)
	bool                  noVsync = false;     // --no-vsync: present の vsync 待ちを切る (素のフレームコスト計測, #53)
	bool                  perf    = false;     // --perf: 実フレーム時間の統計を定期表示 (#53)
	std::string           inputScript;         // --input-script <f>: in-process 入力注入 (#43-1)
	std::string           inputRecordPath;     // --input-record <f>: 実入力を input-script 形式で録画 (#45)
	std::vector<mitiru::Tool> openTools;       // --inspect <name>: 起動時に開くツール独立窓 (ADR 0014)
	std::string           errorFile;           // --error-file <f>: mitiru watch のビルドエラー帯 (存在中だけ表示)
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
		else if (a == "--state-diff")
		{
			if (i + 2 < argc) { out.stateDiffA = argv[++i]; out.stateDiffB = argv[++i]; }
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
		else if (a == "--no-cef")
		{
			// 完全ネイティブ描画の game (HTML UI を使わない) は CEF を起動しないことで
			// Chromium コールドブートの起動スパイク + GPU/renderer サブプロセス常駐を避ける。
			out.noCef = true;
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
		else if (a == "--speed")
		{
			if (i + 1 < argc) { try { out.speed = std::stof(argv[++i]); } catch (...) {} }
		}
		else if (a == "--max-frames")
		{
			if (i + 1 < argc) { try { out.maxFrames = std::stoi(argv[++i]); } catch (...) {} }
		}
		else if (a == "--fixed-size")
		{
			out.fixedSize = true;
		}
		else if (a == "--input-script")
		{
			if (i + 1 < argc) { out.inputScript = argv[++i]; }
		}
		else if (a == "--input-record")
		{
			if (i + 1 < argc) { out.inputRecordPath = argv[++i]; }
		}
		else if (a == "--error-file")
		{
			if (i + 1 < argc) { out.errorFile = argv[++i]; }
		}
		else if (a == "--inspect")
		{
			// --inspect [inspector|input|timetravel] (省略時 inspector)。
			// host を書く人が「この窓を使う」と決めた物だけ開く (ADR 0014)。
			mitiru::Tool t = mitiru::Tool::Inspector;
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				const std::string name{argv[++i]};
				if      (name == "input")      { t = mitiru::Tool::InputMonitor; }
				else if (name == "timetravel") { t = mitiru::Tool::TimeTravel; }
				else if (name == "scene")      { t = mitiru::Tool::SceneTree; }
				else if (name == "perf")       { t = mitiru::Tool::Perf; }
				else if (name == "mixer")      { t = mitiru::Tool::AudioMixer; }
			}
			out.openTools.push_back(t);
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
		"  --no-pause-unfocused  keep running at full rate when window is unfocused (#46b)\n"
		"  --no-vsync       present の vsync 待ちを切る (フレームコストの素を計測する用, #53)\n"
		"  --perf           実フレーム時間の統計 (avg/p50/p95/max) を 600 フレームごとに表示 (#53)\n"
		"                   GPU 実機の描画コスト計測は windowed + --perf --no-vsync で\n"
		"  --url <url>      override CEF start URL\n"
		"  --font <mode>    none|latin|kana|japanese — native draw 用フォント (既定 kana)\n"
		"  --font-face <f>  normal|retro — 普通(M+ Rounded) / レトロ(PixelMplus) (既定 normal)\n"
		"                   (既定 none = フォント skip・起動高速。日本語 native text を\n"
		"                    出すなら japanese)\n"
		"  --lofi           低解像描画+パレット量子化+Bayerディザ (DX12, DirectX5期の質感)\n"
		"  --lofi-size WxH  内部解像度 (既定 320x240)\n"
		"  --lofi-bits R,G,B  量子化ビット数 (既定 5,6,5=RGB565 / 3,3,2=256色相当)\n"
		"  --lofi-dither S  ディザ強度 (既定 1.0, 0=ディザ無し)\n"
		"  --http-port N    EngineHttpServer を 127.0.0.1:N で開始 (runtime コントロール, ADR 0011)\n"
		"  --console        HTTP 起動 + 既定ブラウザで control panel を自動表示 (port 既定 8090)\n"
		"  --no-cef         CEF を起動しない (完全ネイティブ描画の game 用・起動軽量化)\n"
		"  --capture-dir D  毎 N フレームのフレームを PNG 連番で D に吐く (AI 視覚検証, #43)\n"
		"  --capture-every N  上記の間隔 (フレーム数。--capture-dir 指定時の既定 30)\n"
		"  --headless       ウィンドウ無しで走らせる (AI 自動プレイの裏回し)\n"
		"  --speed N        time scale 倍率 (固定 dt × N で早回し。長いプレイの自動回し用)\n"
		"  --max-frames N   N フレーム走ったら自動終了 (headless 自動回しの停止条件, #43)\n"
		"  --fixed-size     ユーザのウィンドウリサイズを禁止 (固定解像度運用, #44)\n"
		"  --input-script F 入力スクリプト F を in-process 注入 (OS 入力を経由せず他アプリに漏れない, #43)\n"
		"                   形式: 1 行 '<frame> <down|up> <KEY>' (# でコメント)。KEY=Left/Right/Up/Down/\n"
		"                   Space/Enter/Escape/英数字1字/生 VK 整数。実キーボードは無視される\n"
		"  --input-record F 実プレイの入力を input-script 形式で F に録画 (--input-script で再生可, #45)\n"
		"  --error-file F   ビルドエラーファイル F を監視し、存在する間だけ画面上部に帯を表示\n"
		"                   (mitiru watch が自動指定。直して保存 → ビルド成功で帯が消える)\n"
		"  --inspect [name] ツール独立窓を起動時に開く (name=inspector|input|timetravel, 既定 inspector)\n"
		"                   ※ host を書く人が main.cpp で mitiru::debug::openTool(Tool::X) と\n"
		"                     直接書けば、欲しい窓だけコードで指定できる (ADR 0014)\n"
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
	// host hotkey は GetAsyncKeyState (グローバル) で読むため、自プロセスのウィンドウが
	// 前面の時だけ処理する。さもないと別アプリで作業中の F7-F12 を奪ってしまう
	// (ゲーム入力自体は WM_KEYDOWN でフォーカス限定済みだが、ここだけグローバルだった)。
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

// ── 入力スクリプト (#43-1, in-process 注入) ────────────────────────────────
// OS 入力 (SendInput) を経由せず InputSnapshot を直接書き換えるので、他アプリにキーが
// 漏れない・headless でも効く・決定的。実キーボードはスクリプト実行中は無視される。

/// KEY 名 → 仮想キーコード。1字英数字は ASCII 大文字 / 数字、名前は主要キー、生 VK 整数も可。
inline int keyNameToVk(const std::string& s)
{
	if (s.empty()) { return -1; }
	if (s.size() == 1)
	{
		char c = s[0];
		if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
		{
			return static_cast<int>(static_cast<unsigned char>(c));
		}
	}
	std::string u = s;
	for (auto& c : u) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
	if (u == "LEFT")  { return 0x25; }
	if (u == "UP")    { return 0x26; }
	if (u == "RIGHT") { return 0x27; }
	if (u == "DOWN")  { return 0x28; }
	if (u == "SPACE") { return 0x20; }
	if (u == "ENTER" || u == "RETURN") { return 0x0D; }
	if (u == "ESCAPE" || u == "ESC")   { return 0x1B; }
	if (u == "SHIFT") { return 0x10; }
	try { return std::stoi(s, nullptr, 0); } catch (...) { return -1; }
}

/// 仮想キーコード → 名前（keyNameToVk の逆。--input-record の出力に使う）。
inline std::string vkToName(int vk)
{
	switch (vk)
	{
	case 0x25: return "Left";
	case 0x26: return "Up";
	case 0x27: return "Right";
	case 0x28: return "Down";
	case 0x20: return "Space";
	case 0x0D: return "Enter";
	case 0x1B: return "Escape";
	case 0x10: return "Shift";
	default: break;
	}
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
	{
		return std::string(1, static_cast<char>(vk));
	}
	return std::to_string(vk);  // 名前の無いキーは生 VK 整数
}

struct InputScriptEvent { int frame; int vk; bool down; };

/// スクリプトを毎フレーム適用し InputSnapshot のキー配列を上書きするプレイヤ。
struct InputScriptPlayer
{
	std::vector<InputScriptEvent> events;  // frame 昇順
	std::size_t cursor = 0;
	int frame = 0;
	bool held[256] = {};

	void apply(mitiru::module::InputSnapshot& snap)
	{
		bool prev[256];
		std::memcpy(prev, held, sizeof(prev));
		while (cursor < events.size() && events[cursor].frame <= frame)
		{
			const auto& e = events[cursor++];
			if (e.vk >= 0 && e.vk < 256) { held[e.vk] = e.down; }
		}
		for (int v = 0; v < 256; ++v)
		{
			snap.keysDown[v]         = held[v] ? 1 : 0;
			snap.keysJustPressed[v]  = (held[v] && !prev[v]) ? 1 : 0;
			snap.keysJustReleased[v] = (!held[v] && prev[v]) ? 1 : 0;
		}
		++frame;
	}
};

/// '<frame> <down|up> <KEY>' 形式 (# でコメント) を読む。失敗時 false。
inline bool loadInputScript(const std::string& path, InputScriptPlayer& out)
{
	std::ifstream f(path);
	if (!f) { return false; }
	std::string line;
	while (std::getline(f, line))
	{
		const auto h = line.find('#');
		if (h != std::string::npos) { line = line.substr(0, h); }
		std::istringstream is(line);
		int frame = 0;
		std::string t2, t3;
		if (!(is >> frame >> t2 >> t3)) { continue; }
		// 両形式を許す: "<frame> <down|up> <KEY>" と "<frame> <KEY> <down|up>"。
		// (--input-record の出力は後者。#45 の例 `120 Z down` もこれ。)
		auto isAct = [](const std::string& s) {
			return s == "down" || s == "d" || s == "DOWN" || s == "up" || s == "u" || s == "UP";
		};
		std::string act, key;
		if (isAct(t2)) { act = t2; key = t3; }
		else           { key = t2; act = t3; }
		const int vk = keyNameToVk(key);
		if (vk < 0) { continue; }
		const bool down = (act == "down" || act == "d" || act == "DOWN");
		const bool up   = (act == "up" || act == "u" || act == "UP");
		if (!down && !up) { continue; }
		out.events.push_back({frame, vk, down});
	}
	std::stable_sort(out.events.begin(), out.events.end(),
		[](const InputScriptEvent& a, const InputScriptEvent& b) { return a.frame < b.frame; });
	return true;
}

}  // namespace

int main(int argc, char* argv[])
{
	anchorCwdToExeDir(argc > 0 ? argv[0] : nullptr);

	// 引数なし起動 (ダブルクリック / Steam) のときは sidecar <exe>.mtargs から
	// argv を補う。これで mitiru_host を <game>.exe にリネーム配布できる。
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

	const CliArgs args = parseArgs(useArgc, useArgv);
	if (args.helpRequested) { printUsage(); return args.dllPath.empty() ? 1 : 0; }

	// --state-diff A B: DLL 不要。2 つの .mtrr の GameMemory blob を byte 比較し、
	// 「同入力・異コードでどの frame から分岐したか」を 1 行 JSON で報告する (ADR 0013)。
	if (!args.stateDiffA.empty() && !args.stateDiffB.empty())
	{
		const auto d = mitiru::replay::Player::diffState(args.stateDiffA, args.stateDiffB);
		if (d.totalFrames == 0)
		{
			std::fprintf(stderr, "mitiru_host: --state-diff: 比較不能 (header/形式エラー or 空録画)\n");
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

	std::error_code ec;
	if (!std::filesystem::exists(args.dllPath, ec) || ec)
	{
		std::fprintf(stderr, "mitiru_host: DLL not found: %s\n",
		             args.dllPath.string().c_str());
		return 2;
	}

	// 秘匿配布 (ADR 0016): DLL の隣に assets.mtpak があれば mount。以後 CEF (app://) も
	// native loader (Texture/ImageLoader) も音声も vfs::readGlobal 経由で pack から読む。
	std::string packedAppUrl;
	{
		const auto packPath =
			std::filesystem::path(args.dllPath).parent_path() / "assets.mtpak";
		if (std::filesystem::exists(packPath, ec) && !ec)
		{
			if (auto pack = mitiru::vfs::AssetPack::open(packPath))
			{
				mitiru::vfs::mountGlobal(std::move(*pack));
				// header-only の mount static は host / game DLL / CEF helper で別インスタンス。
				// 環境変数で pack パスを共有し、各モジュールが lazy mount する (境界越え)。
				const auto absPack = std::filesystem::absolute(packPath, ec);
				const std::string packEnv = (ec ? packPath : absPack).string();
#ifdef _WIN32
				_putenv_s("MITIRU_ASSET_PACK", packEnv.c_str());
#else
				setenv("MITIRU_ASSET_PACK", packEnv.c_str(), 1);
#endif
				// pack キーは cwd 相対の "<gameDir>/assets/...". CEF の virtualPath を
				// それに合わせるため <gameDir> を cwd からの相対で前置する。
				const auto rel = std::filesystem::relative(
					std::filesystem::path(args.dllPath).parent_path(),
					std::filesystem::current_path(), ec);
				const auto under = (ec || rel.empty())
					? std::filesystem::path(args.dllPath).parent_path() : rel;
				packedAppUrl = "app://" + under.generic_string() + "/assets/scene.html";
				std::fprintf(stdout, "[mitiru_host] asset pack mounted: %s\n",
				             packPath.string().c_str());
			}
		}
	}

	mitiru::EngineConfig cfg;
	cfg.title           = "mitiru_host";
	cfg.windowWidth     = args.widthOverride  > 0 ? args.widthOverride  : 1280;
	cfg.windowHeight    = args.heightOverride > 0 ? args.heightOverride :  720;
	// resize 安全: 要求サイズの半分を floor にする (極端な潰れだけ防ぎ、指定サイズは
	// 超えない)。game 窓 / launcher / 760x80 の companion bar を同じ host が起動するので、
	// 固定値でなく要求サイズ基準にして「floor > 指定」で窓が開けない事態を避ける。
	{
		const int floorW = cfg.windowWidth  / 2;
		const int floorH = cfg.windowHeight / 2;
		cfg.minWindowWidth  = floorW > 200 ? floorW : 200;
		cfg.minWindowHeight = floorH > 48  ? floorH : 48;
	}
	cfg.vsync           = true;
	if (args.noPauseUnfocused) { cfg.vsync = false; }  // 背面でもフルレート (present の vsync 待ちを回避)
	if (args.noVsync)          { cfg.vsync = false; }  // --no-vsync: 素のフレームコスト計測 (#53)
	cfg.enableCef       = !args.noCef;   // --no-cef: 完全ネイティブ game は CEF 抜きで軽量起動
	cfg.timeScale       = args.speed;    // --speed: 固定 dt × N 早回し (#43)
	cfg.errorBannerFile = args.errorFile; // --error-file: mitiru watch のビルドエラー帯 (空=OFF)
	if (args.fixedSize) { cfg.windowResizable = false; }   // --fixed-size: リサイズ禁止 (#44)
	if (args.headless)                   // --headless: 窓なし自動回し。vsync/CEF を切って最速で (#43)
	{
		cfg.headless  = true;
		cfg.vsync     = false;
		cfg.enableCef = false;
		cfg.deterministic = true;  // 固定 clock で run 間を決定的に (1 host frame = 1 fixed-step)
	}
	// EngineHttpServer (ADR 0011 + AI Lens ADR 0018): --http-port > 0 / --console / 環境変数
	// MITIRU_AI が立ってれば HTTP listen を開始 (127.0.0.1 限定)。MITIRU_AI は AI が zero-config で
	// /api/ai/state・/diff・/branch を叩けるようにする opt-in (port は MITIRU_AI_PORT、既定 8090)。
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
	// フォント: 既定で同梱の日本語フォント (PixelMplus) を読み、native draw
	// (drawTextInRect 等) でも日本語が出せる。かなは SDF atlas、漢字は TTF
	// 直描画 fallback なので起動は軽い。最速・純レトロ (8x8 ビットマップ ASCII)
	// で起動したい時は --font none。全漢字を SDF 化したい時は --font japanese。
	using FontAtlas = mitiru::EngineConfig::FontAtlas;
	if (args.fontMode == "none")
	{
		cfg.skipDefaultFont = true;
	}
	else
	{
		cfg.skipDefaultFont = false;
		if      (args.fontMode == "latin")    { cfg.fontAtlasRanges = FontAtlas::Latin; }
		else if (args.fontMode == "japanese") { cfg.fontAtlasRanges = FontAtlas::Japanese; }
		else                                   { cfg.fontAtlasRanges = FontAtlas::Kana; }  // 既定 (空 / "kana")

		// フォントフェイス: 既定 normal = M+ Rounded 1c (普通の丸ゴシック)、
		// retro = PixelMplus (ファミコン風ピクセル)。exe 隣の同梱フォントを解決する。
		const std::string faceFile = (args.fontFace == "retro")
			? "assets/fonts/PixelMplus12-Regular.ttf"
			: "assets/fonts/MPLUSRounded1c-Regular.ttf";
		cfg.fontPath = mitiru::resource::AssetPath::resolve(faceFile);
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
		: (!packedAppUrl.empty() ? packedAppUrl : defaultCefUrlFor(args.dllPath));

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
	// --capture-dir/--capture-every (#43): 既定を補完してディレクトリを作る。
	// 片方だけ指定でも有効化（dir 省略→"captures"、every 省略→30）。
	std::string captureDir = args.captureDir;
	int captureEvery = args.captureEvery;
	if (!captureDir.empty() && captureEvery <= 0) { captureEvery = 30; }
	if (captureEvery > 0 && captureDir.empty()) { captureDir = "captures"; }
	const bool captureOn = (captureEvery > 0 && !captureDir.empty());
	// #53: headless では capture が読むフレームだけ SW ラスタライズする (観測フレーム gating)。
	// capture 無しの自動回しは on-demand のみ (HTTP screenshot 等は 1 フレーム遅れで追従)。
	// CPU ラスタライズはピクセル数比例で重く、これを省くと --speed の早回しが実時間でも速くなる。
	if (args.headless)
	{
		cfg.swRasterizeEvery = captureOn ? captureEvery : 0;
	}
	if (captureOn)
	{
		std::error_code cec;
		std::filesystem::create_directories(captureDir, cec);
		std::fprintf(stderr, "[mitiru_host] capture: every %d frame -> %s/\n",
		             captureEvery, captureDir.c_str());
	}
	int captureFrame = 0;   // 経過フレーム数 (onFrameStart クロージャが進める)
	int captureSeq = 0;     // 保存連番
	int totalFrame = 0;     // 総フレーム数 (--max-frames 判定用)

	// --perf (#53): onFrameStart 間隔 = 1 host frame の実時間。600 フレームごとに統計を出す。
	struct PerfStats
	{
		std::vector<double> samples;                    // 当ウィンドウのフレーム時間 (ms)
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
			std::fprintf(stderr,
				"[mitiru_host] perf: %zu frames  avg %.2f ms (%.1f fps)  p50 %.2f  p95 %.2f  max %.2f\n",
				s.size(), avg, 1000.0 / avg, pct(0.50), pct(0.95), s.back());
			samples.clear();
		}
	};
	PerfStats perfStats;
	if (args.perf && cfg.vsync)
	{
		std::fprintf(stderr,
			"[mitiru_host] perf: vsync ON のため present 待ちを含みます (素の描画コストは --no-vsync 併用)\n");
	}

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

	// time-travel scrub: inspector(timetravel.html → tool_cef)が書く scrub command を
	// 毎フレーム読み、過去フレームの GameMemory へ巻き戻す (ADR 0017、click-to-scrub)。
	// reader は host 側 = rewind は host の責務 (game DLL は pure を保つ、ADR 0005)。
	mitiru::observe::ScrubControlReader scrubReader;  // 自プロセス pid 宛 (inspector が host pid に書く)
	long scrubLastSeq = 0;

	cfg.onFrameStart = [&watcher, watchOn = args.watch,
	                    captureOn, captureEvery, captureDir, &captureFrame, &captureSeq,
	                    maxFrames = args.maxFrames, &totalFrame,
	                    perfOn = args.perf, &perfStats,
	                    &scrubReader, &scrubLastSeq]
	                   (mitiru::Engine& engine)
	{
		// --perf (#53): 前回 onFrameStart からの実時間 = 1 host frame のコスト。
		if (perfOn)
		{
			const auto now = std::chrono::steady_clock::now();
			if (perfStats.hasLast)
			{
				perfStats.samples.push_back(
					std::chrono::duration<double, std::milli>(now - perfStats.last).count());
				if (perfStats.samples.size() >= 600) { perfStats.report(); }
			}
			perfStats.last = now;
			perfStats.hasLast = true;
		}

		pollHostHotkeys(engine);

		// time-travel rewind (ADR 0017): inspector の graph click → scrub command を適用。
		// 単調 seq で重複適用を防ぐ。次フレームの on_update が復元 state を「現在」として進める。
		if (auto cmd = scrubReader.poll())
		{
			const long seq = cmd->value("seq", 0L);
			if (seq > scrubLastSeq)
			{
				scrubLastSeq = seq;
				const std::size_t off = static_cast<std::size_t>(cmd->value("scrubTo", 0));
				if (const std::uint8_t* past = engine.moduleMemoryRingAt(off))
				{
					engine.rewindModuleMemory(past, engine.moduleMemorySize());
				}
			}
		}

		// --max-frames (#43): 指定フレーム数に達したら停止を要求 (headless 自動回しの終了条件)。
		if (maxFrames > 0 && ++totalFrame > maxFrames) { engine.requestStop(); }

		// --capture-every (#43): N フレームごとに直近フレームを PNG 連番で吐く。
		// onFrameStart は描画前なので「前フレームの提示結果」を保存する (AI 視覚検証には十分)。
		if (captureOn && (captureFrame++ % captureEvery == 0))
		{
			const int w = engine.captureWidth();
			const int h = engine.captureHeight();
			const auto rgba = engine.capture();
			if (w > 0 && h > 0 && !rgba.empty())
			{
				char name[32];
				std::snprintf(name, sizeof(name), "frame_%06d.png", captureSeq++);
				const std::string path = captureDir + "/" + name;
				(void)mitiru::render::savePixelsToPng(rgba.data(), w, h, path);
			}
		}

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
					"[mitiru_host] reload FAILED — 旧コードのまま継続します (状態は保持)。\n");
				const std::string why = engine.moduleLoadError();
				if (!why.empty())
				{
					std::fprintf(stderr, "  理由: %s\n", why.c_str());
				}
				std::fprintf(stderr,
					"  ビルドエラー等を修正して保存すれば自動で再試行します。\n");
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
	// headless (自動テスト / AI 回し) では音が不要かつ #52 の音声スレッド競合を避けるため
	// audio engine を作らない (sound intent は no-op、決定的 sim には無影響)。
	if (!args.headless)
	{
		const auto audioDir =
			std::filesystem::path(args.dllPath).parent_path() / "assets" / "audio";
		engine.setAudioEngine(std::make_shared<FileAudioEngine>(audioDir));
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
		// header の seed と毎フレーム snapshot の rngSeed を一致させる (ADR 0012)。
		if (!recorder.open(args.recordPath, cfg.randomSeed))
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
				// GameMemory が申告されていれば「唯一の state」を opaque にそのまま記録する
				// (ADR 0013、bit-exact diffState 用)。未申告 (v≤8) は観測 view.* JSON に
				// フォールバック。
				const std::uint32_t memSize = engine.moduleMemorySize();
				const void*         mem     = engine.moduleMemory();
				if (memSize > 0 && mem != nullptr)
				{
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
	// `--input-script` で再生でき、#43 の headless+capture と組めば完全自動回帰テストになる。
	// 既存の onModuleFrameRecorded (--record) があれば chain する。lifetime は runModule 内。
	std::ofstream inputRecOut;
	std::uint32_t inputRecFrame = 0;
	if (!args.inputRecordPath.empty())
	{
		inputRecOut.open(args.inputRecordPath, std::ios::binary);
		if (!inputRecOut)
		{
			std::fprintf(stderr, "mitiru_host: cannot open --input-record file: %s\n",
			             args.inputRecordPath.c_str());
			return 2;
		}
		inputRecOut << "# mitiru input-script (--input-record). 形式: <frame> <KEY> <down|up>\n";
		std::fprintf(stderr, "[mitiru_host] input recording → %s\n", args.inputRecordPath.c_str());
		auto prev = cfg.onModuleFrameRecorded;   // --record と併用時は chain
		cfg.onModuleFrameRecorded =
			[&inputRecOut, &inputRecFrame, prev](const mitiru::module::InputSnapshot& snap,
			                                     const mitiru::module::FrameIntents& fi)
			{
				if (prev) { prev(snap, fi); }
				for (int vk = 0; vk < 256; ++vk)
				{
					if (snap.keysJustPressed[vk])
						inputRecOut << inputRecFrame << ' ' << vkToName(vk) << " down\n";
					if (snap.keysJustReleased[vk])
						inputRecOut << inputRecFrame << ' ' << vkToName(vk) << " up\n";
				}
				++inputRecFrame;
			};
	}

	// GameMemory 再現検証 (flat POD game のみ; ADR 0013)。replay 中に on_update 後の
	// live GameMemory を記録値と byte 照合し、単一 state channel を test oracle にする。
	std::vector<std::uint8_t> recordedMem;
	std::uint32_t replayFrame     = 0;
	std::uint32_t memDivergeFrame = 0;
	bool          memDiverged     = false;
	bool          memCompared     = false;
	bool          memSizeMismatch = false;   // 録画時と GameMemory サイズが違う (struct 変更)
	std::size_t   memSizeRecorded = 0;
	std::size_t   memSizeCurrent  = 0;
	bool          frameHasRecord  = false;  // この frame に対応する記録 state を読めたか (EOF frame 除外)
	bool          loadSubstFailed = false;  // replay 中の load 代用が不能 (blob 無し録画、ADR 0020)
	std::uint32_t loadSubstFailFrame = 0;

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
		cfg.swRasterizeEvery = 0;  // 照合は GameMemory のみで pixels は読まない (#53)
		cfg.moduleInputOverride =
			[&player, &engine, &recordedMem, &frameHasRecord](mitiru::module::InputSnapshot& snap) -> bool
			{
				std::uint32_t fidx = 0;
				mitiru::module::InputSnapshot rec{};
				if (!player.readNextWithState(rec, recordedMem, fidx))  // 記録 GameMemory を退避
				{
					frameHasRecord = false;
					engine.requestStop();   // EOF → ヘッドレスループを終了
					return false;
				}
				frameHasRecord = true;
				snap = rec;
				return true;
			};
		// on_update 後の live GameMemory を退避した記録値と照合する (frame 整合済み)。
		// EOF frame は記録対応が無いので比較しない (stale な recordedMem との誤検出を防ぐ)。
		cfg.onModuleFrameRecorded =
			[&engine, &recordedMem, &replayFrame, &memDivergeFrame, &memDiverged, &memCompared,
			 &frameHasRecord, &memSizeMismatch, &memSizeRecorded, &memSizeCurrent]
			(const mitiru::module::InputSnapshot&, const mitiru::module::FrameIntents&)
			{
				if (!frameHasRecord) { return; }
				const std::uint32_t memSize = engine.moduleMemorySize();
				const void*         mem     = engine.moduleMemory();
				if (memSize > 0 && mem != nullptr &&
				    recordedMem.size() == static_cast<std::size_t>(memSize))
				{
					memCompared = true;
					if (!memDiverged && std::memcmp(mem, recordedMem.data(), memSize) != 0)
					{
						memDiverged     = true;
						memDivergeFrame = replayFrame;
					}
				}
				else if (memSize > 0 && !recordedMem.empty() &&
				         recordedMem.size() != static_cast<std::size_t>(memSize))
				{
					// サイズ不一致 = GameMemory struct が録画時から変更された。黙って
					// スキップすると false-green (検証ゼロで exit 0) になるため明示 FAIL へ。
					memSizeMismatch = true;
					memSizeRecorded = recordedMem.size();
					memSizeCurrent  = memSize;
				}
				++replayFrame;
			};
		// replay 中の load intent はファイルを読まず、当該フレームの記録済み GameMemory blob
		// で代用する (ADR 0020) — 録画後にセーブファイルが上書きされても bit-exact が保たれる。
		// blob 無し録画 (旧 .mtrr / memorySize=0) では代用不能 → 明示 FAIL (A3 と同じ思想)。
		engine.setSaveLoadOverride(
			[&engine, &recordedMem, &replayFrame, &frameHasRecord,
			 &loadSubstFailed, &loadSubstFailFrame](const char* /*slot*/) -> bool
			{
				// EOF 後のフレーム (記録対応なし) は検証対象外 — 何も適用せずスキップ。
				if (!frameHasRecord) { return true; }
				const std::uint32_t memSize = engine.moduleMemorySize();
				if (memSize == 0 ||
				    recordedMem.size() != static_cast<std::size_t>(memSize))
				{
					if (!loadSubstFailed)
					{
						loadSubstFailed    = true;
						loadSubstFailFrame = replayFrame;
					}
					engine.requestStop();
					return true;  // replay 中は失敗してもファイル load にフォールバックしない
				}
				(void)engine.rewindModuleMemory(recordedMem.data(), memSize);
				return true;
			});
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

	// ── デバッグ独立窓のオプトイン (アトミックツール哲学、ADR 0014) ──────────────
	// 「このデバッグ機能を使いたい」と host を書く人が決めた窓だけ開く。要らなければ
	// 何も書かない = 何も出ない (pulled UI)。game のキー入力とは無関係に host 側で制御。
	//
	//   例: 状態 inspector を常に開きたい host にするなら、ここに直接書く:
	//       mitiru::debug::openTool(mitiru::Tool::Inspector);
	//       mitiru::debug::openTool(mitiru::Tool::TimeTravel);
	//
	// この参照 host では CLI (--inspect <name>) で選べるようにしてある。新しいツール窓は
	// ToolRegistry.hpp の kToolTable に 1 行足せば、ここの呼び出しはそのまま使える。
	for (const mitiru::Tool t : args.openTools)
	{
		mitiru::debug::openTool(t);  // producerPid=0 → この host プロセスを監視
	}

	// --input-script (#43-1): in-process でキーを注入する。replay (--replay) 使用時は
	// そちらの moduleInputOverride が優先なので設定しない。runModule がブロックするので
	// scriptPlayer の lifetime はこのスタックフレームで足りる。
	InputScriptPlayer scriptPlayer;
	if (args.replayPath.empty() && !args.inputScript.empty())
	{
		if (loadInputScript(args.inputScript, scriptPlayer))
		{
			std::fprintf(stderr, "[mitiru_host] input-script: %zu events from %s\n",
			             scriptPlayer.events.size(), args.inputScript.c_str());
			cfg.moduleInputOverride =
				[&scriptPlayer](mitiru::module::InputSnapshot& snap) -> bool
				{
					scriptPlayer.apply(snap);   // 実キーボードを上書き (注入のみ有効)
					return true;
				};
		}
		else
		{
			std::fprintf(stderr, "mitiru_host: cannot open --input-script: %s\n",
			             args.inputScript.c_str());
		}
	}

	if (!engine.runModule(args.dllPath, cfg))
	{
		// module load 失敗 (MITIRU_GAME 入口無し等)。理由は runModule が stderr に出済み。
		// 非ゼロで返すとランチャー .bat が pause してユーザがエラーを読める。
		return 3;
	}

	// --perf: 端数ウィンドウの統計を出してから終了処理へ (#53)。
	if (args.perf) { perfStats.report(); }

	// Replay 検証: 観測可能な最終状態を出力する。--expect 指定時はキー単位で diff し、
	// 不一致があれば非ゼロ終了する (CI リグレッションゲート)。
	if (!args.replayPath.empty())
	{
		std::string finalState = "{}";
		if (auto* store = engine.moduleStateStore()) { finalState = store->snapshotJson(2); }
		std::fprintf(stdout, "%s\n", finalState.c_str());

		// replay 中の load 代用不能 (ADR 0020)。検証ゼロのまま exit 0 にしない (false-green 防止)。
		if (loadSubstFailed)
		{
			std::fprintf(stderr,
			             "replay state: FAIL — load intent at frame %u: 記録に GameMemory blob が"
			             "無く代用できません (blob 無し録画 / memorySize=0)。\n"
			             "  対処: flat POD GameMemory (memorySize 申告) のうえ --record で録り直してください。\n",
			             loadSubstFailFrame);
			return 1;
		}

		// GameMemory 再現の verdict (flat POD game のみ)。bit-exact なら軸④ 構造保証の証明。
		if (memSizeMismatch)
		{
			// 比較ゼロのまま exit 0 すると「検証されてないのに成功」に見える (false-green)。
			std::fprintf(stderr,
			             "replay state: FAIL — GameMemory size mismatch (recorded %zu bytes, "
			             "current %zu bytes)\n"
			             "  GameMemory struct が録画時から変更されています。この録画では検証できません。\n"
			             "  対処: --record で基準リプレイを録り直してください。\n",
			             memSizeRecorded, memSizeCurrent);
			return 1;
		}
		if (memCompared)
		{
			if (memDiverged)
			{
				std::fprintf(stderr, "replay state: GameMemory DIVERGED at frame %u (%u frames)\n",
				             memDivergeFrame, replayFrame);
			}
			else
			{
				std::fprintf(stderr, "replay state: GameMemory reproduced bit-exact (%u frames)\n",
				             replayFrame);
			}
		}
		if (memDiverged) { return 1; }  // 再現失敗 = CI gate fail

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
