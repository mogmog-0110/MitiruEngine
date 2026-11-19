// 人が触る代わりに、同じマウスとキーの経路へ命令を流す (試験と撮影用)。命令の一覧は docs/TILEMAP_EDIT.md

#include "TilemapEditPage.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mitiru::tool
{

namespace
{

std::vector<std::string> words(const std::string& line)
{
	std::vector<std::string> out;
	std::istringstream in(line);
	for (std::string w; in >> w;) { out.push_back(w); }
	return out;
}

int num(const std::vector<std::string>& w, std::size_t i)
{
	return i < w.size() ? std::atoi(w[i].c_str()) : 0;
}

KeyEvent::Key keyNamed(const std::string& name)
{
	using K = KeyEvent::Key;
	static const std::pair<const char*, K> table[] = {
		{ "C", K::C }, { "V", K::V }, { "H", K::H }, { "Z", K::Z }, { "Y", K::Y }, { "F", K::F },
		{ "Delete", K::Delete }, { "Escape", K::Escape }, { "Home", K::Home } };
	for (const auto& [n, k] : table) { if (name == n) { return k; } }
	return K::Other;
}

PointerEvent eventAt(PointerEvent::Kind kind, std::pair<double, double> at, int button, int w, int h)
{
	PointerEvent ev;
	ev.kind = kind;
	ev.button = button;
	ev.x = static_cast<float>(at.first);
	ev.y = static_cast<float>(at.second);
	ev.width = static_cast<float>(w);
	ev.height = static_cast<float>(h);
	return ev;
}

} // namespace

void TilemapEditPage::loadScript(const std::string& path)
{
	std::ifstream in(path);
	if (!in) { m_status = "Can't read the script: " + path; return; }
	m_script.clear();
	m_scriptPos = 0;
	for (std::string line; std::getline(in, line);)
	{
		const std::size_t hash = line.find('#');
		if (hash != std::string::npos) { line.erase(hash); }
		if (!words(line).empty()) { m_script.push_back(line); }
	}
}

void TilemapEditPage::scriptStep()
{
	if (!m_loaded) { return; }
	if (m_scriptWait > 0) { --m_scriptWait; return; }
	if (m_scriptPos >= m_script.size()) { return; }
	runScriptLine(m_script[m_scriptPos++]);
}

void TilemapEditPage::runScriptLine(const std::string& line)
{
	using Kind = PointerEvent::Kind;
	const std::vector<std::string> w = words(line);
	const std::string& cmd = w[0];
	const int vw = m_viewport->width();
	const int vh = m_viewport->height();
	const auto at = [&](std::size_t i) { return m_viewport->centerOf(num(w, i), num(w, i + 1)); };
	if (cmd == "wait") { m_scriptWait = num(w, 1); return; }
	if (cmd == "layer") { chooseLayer(num(w, 1)); return; }
	if (cmd == "tool") { onAction("te.tool", { { "m", w.size() > 1 ? w[1] : "paint" } }); return; }
	if (cmd == "tile")
	{
		m_brush = tilemapedit::Cell{ num(w, 1), 0.0f, false, false };
		changed(true);
		return;
	}
	if (cmd == "fit") { onAction("te.fit", nlohmann::json::object()); return; }
	if (cmd == "click" || cmd == "rclick")
	{
		const int button = cmd == "rclick" ? 1 : 0;
		onPointer("map", eventAt(Kind::Down, at(1), button, vw, vh));
		onPointer("map", eventAt(Kind::Up, at(1), button, vw, vh));
		return;
	}
	if (cmd == "drag")
	{
		onPointer("map", eventAt(Kind::Down, at(1), 0, vw, vh));
		onPointer("map", eventAt(Kind::Move, at(3), 0, vw, vh));
		onPointer("map", eventAt(Kind::Up, at(3), 0, vw, vh));
		return;
	}
	if (cmd == "key")
	{
		KeyEvent ev;
		ev.key = keyNamed(w.size() > 1 ? w[1] : "");
		for (std::size_t i = 2; i < w.size(); ++i) { ev.ctrl = ev.ctrl || w[i] == "ctrl"; ev.shift = ev.shift || w[i] == "shift"; }
		onKey(ev);
		return;
	}
	if (cmd == "action") { onAction(w.size() > 1 ? w[1] : "", nlohmann::json::object()); return; }
	m_status = "Script: unknown command " + cmd;
}

} // namespace mitiru::tool
