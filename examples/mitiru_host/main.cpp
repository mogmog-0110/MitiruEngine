// mitiru_host — minimal engine launcher for game-as-DLL modules (v0.2.0 step 3-4)
//
// Argv:
//   argv[0]  = host exe path
//   argv[1]  = game DLL path (relative to cwd or absolute)
//   argv[2+] = optional flags:
//                --watch         poll the DLL file mtime, reload on change
//                --url <url>     override CEF start URL (default: file:///./<dll_dir>/assets/scene.html)
//
// Per ADR 0005 the host is the only piece that ever sees the engine
// directly — game code lives entirely inside the DLL and communicates with
// the engine through the C-only signal flow defined in ModuleApi.hpp.
//
// `--watch` is the L3 hot reload mode (handoff north-star #3): file mtime is
// polled every ~250ms; when it ticks, the engine swaps the DLL while keeping
// HelloGameMemory* alive across the reload (state preserved).
//
// Future: `mitiru-cli run [--watch]` will resolve project/module.toml to
// build equivalent argv automatically.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <mitiru/Mitiru.hpp>

namespace
{

/// Anchor the process cwd to the directory containing argv[0] so relative
/// paths in EngineConfig (cefStartUrl, etc.) resolve regardless of which
/// shell launched the host.
void anchorCwdToExeDir(const char* argv0)
{
	if (argv0 == nullptr) { return; }
	std::error_code ec;
	const auto canon = std::filesystem::weakly_canonical(
		std::filesystem::path(argv0), ec);
	if (ec) { return; }
	std::filesystem::current_path(canon.parent_path(), ec);
}

/// Build "file:///./<dll_dir>/assets/scene.html" from the DLL path so a
/// game that puts its scene.html next to its DLL works out of the box.
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
	int                   widthOverride  = 0;  // 0 = default 1280
	int                   heightOverride = 0;  // 0 = default 720
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
		else if (a == "--size")
		{
			// Format: WxH e.g. "800x500". Both must be positive ints.
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
		// Trailing positional after the DLL = legacy URL slot.
		else if (out.cefUrlOverride.empty())
		{
			out.cefUrlOverride = a;
		}
	}

	// MITIRU_WATCH=1 env var = same as --watch.
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

/// File watcher state — captured into the onFrameStart closure.
struct WatcherState
{
	std::filesystem::path           dllPath;
	std::filesystem::file_time_type lastMtime{};
	int                             pollTick   = 0;
	int                             pollEvery  = 15;       // frames; ~250ms @60fps
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
	// Mitiru Saturn canonical bg — silver gray (#c8c8c8). All
	// engine-shipped surfaces (hello_game / launcher / companion) render
	// their HUD on this silver base for unified Saturn identity.
	// Games that want a different bg can override per-instance.
	cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};
	cfg.cefStartUrl     = !args.cefUrlOverride.empty()
		? args.cefUrlOverride
		: defaultCefUrlFor(args.dllPath);

	// MITIRU_AUTOTEST_FRAMES allows extending the autotest window so CEF
	// has enough time to load scene.html before the screenshot fires.
	// applyAutoTestEnv() in Engine::run uses default 120 (~2s); on cold CEF
	// boot scene.html may not have rendered yet. Bump to 600 (~10s) for
	// smoke tests that need to verify the HUD overlay.
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

	// File watcher → DLL hot reload (L3 of the engine UX north star).
	// The captured WatcherState lives in this stack frame; lifetime is fine
	// because Engine::runModule blocks until the loop exits.
	WatcherState watcher;
	if (args.watch)
	{
		watcher.dllPath   = args.dllPath;
		// Resolve absolute path so post-cwd-change reload still finds the file.
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
	engine.runModule(args.dllPath, cfg);
	return 0;
}
