// mitiru_tool_cef — 独立ツール窓の汎用 CEF/HTML ホスト (ideal form)。
//
// CEF が assets/<page>.html を全面表示し、C++ は SharedSnapshot 全体を毎フレーム
// window.applySnapshot(json) で JS に push するだけ。各ページは自分の関心事だけ描く。
// → 新しいツール窓を足す = HTML を 1 枚書くだけ (C++ 改変ゼロ)。
//
// 使い方:  mitiru_tool_cef --page perf <pid>            動作中ゲームの snapshot を pid で
//          mitiru_tool_cef --page mixer --file <path>   特定の snapshot JSON を直接
//
// page は assets/<page>.html に対応 (perf / mixer / scene / inspect …)。

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/Mitiru.hpp>
#include <mitiru/debug/ToolWindowApp.hpp>      // parseToolArgs / kToolBg
#include <mitiru/observe/ScrubControlChannel.hpp>  // time-travel click-to-scrub (ADR 0017)
#include <mitiru/observe/SharedSnapshot.hpp>
#include <mitiru/replay/Player.hpp>            // .mtrr 読み込み (replay mode)
#include <mitiru/module/ModuleApi.hpp>         // InputSnapshot

namespace
{
// vk コード → 短い表示名 (replay の held-key 表示用、よく使う分だけ)。
inline const char* vkName(int vk)
{
	switch (vk)
	{
		case 0x25: return "Left";  case 0x27: return "Right";
		case 0x26: return "Up";    case 0x28: return "Down";
		case 0x20: return "Space"; case 0x0D: return "Enter"; case 0x1B: return "Esc";
		case 0x10: return "Shift"; case 0x11: return "Ctrl";
	}
	if (vk >= 'A' && vk <= 'Z') { static thread_local char b[2]; b[0]=static_cast<char>(vk); b[1]=0; return b; }
	if (vk >= '0' && vk <= '9') { static thread_local char b[2]; b[0]=static_cast<char>(vk); b[1]=0; return b; }
	return nullptr;
}
}  // namespace

namespace
{

/// CEF 全面 + SharedSnapshot 全体を JS に push するだけの汎用 Game。
class ToolCef final : public mitiru::Game
{
public:
	ToolCef(std::optional<int> pid, std::optional<std::string> file,
	        std::optional<std::string> mtrr)
		: m_pid(pid)
	{
		if (file) { m_filePath = std::filesystem::path(*file); }
		if (mtrr) { m_mtrrPath = *mtrr; }
	}

	void update(float dt) override
	{
		// time-travel ページの click-to-scrub 逆チャネルを (CEF 準備でき次第) 1 回配線する。
		ensureScrubHandler();

		// replay モード (.mtrr): .mtrr は 1 回だけ load して JSON をキャッシュし、
		// 毎 tick その文字列を push (CEF ページのロード完了タイミングに依存しないため)。
		// JS 側は二重初期化を弾くので user の scrub 位置は保たれる。
		if (m_mtrrPath)
		{
			m_pollAccum += dt;
			if (m_pollAccum < 1.0f / 30.0f) { return; }
			m_pollAccum = 0.0f;
			pushReplay();
			return;
		}
		m_pollAccum += dt;
		if (m_pollAccum < 1.0f / 30.0f) { return; }
		m_pollAccum = 0.0f;

		if (auto snap = readSnapshot())
		{
			m_last = std::move(*snap);
			m_everRead = true;
		}
		pushToJs();
	}

	void draw(mitiru::Screen& screen) override
	{
		screen.clear(mitiru::debug::kToolBg);   // CEF ロード前の一瞬の保険
	}

	[[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
	{
		return {outsideW, outsideH};
	}

private:
	std::optional<nlohmann::json> readSnapshot()
	{
		try
		{
			if (m_filePath)
			{
				if (!std::filesystem::exists(*m_filePath)) { return std::nullopt; }
				const auto mt = std::filesystem::last_write_time(*m_filePath);
				if (m_haveMtime && mt == m_lastMtime) { return std::nullopt; }
				std::ifstream in(*m_filePath, std::ios::binary);
				if (!in) { return std::nullopt; }
				auto j = nlohmann::json::parse(in);
				m_lastMtime = mt; m_haveMtime = true;
				return j;
			}
			if (!m_reader && m_pid) { m_reader.emplace(*m_pid); }
			if (m_reader) { return m_reader->tryRead(); }
		}
		catch (...) { /* truncate race: 次 tick で retry */ }
		return std::nullopt;
	}

	void pushToJs()
	{
		auto* eng = engine();
		if (!eng) { return; }
		auto* cef = eng->cefContext();
		if (!cef) { return; }

		nlohmann::json env;
		env["ready"] = m_everRead;
		env["snap"]  = m_last;
		cef->executeJavaScript(
			"window.applySnapshot && window.applySnapshot(" + env.dump() + ");");
	}

	// time-travel: timetravel.html の graph click → window.cefQuery("timetravel.scrub|<offset>")
	// を受けて、監視中ゲーム(host pid)宛に scrub command を書く (ADR 0017、click-to-scrub)。
	// host が ScrubControlReader で読み、GameMemory を過去 bytes へ巻き戻す。1 度だけ登録する。
	void ensureScrubHandler()
	{
		if (m_scrubHandlerRegistered || !m_pid) { return; }
		auto* eng = engine();
		if (!eng) { return; }
		auto* cef = eng->cefContext();
		if (!cef || !cef->isInitialized()) { return; }  // CEF 準備待ち

		if (!m_scrubWriter) { m_scrubWriter.emplace(*m_pid); }  // host pid 宛 writer
		cef->registerHandler("timetravel.scrub",
			[this](std::string_view payload) -> std::string
			{
				// payload = offsetFromNewest (0 = 最新)。
				int off = 0;
				try { off = std::stoi(std::string{payload}); }
				catch (...) { return "{}"; }
				if (off < 0) { off = 0; }
				if (m_scrubWriter)
				{
					m_scrubWriter->write({{"scrubTo", off}, {"seq", ++m_scrubSeq}});
				}
				return "{}";
			});
		m_scrubHandlerRegistered = true;
	}

	void pushReplay()
	{
		auto* eng = engine();
		if (!eng) { return; }
		auto* cef = eng->cefContext();
		if (!cef) { return; }  // CEF 未初期化: 次 tick で retry

		// 初回のみ .mtrr を load して JSON 文字列をキャッシュ。
		if (!m_replayPushed) { m_replayJson = buildReplayJson(); m_replayPushed = true; }
		cef->executeJavaScript("window.applyReplay && window.applyReplay(" + m_replayJson + ");");
	}

	std::string buildReplayJson()
	{
		nlohmann::json out;
		out["file"] = std::filesystem::path(*m_mtrrPath).filename().string();
		nlohmann::json frames = nlohmann::json::array();

		mitiru::replay::Player player;
		if (player.open(*m_mtrrPath))
		{
			mitiru::module::InputSnapshot snap{};
			std::uint32_t idx = 0;
			while (player.readNext(snap, idx))
			{
				nlohmann::json f;
				f["i"] = idx;
				nlohmann::json keys = nlohmann::json::array();
				for (int vk = 0; vk < 256; ++vk)
				{
					if (snap.keysDown[vk]) { if (const char* n = vkName(vk)) { keys.push_back(n); } }
				}
				f["k"]  = keys;
				f["mx"] = static_cast<int>(snap.mouseX);
				f["my"] = static_cast<int>(snap.mouseY);
				std::string mb;
				if (snap.mouseButtonsDown[0]) { mb += "L"; }
				if (snap.mouseButtonsDown[1]) { mb += "R"; }
				if (snap.mouseButtonsDown[2]) { mb += "M"; }
				f["mb"]  = mb;
				f["pad"] = snap.gamepadConnected;
				frames.push_back(std::move(f));
			}
			out["error"] = (player.lastError() == mitiru::replay::PlayerError::None ||
			                player.lastError() == mitiru::replay::PlayerError::FrameTruncated)
			               ? "" : errorText(player.lastError());
		}
		else
		{
			out["error"] = errorText(player.lastError());
		}
		out["frames"] = std::move(frames);
		return out.dump();
	}

	static const char* errorText(mitiru::replay::PlayerError e)
	{
		using E = mitiru::replay::PlayerError;
		switch (e)
		{
			case E::FileNotOpen:       return "file not found / unreadable";
			case E::HeaderTooShort:    return "not a .mtrr (header too short)";
			case E::MagicMismatch:     return "not a .mtrr (bad magic)";
			case E::VersionMismatch:   return "incompatible recording (version mismatch)";
			case E::FrameSizeMismatch: return "incompatible recording (InputSnapshot size changed)";
			case E::ChecksumMismatch:  return "corrupt recording (checksum mismatch)";
			default:                   return "";
		}
	}

	std::optional<int>                                     m_pid;
	std::optional<std::filesystem::path>                   m_filePath;
	std::optional<std::string>                             m_mtrrPath;
	bool                                                   m_replayPushed{false};
	std::string                                            m_replayJson{"{}"};
	std::optional<mitiru::observe::SharedSnapshot::Reader> m_reader;
	nlohmann::json                                         m_last = nlohmann::json::object();
	std::filesystem::file_time_type                        m_lastMtime{};
	bool                                                   m_haveMtime{false};
	bool                                                   m_everRead{false};
	float                                                  m_pollAccum{0.0f};

	// time-travel click-to-scrub (ADR 0017)
	std::optional<mitiru::observe::ScrubControlWriter>     m_scrubWriter;
	long                                                   m_scrubSeq{0};
	bool                                                   m_scrubHandlerRegistered{false};
};

}  // namespace

int main(int argc, char* argv[])
{
	// --page <name> を抜き取り、残りを共通 arg parser (pid / --file) に渡す。
	std::string page = "perf";
	std::optional<std::string> mtrr;
	std::vector<char*> rest;
	rest.push_back(argv[0]);
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--page" && i + 1 < argc) { page = argv[++i]; }
		else if (a == "--mtrr" && i + 1 < argc) { mtrr = argv[++i]; }
		else { rest.push_back(argv[i]); }
	}
	// replay (.mtrr) は file/pid 不要なので parse をスキップして良い。
	mitiru::debug::ToolArgs args;
	if (mtrr) { args.ok = true; }
	else
	{
		args = mitiru::debug::parseToolArgs(static_cast<int>(rest.size()), rest.data());
		if (!args.ok) { return 2; }
	}

	// cwd を exe ディレクトリへ固定 (相対 URL / CEF helper 検索)。
	std::error_code ec;
	const auto exeDir = std::filesystem::weakly_canonical(
		std::filesystem::path(argv[0]), ec).parent_path();
	if (!exeDir.empty()) { std::filesystem::current_path(exeDir, ec); }

	const std::string url =
		"file:///" + (exeDir / "assets" / (page + ".html")).generic_string();

	ToolCef tool(args.pid, args.file, mtrr);

	const std::string title = "MitiruEngine — " + page;
	mitiru::Engine engine;
	mitiru::EngineConfig cfg;
	cfg.title           = title.c_str();   // run() 中 生存。
	cfg.windowWidth     = 400;
	cfg.windowHeight    = 620;
	cfg.minWindowWidth  = 300;
	cfg.minWindowHeight = 360;
	cfg.vsync           = true;
	cfg.enableCef       = true;
	cfg.cefStartUrl     = url;
	// HTML 側 (tool.css) の地色 Win95 グレーに合わせる。letterbox 余白も
	// この色で clear されるので端の黒帯が出ない。
	cfg.backgroundColor = sgc::Colorf{0.753f, 0.749f, 0.741f, 1.0f};
	cfg.useLogicalWindowSize = true;
	engine.run(tool, cfg);
	return 0;
}
