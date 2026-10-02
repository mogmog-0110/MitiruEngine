// replay: .mtrr 録画をフレーム単位で見る (読み取り専用)。録画は最初に 1 回だけ読み、スクラブの位置は
// この窓の中だけで持つ。←/→ で 1 フレーム、Home/End で端、バーのクリックでその位置へ。

#include "Pages.hpp"

#include "../PageUtil.hpp"

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/replay/Player.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace mitiru::tool
{

namespace
{

struct ReplayFrame
{
	std::uint32_t index = 0;
	std::vector<std::string> keys;
	int mouseX = 0;
	int mouseY = 0;
	std::string mouseButtons;
	bool pad = false;
	std::vector<std::string> padButtons;
	float axes[6] = {};
};

// よく使うキーだけ短い名前にする (残りは出さない)。
std::string vkName(int vk)
{
	switch (vk)
	{
	case 0x25: return "Left";  case 0x27: return "Right";
	case 0x26: return "Up";    case 0x28: return "Down";
	case 0x20: return "Space"; case 0x0D: return "Enter"; case 0x1B: return "Esc";
	case 0x10: return "Shift"; case 0x11: return "Ctrl";
	default: break;
	}
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { return std::string(1, static_cast<char>(vk)); }
	return {};
}

std::vector<std::string> padButtonNames(std::uint32_t down)
{
	using namespace mitiru::module::gamepad;
	static const std::pair<std::uint32_t, const char*> kNames[] = {
		{ DPadUp, "DUp" }, { DPadDown, "DDown" }, { DPadLeft, "DLeft" }, { DPadRight, "DRight" },
		{ Start, "Start" }, { Back, "Back" }, { LS, "LS" }, { RS, "RS" }, { LB, "LB" }, { RB, "RB" },
		{ A, "A" }, { B, "B" }, { X, "X" }, { Y, "Y" },
	};
	std::vector<std::string> out;
	for (const auto& [bit, name] : kNames) { if ((down & bit) != 0) { out.emplace_back(name); } }
	return out;
}

ReplayFrame frameFrom(const module::InputSnapshot& snap, std::uint32_t index)
{
	ReplayFrame f;
	f.index = index;
	for (int vk = 0; vk < 256; ++vk)
	{
		if (snap.keysDown[vk] == 0) { continue; }
		if (std::string name = vkName(vk); !name.empty()) { f.keys.push_back(std::move(name)); }
	}
	f.mouseX = static_cast<int>(snap.mouseX);
	f.mouseY = static_cast<int>(snap.mouseY);
	const char* buttonNames[3] = { "L", "R", "M" };
	for (int b = 0; b < 3; ++b) { if (snap.mouseButtonsDown[b] != 0) { f.mouseButtons += buttonNames[b]; } }
	f.pad = snap.gamepadConnected != 0;
	f.padButtons = padButtonNames(snap.gamepadButtonsDown);
	for (int a = 0; a < 6; ++a) { f.axes[a] = snap.gamepadAxes[a]; }
	return f;
}

const char* errorText(replay::PlayerError e)
{
	using E = replay::PlayerError;
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

class ReplayPage final : public ToolPage
{
public:
	explicit ReplayPage(const PageContext& ctx) : m_view(ctx.view), m_path(ctx.mtrrPath.value_or("")) {}

	void start() override
	{
		if (!m_loaded) { load(); }
		publish();
	}

	void onKey(const KeyEvent& ev) override
	{
		if (m_frames.empty()) { return; }
		switch (ev.key)
		{
		case KeyEvent::Key::Right: ++m_cur; break;
		case KeyEvent::Key::Left:  --m_cur; break;
		case KeyEvent::Key::Home:  m_cur = 0; break;
		case KeyEvent::Key::End:   m_cur = static_cast<int>(m_frames.size()) - 1; break;
		default: return;
		}
		publish();
	}

	void onPointer(std::string_view, const PointerEvent& ev) override
	{
		if (m_frames.empty() || ev.kind != PointerEvent::Kind::Down) { return; }
		m_cur = static_cast<int>(std::lround(ev.x / std::max(ev.width, 1.0f) * static_cast<float>(m_frames.size() - 1)));
		publish();
	}

	[[nodiscard]] std::vector<std::string> pointerTargets() const override { return { "scrub" }; }
	[[nodiscard]] bool wantsSnapshot() const override { return false; }

private:
	void load()
	{
		m_loaded = true;
		m_file = std::filesystem::path(m_path).filename().string();
		if (m_path.empty()) { m_error = "no recording (--mtrr <file.mtrr>)"; return; }
		replay::Player player;
		if (!player.open(m_path)) { m_error = errorText(player.lastError()); return; }
		module::InputSnapshot snap{};
		std::uint32_t index = 0;
		while (player.readNext(snap, index)) { m_frames.push_back(frameFrom(snap, index)); }
		const auto e = player.lastError();
		if (e != replay::PlayerError::None && e != replay::PlayerError::FrameTruncated) { m_error = errorText(e); }
		if (m_error.empty() && m_frames.empty()) { m_error = "empty recording (0 frames)"; }
	}

	// 入力の多さの帯 (押していたキーの数で高さ、最大 200 本)。
	nlohmann::json strip() const
	{
		nlohmann::json out = nlohmann::json::array();
		const std::size_t n = std::min<std::size_t>(m_frames.size(), 200);
		const double step = static_cast<double>(m_frames.size()) / static_cast<double>(std::max<std::size_t>(n, 1));
		for (std::size_t i = 0; i < n; ++i)
		{
			const auto& fr = m_frames[static_cast<std::size_t>(std::floor(static_cast<double>(i) * step))];
			out.push_back({ { "h", std::min<std::size_t>(100, fr.keys.size() * 30 + 8) } });
		}
		return out;
	}

	void publish()
	{
		const bool ok = m_loaded && m_error.empty() && !m_frames.empty();
		m_view->set("loaded", m_loaded);
		m_view->set("file", m_file);
		m_view->set("error", m_error);
		m_view->set("ok", ok);
		if (!ok)
		{
			m_view->set("pct", 0.0);   // 隠れているスクラブ位置の式も値が無いと RCSS の構文エラーになる
			return;
		}
		m_cur = std::clamp(m_cur, 0, static_cast<int>(m_frames.size()) - 1);
		const ReplayFrame& f = m_frames[static_cast<std::size_t>(m_cur)];
		const auto axis = [&f](int a) { return toFixed(f.axes[a], 2); };
		m_view->set("total", m_frames.size());
		m_view->set("cur", m_cur + 1);
		m_view->set("pct", m_frames.size() > 1 ? static_cast<double>(m_cur) / static_cast<double>(m_frames.size() - 1) * 100.0 : 0.0);
		m_view->set("strip", strip());
		m_view->set("keys", f.keys);
		m_view->set("frame", { { "i", f.index }, { "mouse", "(" + std::to_string(f.mouseX) + ", " + std::to_string(f.mouseY) + ")" },
		                       { "mb", f.mouseButtons }, { "pad", f.pad } });
		m_view->set("pad_buttons", f.padButtons);
		m_view->set("stick_l", "(" + axis(0) + ", " + axis(1) + ")");
		m_view->set("stick_r", "(" + axis(2) + ", " + axis(3) + ")");
		m_view->set("trigger", "L " + axis(4) + " \xC2\xB7 R " + axis(5));
	}

	ToolView* m_view;
	std::string m_path;
	std::string m_file;
	std::string m_error;
	std::vector<ReplayFrame> m_frames;
	int m_cur = 0;
	bool m_loaded = false;
};

} // namespace

std::unique_ptr<ToolPage> makeReplayPage(const PageContext& ctx)
{
	return std::make_unique<ReplayPage>(ctx);
}

} // namespace mitiru::tool
