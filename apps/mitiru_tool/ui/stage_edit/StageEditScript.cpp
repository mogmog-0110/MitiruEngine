// stage_edit の操作の台本。窓を人が触る代わりに、同じマウスとキーの経路へ命令を流す (試験と撮影用)。
// 1 行 1 命令、# から行末までは注釈。座標は世界座標で、上からの図の画面位置は図が決める。
//   wait N                    N フレーム待つ
//   select POINTER            物を選ぶ (例: boxes/0)
//   click X Z                 図の (X, Z) を押して離す
//   drag X0 Z0 X1 Z1          (X0, Z0) を押し、(X1, Z1) まで引いて離す
//   handle N X Z              選んでいる物の N 番目の取っ手を (X, Z) まで引く
//   key NAME [ctrl] [shift]   Left Right Up Down PageUp PageDown Delete Enter Escape Home F Z Y D Q E
//   add KIND X Z              図の中心を (X, Z) にして、種類 KIND の物を置く
//   field KEY COMP VALUE      選んでいる物の欄に値を入れる (COMP は成分の番号、数 1 つの欄は -1)
//   action NAME               se.try / se.keep / se.discard / se.undo などの操作

#include "StageEditPage.hpp"

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

double num(const std::vector<std::string>& w, std::size_t i)
{
	return i < w.size() ? std::atof(w[i].c_str()) : 0.0;
}

KeyEvent::Key keyNamed(const std::string& name)
{
	using K = KeyEvent::Key;
	static const std::pair<const char*, K> table[] = {
		{ "Left", K::Left }, { "Right", K::Right }, { "Up", K::Up }, { "Down", K::Down }, { "PageUp", K::PageUp },
		{ "PageDown", K::PageDown }, { "Delete", K::Delete }, { "Enter", K::Enter }, { "Escape", K::Escape }, { "Home", K::Home },
		{ "F", K::F }, { "Z", K::Z }, { "Y", K::Y }, { "D", K::D }, { "Q", K::Q }, { "E", K::E } };
	for (const auto& [n, k] : table) { if (name == n) { return k; } }
	return K::Other;
}

PointerEvent eventAt(PointerEvent::Kind kind, double sx, double sy, int w, int h)
{
	PointerEvent ev;
	ev.kind = kind;
	ev.x = static_cast<float>(sx);
	ev.y = static_cast<float>(sy);
	ev.width = static_cast<float>(w);
	ev.height = static_cast<float>(h);
	return ev;
}

} // namespace

void StageEditPage::loadScript(const std::string& path)
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

void StageEditPage::scriptStep()
{
	if (!m_loaded) { return; }
	if (m_scriptWait > 0) { --m_scriptWait; return; }
	if (m_scriptPos >= m_script.size()) { return; }
	runScriptLine(m_script[m_scriptPos++]);
}

void StageEditPage::runScriptLine(const std::string& line)
{
	using Kind = PointerEvent::Kind;
	const std::vector<std::string> w = words(line);
	const std::string& cmd = w[0];
	const int cw = m_top->width();
	const int ch = m_top->height();
	const auto screen = [&](double x, double z) { return m_top->toScreen(x, z); };
	if (cmd == "wait") { m_scriptWait = static_cast<int>(num(w, 1)); return; }
	if (cmd == "select")
	{
		for (std::size_t i = 0; i < m_doc.items().size(); ++i)
		{
			if (w.size() > 1 && m_doc.items()[i].pointer == w[1]) { m_doc.select(static_cast<int>(i)); }
		}
		changed(true);
		return;
	}
	if (cmd == "click")
	{
		const auto p = screen(num(w, 1), num(w, 2));
		onPointer("top", eventAt(Kind::Down, p.first, p.second, cw, ch));
		onPointer("top", eventAt(Kind::Up, p.first, p.second, cw, ch));
		return;
	}
	if (cmd == "drag" || cmd == "handle")
	{
		auto from = screen(num(w, 1), num(w, 2));
		auto to = screen(num(w, 3), num(w, 4));
		if (cmd == "handle")
		{
			const auto handles = m_doc.selected() >= 0 ? stageedit::handlesOf(m_doc.items()[static_cast<std::size_t>(m_doc.selected())].geom) : std::vector<stageedit::Handle>{};
			const std::size_t n = static_cast<std::size_t>(num(w, 1));
			if (n >= handles.size()) { m_status = "Script: no such handle"; return; }
			from = screen(handles[n].x, handles[n].z);
			to = screen(num(w, 2), num(w, 3));
		}
		onPointer("top", eventAt(Kind::Down, from.first, from.second, cw, ch));
		for (const double t : { 0.34, 0.67, 1.0 })
		{
			onPointer("top", eventAt(Kind::Move, from.first + (to.first - from.first) * t, from.second + (to.second - from.second) * t, cw, ch));
		}
		onPointer("top", eventAt(Kind::Up, to.first, to.second, cw, ch));
		return;
	}
	if (cmd == "key" && w.size() > 1)
	{
		KeyEvent ev;
		ev.key = keyNamed(w[1]);
		for (std::size_t i = 2; i < w.size(); ++i) { ev.ctrl = ev.ctrl || w[i] == "ctrl"; ev.shift = ev.shift || w[i] == "shift"; }
		onKey(ev);
		return;
	}
	if (cmd == "add" && w.size() > 1)
	{
		for (std::size_t i = 0; i < m_schema.kinds.size(); ++i)
		{
			if (m_schema.kinds[i].id != w[1]) { continue; }
			const double keepX = m_top->centerX();
			const double keepZ = m_top->centerZ();
			m_top->centerOn(num(w, 2), num(w, 3));
			onAction("se.add", nlohmann::json{ { "k", std::to_string(i) } });
			m_top->centerOn(keepX, keepZ);
			return;
		}
		m_status = "Script: unknown kind " + w[1];
		return;
	}
	if (cmd == "field" && w.size() > 3)
	{
		onAction("se.field", nlohmann::json{ { "key", w[1] }, { "i", w[2] }, { "v", w[3] } });
		return;
	}
	if (cmd == "action" && w.size() > 1) { onAction(w[1], nlohmann::json::object()); return; }
	if (cmd == "fit") { onAction("se.fit", nlohmann::json::object()); return; }
	m_status = "Script: unreadable line " + line;
}

} // namespace mitiru::tool
