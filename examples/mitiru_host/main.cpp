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
// 将来: `mitiru-cli run [--watch]` が project/module.toml を解決して
// 等価な argv を自動構築する。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <mitiru/Mitiru.hpp>
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/replay/Player.hpp>
#include <mitiru/replay/Recorder.hpp>

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

	void playSound(std::string_view id) override { playById(id, 1.0f, /*music=*/false, false); }
	void playSound(std::string_view id, float vol) override { playById(id, vol, false, false); }
	void stopSound(std::string_view) override {}      // SE は撃ちっぱなし (id 別 stop は未対応)
	void playMusic(std::string_view id) override { playById(id, 1.0f, /*music=*/true, true); }
	void playMusic(std::string_view id, float vol, bool loop) override { playById(id, vol, true, loop); }
	void stopMusic() override { m_engine.stopMusic(); }
	void setVolume(float v) override { m_engine.setMasterVolume(v); }
	[[nodiscard]] bool isPlaying(std::string_view) const override { return false; }

private:
	void playById(std::string_view id, float volume, bool music, bool loop)
	{
		for (const char* ext : {".wav", ".ogg", ".mp3"})
		{
			const auto p = m_baseDir / (std::string(id) + ext);
			if (std::filesystem::exists(p))
			{
				if (music) { m_engine.playMusic(p.string(), volume, loop); }
				else       { m_engine.playSoundVolume(p.string(), volume); }
				return;
			}
		}
		std::fprintf(stderr, "[mitiru_host] sound id not found under %s: %.*s\n",
		             m_baseDir.string().c_str(),
		             static_cast<int>(id.size()), id.data());
	}

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
		"  --help, -h       this message\n"
		"\n"
		"environment:\n"
		"  MITIRU_WATCH=1   same as --watch\n"
		"\n"
		"The host loads the game DLL via Engine::loadModule and drives the\n"
		"main loop. The DLL must export mitiru_module_load (see\n"
		"docs/adr/0005-host-game-c-abi-signal-flow.md).\n");
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
	cfg.skipDefaultFont = true;
	// Mitiru Saturn 標準背景 — シルバーグレー (#c8c8c8)。エンジン同梱の全 surface
	// (hello_game / launcher / companion) はこのシルバー地に HUD を描き、Saturn の
	// 統一感を出す。別の背景が欲しい game はインスタンス単位で上書きできる。
	cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};
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

	// ファイル監視 → DLL ホットリロード (エンジン UX 北極星の L3)。
	// キャプチャした WatcherState はこのスタックフレームに置く。Engine::runModule が
	// ループ終了までブロックするので lifetime は問題ない。
	WatcherState watcher;
	if (args.watch)
	{
		watcher.dllPath   = args.dllPath;
		// 絶対パスに解決し、cwd 変更後のリロードでもファイルを見つけられるようにする。
		std::error_code rc;
		auto abs = std::filesystem::absolute(args.dllPath, rc);
		if (!rc) { watcher.dllPath = abs; }

		std::fprintf(stderr, "[mitiru_host] watch mode: polling %s\n",
		             watcher.dllPath.string().c_str());

		cfg.onFrameStart = [&watcher](mitiru::Engine& engine)
		{
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
	}

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
