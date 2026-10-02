#include "SceneViewEditor.hpp"

#include "../PageUtil.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool
{

namespace
{

bool isNumberAt(const Snapshot& gm, const std::string& key)
{
	const auto it = gm.find(key);
	return it != gm.end() && it->is_number();
}

double numberAt(const Snapshot& gm, const std::string& key)
{
	return gm.find(key)->get<double>();
}

} // namespace

std::vector<SceneObject> sceneObjectsFrom(const Snapshot& objects)
{
	std::vector<SceneObject> out;
	if (!objects.is_array()) { return out; }
	for (const Snapshot& o : objects)
	{
		SceneObject s;
		const Snapshot* id = findAt(o, { "id" });
		s.id = id != nullptr ? id->dump() : std::to_string(out.size());
		s.x = numberOr(findAt(o, { "x" }), 0.0);
		s.y = numberOr(findAt(o, { "y" }), 0.0);
		s.w = numberOr(findAt(o, { "w" }), 0.0);
		s.h = numberOr(findAt(o, { "h" }), 0.0);
		s.field = stringOr(findAt(o, { "field" }), "");
		s.fieldX = stringOr(findAt(o, { "fieldX" }), "");
		s.fieldY = stringOr(findAt(o, { "fieldY" }), "");
		out.push_back(std::move(s));
	}
	return out;
}

void SceneViewEditor::setObjects(std::vector<SceneObject> incoming)
{
	if (m_drag) { return; }
	m_objects = std::move(incoming);
	// 選択は id で新しい枠に付け替え、消えた枠は選択から外す。
	std::erase_if(m_selection, [this](const std::string& id) { return find(id) == nullptr; });
}

SceneObject* SceneViewEditor::find(const std::string& id)
{
	const auto it = std::find_if(m_objects.begin(), m_objects.end(), [&id](const SceneObject& o) { return o.id == id; });
	return it == m_objects.end() ? nullptr : &*it;
}

const SceneObject* SceneViewEditor::primary() const
{
	if (m_selection.empty()) { return nullptr; }
	const auto it = std::find_if(m_objects.begin(), m_objects.end(),
	                             [this](const SceneObject& o) { return o.id == m_selection.back(); });
	return it == m_objects.end() ? nullptr : &*it;
}

bool SceneViewEditor::isSelected(const std::string& id) const
{
	return std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end();
}

// 後ろ (配列の末尾 = 最後に描かれた = 手前) から探す。
const SceneObject* SceneViewEditor::objectAt(double x, double y) const
{
	for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
	{
		if (x >= it->x && x <= it->x + it->w && y >= it->y && y <= it->y + it->h) { return &*it; }
	}
	return nullptr;
}

// クリック = 単独選択、Ctrl+クリック = 追加 / 解除、Shift+空白ドラッグ = 矩形選択、選択済みの枠を
// つかむと選択全員をまとめて動かす。
void SceneViewEditor::pointerDown(double x, double y, bool ctrl, bool shift)
{
	const SceneObject* hit = objectAt(x, y);
	if (shift && hit == nullptr)
	{
		m_marquee = Marquee{ x, y, x, y };
		return;
	}
	if (hit == nullptr) { m_selection.clear(); }
	else if (ctrl)
	{
		const auto it = std::find(m_selection.begin(), m_selection.end(), hit->id);
		if (it != m_selection.end()) { m_selection.erase(it); }
		else { m_selection.push_back(hit->id); }
	}
	else if (!isSelected(hit->id)) { m_selection = { hit->id }; }
	if (hit == nullptr || ctrl) { return; }
	Drag drag{ {}, x, y };
	for (const std::string& id : m_selection)
	{
		if (const SceneObject* o = find(id)) { drag.entries.push_back({ id, o->x, o->y }); }
	}
	m_drag = std::move(drag);
}

void SceneViewEditor::pointerMove(double x, double y)
{
	if (m_marquee)
	{
		m_marquee->x1 = x;
		m_marquee->y1 = y;
		return;
	}
	if (!m_drag) { return; }
	for (const DragEntry& e : m_drag->entries)
	{
		if (SceneObject* o = find(e.id))
		{
			o->x = e.origX + (x - m_drag->startX);
			o->y = e.origY + (y - m_drag->startY);
		}
	}
}

// 矩形選択は枠の中心が矩形に入るかで決める (交差で決めると画面いっぱいの背景の枠まで拾う)。
void SceneViewEditor::finishMarquee()
{
	const Marquee m = *m_marquee;
	m_marquee.reset();
	const double x0 = std::min(m.x0, m.x1), x1 = std::max(m.x0, m.x1);
	const double y0 = std::min(m.y0, m.y1), y1 = std::max(m.y0, m.y1);
	std::vector<std::string> picked;
	for (const SceneObject& o : m_objects)
	{
		const double cx = o.x + o.w / 2.0, cy = o.y + o.h / 2.0;
		if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) { picked.push_back(o.id); }
	}
	if (picked.size() > kMaxSelection)
	{
		const std::string n = std::to_string(kMaxSelection);
		m_notice = "選択は " + n + " 個まで (1 回の PUT に載る field 数の上限)。先頭 " + n + " 個だけ選びました";
		picked.resize(kMaxSelection);
	}
	m_selection = std::move(picked);
}

// 枠の座標は画面 (world) だが、書き戻す field は LocalTransform の子なら親相対 (local) なので、絶対値ではなく
// 今の値に差分を足す。今の値が無い (名前解決できていない) 時だけ枠の絶対座標を書き、field 名も無い枠は飛ばす。
nlohmann::json SceneViewEditor::moveFields(double dx, double dy) const
{
	nlohmann::json fields = nlohmann::json::object();
	for (const DragEntry& e : m_drag->entries)
	{
		const auto it = std::find_if(m_objects.begin(), m_objects.end(), [&e](const SceneObject& o) { return o.id == e.id; });
		if (it == m_objects.end() || (it->field.empty() && it->fieldX.empty())) { continue; }
		const bool explicitKeys = !it->fieldX.empty() && !it->fieldY.empty();
		const std::string kx = explicitKeys ? it->fieldX : it->field + ".x";
		const std::string ky = explicitKeys ? it->fieldY : it->field + ".y";
		fields[kx] = isNumberAt(m_gameMemory, kx) ? numberAt(m_gameMemory, kx) + dx : it->x;
		fields[ky] = isNumberAt(m_gameMemory, ky) ? numberAt(m_gameMemory, ky) + dy : it->y;
	}
	return fields;
}

std::optional<MoveRequest> SceneViewEditor::pointerUp()
{
	if (m_marquee) { finishMarquee(); return std::nullopt; }
	if (!m_drag) { return std::nullopt; }
	std::optional<MoveRequest> out;
	if (!m_drag->entries.empty())
	{
		const DragEntry& first = m_drag->entries.front();
		const SceneObject* o = find(first.id);
		const double dx = o != nullptr ? o->x - first.origX : 0.0;
		const double dy = o != nullptr ? o->y - first.origY : 0.0;
		nlohmann::json fields = moveFields(dx, dy);
		if (!fields.empty())
		{
			const std::string name = o == nullptr ? std::string() : (o->field.empty() ? o->fieldX : o->field);
			const std::string label = m_drag->entries.size() > 1
				? "移動 " + std::to_string(m_drag->entries.size()) + " 個" : "移動 " + name;
			out = MoveRequest{ std::move(fields), label, dx != 0.0 || dy != 0.0 };
		}
	}
	m_drag.reset();
	return out;
}

void SceneViewEditor::clearSelection()
{
	m_selection.clear();
	m_marquee.reset();
}

void SceneViewEditor::pushUndo(const std::string& label, const nlohmann::json& after)
{
	nlohmann::json before = nlohmann::json::object();
	for (auto it = after.begin(); it != after.end(); ++it)
	{
		if (isNumberAt(m_gameMemory, it.key())) { before[it.key()] = numberAt(m_gameMemory, it.key()); }
	}
	if (before.empty()) { return; }
	m_done.push_back({ label, std::move(before), after });
	if (m_done.size() > kMaxUndo) { m_done.erase(m_done.begin()); }
	m_undone.clear();
}

std::optional<UndoStep> SceneViewEditor::undo()
{
	if (m_done.empty()) { return std::nullopt; }
	UndoStep step = m_done.back();
	m_done.pop_back();
	m_undone.push_back(step);
	return step;
}

std::optional<UndoStep> SceneViewEditor::redo()
{
	if (m_undone.empty()) { return std::nullopt; }
	UndoStep step = m_undone.back();
	m_undone.pop_back();
	m_done.push_back(step);
	return step;
}

void SceneViewEditor::clearHistory()
{
	m_done.clear();
	m_undone.clear();
}

} // namespace mitiru::tool
