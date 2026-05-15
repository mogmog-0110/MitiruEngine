#pragma once
/// @file UIDragDrop.hpp
/// @brief 統合ドラッグ＆ドロップシステム

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief ドラッグ中のデータ
struct DragPayload
{
	std::string type;                                   ///< ペイロード型識別子
	std::variant<int, float, std::string> data;         ///< データ
	UINodeId sourceNodeId = INVALID_UI_NODE;            ///< ドラッグ元ノードID
	std::string icon;                                   ///< ゴーストアイコンキー
};

/// @brief ドロップ結果種別
enum class DropResultType : std::uint8_t { Accepted, Rejected, Cancelled, NoTarget };

/// @brief ドロップ結果
struct DropResult
{
	DropResultType type = DropResultType::NoTarget;
	UINodeId targetNodeId = INVALID_UI_NODE;
};

/// @brief ドロップゾーンのハイライト状態
enum class DropZoneHighlight : std::uint8_t { None, Valid, Invalid };

using DropCallback = std::function<bool(const DragPayload&)>;

/// @brief ドロップターゲット登録情報
struct DropTarget
{
	UINodeId nodeId = INVALID_UI_NODE;
	std::vector<std::string> acceptedTypes;
	DropCallback onDrop;
	sgc::Rectf bounds;
};

/// @brief ドラッグ中のレンダリング情報
struct DragRenderInfo
{
	bool active = false;
	float mouseX = 0.0f, mouseY = 0.0f;
	std::string icon, payloadType;
	UINodeId sourceNodeId = INVALID_UI_NODE;
};

/// @brief ドロップゾーンのレンダリング情報
struct DropZoneRenderInfo
{
	UINodeId nodeId = INVALID_UI_NODE;
	sgc::Rectf bounds;
	DropZoneHighlight highlight = DropZoneHighlight::None;
};

/// @brief 統合ドラッグ＆ドロップ管理
/// @code
///   UIDragDropManager dd;
///   dd.registerDropTarget(invId, {"item"}, [](const DragPayload& p) { return true; }, invBounds);
///   dd.beginDrag(payload, mx, my);
///   dd.updateDrag(mx, my);
///   auto result = dd.endDrag(mx, my);
/// @endcode
class UIDragDropManager
{
	bool m_dragging = false;
	DragPayload m_payload;
	float m_mouseX = 0.0f, m_mouseY = 0.0f, m_startX = 0.0f, m_startY = 0.0f;
	std::vector<DropTarget> m_targets;
	UINodeId m_hoveredTarget = INVALID_UI_NODE;

public:
	void beginDrag(const DragPayload& payload, float mx, float my)
	{
		m_dragging = true; m_payload = payload;
		m_mouseX = m_startX = mx; m_mouseY = m_startY = my;
		m_hoveredTarget = INVALID_UI_NODE;
	}

	void updateDrag(float mx, float my)
	{
		if (!m_dragging) { return; }
		m_mouseX = mx; m_mouseY = my;
		m_hoveredTarget = findTargetAt(mx, my);
	}

	[[nodiscard]] DropResult endDrag(float mx, float my)
	{
		if (!m_dragging) { return {DropResultType::NoTarget, INVALID_UI_NODE}; }
		m_dragging = false; m_mouseX = mx; m_mouseY = my;
		const UINodeId tid = findTargetAt(mx, my);
		m_hoveredTarget = INVALID_UI_NODE;
		if (tid == INVALID_UI_NODE) { return {DropResultType::NoTarget, INVALID_UI_NODE}; }
		for (const auto& t : m_targets) {
			if (t.nodeId == tid) {
				if (t.onDrop && t.onDrop(m_payload)) { return {DropResultType::Accepted, tid}; }
				return {DropResultType::Rejected, tid};
			}
		}
		return {DropResultType::NoTarget, INVALID_UI_NODE};
	}

	[[nodiscard]] DropResult cancelDrag()
	{
		m_dragging = false; m_hoveredTarget = INVALID_UI_NODE;
		return {DropResultType::Cancelled, INVALID_UI_NODE};
	}

	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }
	[[nodiscard]] const DragPayload& currentPayload() const noexcept { return m_payload; }
	[[nodiscard]] float mouseX() const noexcept { return m_mouseX; }
	[[nodiscard]] float mouseY() const noexcept { return m_mouseY; }
	[[nodiscard]] float startX() const noexcept { return m_startX; }
	[[nodiscard]] float startY() const noexcept { return m_startY; }
	[[nodiscard]] UINodeId hoveredTarget() const noexcept { return m_hoveredTarget; }
	[[nodiscard]] std::size_t targetCount() const noexcept { return m_targets.size(); }

	void registerDropTarget(UINodeId nodeId, std::vector<std::string> types,
	                        DropCallback onDrop, const sgc::Rectf& bounds)
	{
		for (auto& t : m_targets) {
			if (t.nodeId == nodeId) { t.acceptedTypes = std::move(types); t.onDrop = std::move(onDrop); t.bounds = bounds; return; }
		}
		m_targets.push_back({nodeId, std::move(types), std::move(onDrop), bounds});
	}

	void unregisterDropTarget(UINodeId nodeId)
	{
		m_targets.erase(std::remove_if(m_targets.begin(), m_targets.end(),
			[nodeId](const DropTarget& t) { return t.nodeId == nodeId; }), m_targets.end());
	}

	void updateTargetBounds(UINodeId nodeId, const sgc::Rectf& bounds)
	{
		for (auto& t : m_targets) { if (t.nodeId == nodeId) { t.bounds = bounds; return; } }
	}

	void clearTargets() { m_targets.clear(); }

	[[nodiscard]] DragRenderInfo dragRenderInfo() const
	{
		return {m_dragging, m_mouseX, m_mouseY, m_payload.icon, m_payload.type, m_payload.sourceNodeId};
	}

	[[nodiscard]] std::vector<DropZoneRenderInfo> dropZoneRenderInfo() const
	{
		std::vector<DropZoneRenderInfo> r;
		if (!m_dragging) { return r; }
		r.reserve(m_targets.size());
		for (const auto& t : m_targets) {
			r.push_back({t.nodeId, t.bounds, canAccept(t, m_payload.type) ? DropZoneHighlight::Valid : DropZoneHighlight::Invalid});
		}
		return r;
	}

private:
	[[nodiscard]] UINodeId findTargetAt(float x, float y) const noexcept
	{
		for (const auto& t : m_targets) {
			const auto& b = t.bounds;
			if (x >= b.x() && x < b.x() + b.width() && y >= b.y() && y < b.y() + b.height()) { return t.nodeId; }
		}
		return INVALID_UI_NODE;
	}

	[[nodiscard]] static bool canAccept(const DropTarget& t, const std::string& type) noexcept
	{
		if (t.acceptedTypes.empty()) { return true; }
		for (const auto& a : t.acceptedTypes) { if (a == type) { return true; } }
		return false;
	}
};

} // namespace mitiru::ui
