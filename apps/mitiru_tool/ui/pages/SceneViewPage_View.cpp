// scene_view の表示の値づくり。枠と矩形選択は絵に対する割合 (%) で渡すので、窓の大きさが変わっても
// RCSS の側で絵と一緒に伸び縮みする。

#include "SceneViewPage.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool
{

namespace
{

double percent(double value, int whole)
{
	return whole > 0 ? value / static_cast<double>(whole) * 100.0 : 0.0;
}

std::string boxLabel(const SceneObject& o)
{
	if (!o.field.empty()) { return o.field; }
	if (!o.fieldX.empty() && !o.fieldY.empty()) { return o.fieldX + "/" + o.fieldY; }
	return {};
}

} // namespace

// key (dotted path) が分岐中の field そのものか、その配下か。
bool SceneViewPage::isBranchDirty(const std::string& key) const
{
	const Snapshot* fields = findAt(m_branch, { "fields" });
	if (fields == nullptr || !fields->is_array()) { return false; }
	return std::any_of(fields->begin(), fields->end(), [&key](const Snapshot& f) {
		if (!f.is_string()) { return false; }
		const std::string& name = f.get_ref<const std::string&>();
		return key == name || key.rfind(name + ".", 0) == 0;
	});
}

nlohmann::json SceneViewPage::boxes() const
{
	nlohmann::json out = nlohmann::json::array();
	if (m_spaceW <= 0 || m_spaceH <= 0) { return out; }
	for (const SceneObject& o : m_editor.objects())
	{
		const std::string label = boxLabel(o);
		out.push_back({ { "left", percent(o.x, m_spaceW) }, { "top", percent(o.y, m_spaceH) },
		                { "width", percent(o.w, m_spaceW) }, { "height", percent(o.h, m_spaceH) },
		                { "sel", m_editor.isSelected(o.id) }, { "label", label }, { "has_label", !label.empty() },
		                // 枠が絵の上端に近いと名札が絵の外へ出るので、枠の内側へ入れる
		                { "label_inside", o.y < 12.0 } });
	}
	return out;
}

nlohmann::json SceneViewPage::marquee() const
{
	const auto& m = m_editor.marquee();
	if (!m || m_spaceW <= 0) { return { { "show", false }, { "left", 0 }, { "top", 0 }, { "width", 0 }, { "height", 0 } }; }
	return { { "show", true },
	         { "left", percent(std::min(m->x0, m->x1), m_spaceW) }, { "top", percent(std::min(m->y0, m->y1), m_spaceH) },
	         { "width", percent(std::fabs(m->x1 - m->x0), m_spaceW) }, { "height", percent(std::fabs(m->y1 - m->y0), m_spaceH) } };
}

nlohmann::json SceneViewPage::inspectorRows() const
{
	nlohmann::json rows = nlohmann::json::array();
	const SceneObject* sel = m_editor.primary();
	if (sel == nullptr || (sel->field.empty() && sel->fieldX.empty())) { return rows; }
	const Snapshot& gm = m_editor.gameMemory();
	std::vector<std::string> keys;
	for (auto it = gm.begin(); it != gm.end(); ++it)
	{
		const std::string& k = it.key();
		const bool wanted = !sel->field.empty()
			? (k == sel->field || k.rfind(sel->field + ".", 0) == 0)
			: (k == sel->fieldX || k == sel->fieldY);
		if (wanted) { keys.push_back(k); }
	}
	std::sort(keys.begin(), keys.end());
	for (const std::string& k : keys)
	{
		const Snapshot& v = gm[k];
		rows.push_back({ { "k", k }, { "v", v.dump() }, { "dirty", isBranchDirty(k) }, { "numeric", v.is_number() } });
	}
	return rows;
}

nlohmann::json SceneViewPage::candidateChips() const
{
	nlohmann::json chips = nlohmann::json::array();
	for (std::size_t slot = 0; slot < m_candidates.size(); ++slot)
	{
		const double rounded = std::round(m_candidates[slot] * 1000.0) / 1000.0;
		std::string label = m_candidateField + " = " + jsString(Snapshot(rounded));
		if (!m_baseFrame.empty()) { label += " (frame " + m_baseFrame + " から)"; }
		chips.push_back({ { "slot", slot }, { "label", label } });
	}
	return chips;
}

nlohmann::json SceneViewPage::gateRuns() const
{
	nlohmann::json runs = nlohmann::json::array();
	for (const Snapshot& r : m_gateRuns)
	{
		const bool pass = truthy(findAt(r, { "pass" }));
		const std::string reason = stringOr(findAt(r, { "reason" }), "");
		runs.push_back({ { "file", stringOr(findAt(r, { "file" }), "") }, { "pass", pass },
		                 { "reason", "(" + reason + ")" }, { "has_reason", !pass && !reason.empty() } });
	}
	return runs;
}

void SceneViewPage::publish()
{
	const nlohmann::json runs = gateRuns();
	const auto passed = std::count_if(runs.begin(), runs.end(), [](const nlohmann::json& r) { return r["pass"].get<bool>(); });
	const std::size_t selected = m_editor.selection().size();
	m_view->set("ready", m_ready);
	m_view->set("banner", branchBannerText(&m_branch));
	m_view->set("has_image", m_spaceW > 0);
	m_view->set("boxes", boxes());
	m_view->set("marquee", marquee());
	m_view->set("can_commit", m_pending.has_value());
	m_view->set("can_undo", m_editor.canUndo());
	m_view->set("can_redo", m_editor.canRedo());
	m_view->set("status", m_status);
	m_view->set("multi", selected > 1 ? std::to_string(selected) + " 個を選択中 (ドラッグで一括移動、Esc で解除)" : std::string());
	m_view->set("rows", inspectorRows());
	m_view->set("candidates", candidateChips());
	m_view->set("gate_title", "決定論ゲート (" + std::to_string(passed) + "/" + std::to_string(runs.size()) + ")");
	m_view->set("gate_runs", runs);
}

} // namespace mitiru::tool
