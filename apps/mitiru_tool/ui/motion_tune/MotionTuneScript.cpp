// motion_tune の操作スクリプトの実装。画面操作と同じ入口の onAction に命令を送り、テストと撮影に使う。
// 1 行 1 命令。# から行末までは注釈。
//   wait N              N フレーム待つ
//   waitidle [N]        ゲームへの要求の返事が揃うまで待つ (最長 N フレーム、既定 600)
//   tab N               タブを開く (0 は「よく使う」、1 以降はスキーマのファイルの順)
//   filter TEXT         欄を絞る
//   set NAME VALUE      NAME の欄 (GameMemory の名前) に値を入れる
//   action NAME         mt.try / mt.keep / mt.discard / mt.trace

#include "MotionTunePage.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mitiru::tool
{

namespace
{

constexpr int kDefaultIdleWait = 600;

std::vector<std::string> words(const std::string& line)
{
	std::vector<std::string> out;
	std::istringstream in(line);
	for (std::string w; in >> w;) { out.push_back(w); }
	return out;
}

} // namespace

void MotionTunePage::loadScript(const std::string& path)
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

void MotionTunePage::scriptStep()
{
	if (!m_loaded) { return; }
	if (m_scriptWait > 0) { --m_scriptWait; return; }
	if (m_scriptWait < 0)
	{
		// 負の値は waitidle の残りフレーム数。返事が揃うか、残りが 0 になると次へ進む。
		if (!(idle() && m_connected) && ++m_scriptWait < 0) { return; }
		m_scriptWait = 0;
	}
	if (m_scriptPos >= m_script.size()) { return; }
	runScriptLine(m_script[m_scriptPos++]);
}

void MotionTunePage::runScriptLine(const std::string& line)
{
	const std::vector<std::string> w = words(line);
	if (w.empty()) { return; }
	if (w[0] == "wait" && w.size() > 1) { m_scriptWait = std::atoi(w[1].c_str()); }
	else if (w[0] == "waitidle") { m_scriptWait = -(w.size() > 1 ? std::atoi(w[1].c_str()) : kDefaultIdleWait); }
	else if (w[0] == "tab" && w.size() > 1) { onAction("mt.tab", { { "i", w[1] } }); }
	else if (w[0] == "filter") { onAction("mt.filter", { { "v", w.size() > 1 ? w[1] : std::string() } }); }
	else if (w[0] == "set" && w.size() > 2) { onAction("mt.set", { { "n", w[1] }, { "v", w[2] } }); }
	else if (w[0] == "action" && w.size() > 1) { onAction(w[1], nlohmann::json::object()); }
	else { m_status = "Unreadable script line: " + line; }
}

} // namespace mitiru::tool
