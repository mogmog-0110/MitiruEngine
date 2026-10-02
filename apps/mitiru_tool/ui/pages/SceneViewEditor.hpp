#pragma once
// scene_view (ADR 0035 分岐エディタ) の手触りの部分: 枠の選択・ドラッグ・矩形選択・取り消し。
// 座標はゲーム画面の絵 (/api/ai/frame の PNG) の画素。ここはゲームへ何も送らず、ドラッグを離した時に
// 「この field をこの値で試して」という PUT の中身を返すだけ (送るのは SceneViewPage)。

#include "../SnapshotFormat.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mitiru::tool
{

struct SceneObject
{
	std::string id;
	double x = 0.0;
	double y = 0.0;
	double w = 0.0;
	double h = 0.0;
	std::string field;    ///< 空なら名前解決できていない枠
	std::string fieldX;   ///< beginObject(name, fieldX, fieldY) で明示した 2 キー
	std::string fieldY;
};

[[nodiscard]] std::vector<SceneObject> sceneObjectsFrom(const Snapshot& objects);

struct Marquee
{
	double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};

/// ドラッグを離した結果。fields は {"player.x": 12, ...}。
struct MoveRequest
{
	nlohmann::json fields;
	std::string label;
	bool moved = false;
};

struct UndoStep
{
	std::string label;
	nlohmann::json before;
	nlohmann::json after;
};

class SceneViewEditor
{
public:
	/// 1 回の PUT に載せる枠の数 (1 体につき x/y の 2 field を書く)。
	static constexpr std::size_t kMaxSelection = 60;
	static constexpr std::size_t kMaxUndo = 128;

	/// host の新しい枠。ドラッグ中は置き換えない (動かしている途中の枠が元の位置へ戻って見えるので)。
	void setObjects(std::vector<SceneObject> incoming);
	void setGameMemory(Snapshot gm) { m_gameMemory = std::move(gm); }

	void pointerDown(double x, double y, bool ctrl, bool shift);
	void pointerMove(double x, double y);
	[[nodiscard]] std::optional<MoveRequest> pointerUp();
	void clearSelection();

	/// PUT の前後の値を 1 手として積む。before は今の値から拾えたキーだけ (拾えなければ積まない)。
	void pushUndo(const std::string& label, const nlohmann::json& after);
	[[nodiscard]] std::optional<UndoStep> undo();
	[[nodiscard]] std::optional<UndoStep> redo();
	void clearHistory();

	[[nodiscard]] const std::vector<SceneObject>& objects() const noexcept { return m_objects; }
	[[nodiscard]] const std::vector<std::string>& selection() const noexcept { return m_selection; }
	[[nodiscard]] const SceneObject* primary() const;
	[[nodiscard]] bool isSelected(const std::string& id) const;
	[[nodiscard]] const std::optional<Marquee>& marquee() const noexcept { return m_marquee; }
	[[nodiscard]] const Snapshot& gameMemory() const noexcept { return m_gameMemory; }
	[[nodiscard]] bool dragging() const noexcept { return m_drag.has_value(); }
	[[nodiscard]] bool canUndo() const noexcept { return !m_done.empty(); }
	[[nodiscard]] bool canRedo() const noexcept { return !m_undone.empty(); }

	/// 矩形選択で上限を超えた時の知らせ (読んだら消える)。
	[[nodiscard]] std::string takeNotice() { return std::exchange(m_notice, {}); }

private:
	struct DragEntry
	{
		std::string id;
		double origX = 0.0;
		double origY = 0.0;
	};
	struct Drag
	{
		std::vector<DragEntry> entries;
		double startX = 0.0;
		double startY = 0.0;
	};

	[[nodiscard]] SceneObject* find(const std::string& id);
	[[nodiscard]] const SceneObject* objectAt(double x, double y) const;
	void finishMarquee();
	[[nodiscard]] nlohmann::json moveFields(double dx, double dy) const;

	std::vector<SceneObject> m_objects;
	std::vector<std::string> m_selection;   ///< 末尾が主選択 (mini inspector の対象)
	std::optional<Marquee> m_marquee;
	std::optional<Drag> m_drag;
	Snapshot m_gameMemory = Snapshot::object();
	std::vector<UndoStep> m_done;
	std::vector<UndoStep> m_undone;
	std::string m_notice;
};

} // namespace mitiru::tool
