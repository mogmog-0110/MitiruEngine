#pragma once

/// @file UISplitter.hpp
/// @brief drag handle 付きの可変サイズ panel 分割 widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief splitter の向き (分割線がどの軸に沿うか)。
enum class SplitterOrientation : std::uint8_t
{
	Horizontal,  ///< panel が左右並び (left | right)、handle は縦。
	Vertical     ///< panel が上下積み (top / bottom)、handle は横。
};

/// @brief UISplitter 生成用の設定。
struct UISplitterConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	SplitterOrientation orientation = SplitterOrientation::Horizontal;
	float initialRatio = 0.5f;            ///< 初期の分割比 (0.0-1.0)。
	float minRatio = 0.1f;                ///< panel A の最小比。
	float maxRatio = 0.9f;                ///< panel A の最大比。
	float handleSize = 6.0f;              ///< handle の太さ (pixel)。
	float totalWidth = 800.0f;            ///< widget 全体の幅。
	float totalHeight = 600.0f;           ///< widget 全体の高さ。
	std::string handleImageKey;            ///< デフォルトの handle 画像。
	std::string handleHoverImageKey;       ///< hover 時の handle 画像。
	std::string handleDragImageKey;        ///< drag 中の handle 画像。
	std::string panelABackgroundImageKey;  ///< panel A の背景画像。
	std::string panelBBackgroundImageKey;  ///< panel B の背景画像。
};

/// @brief 可変サイズの panel 分割 widget。
///
/// 利用可能な領域を、drag 可能な handle で区切られた 2 つの panel (A と B)
/// に分割する。horizontal / vertical の向き、比の clamp、double-click で
/// リセット に対応。
///
/// @code
///   UISplitterConfig cfg;
///   cfg.id = 140;
///   cfg.orientation = SplitterOrientation::Horizontal;
///   cfg.initialRatio = 0.3f;
///   cfg.totalWidth = 1024.0f;
///   cfg.totalHeight = 768.0f;
///   UISplitter splitter(cfg);
///
///   splitter.setOnRatioChanged([](float r) { /* relayout children */ });
///   auto [ax, ay, aw, ah] = splitter.panelABounds();
/// @endcode
class UISplitter
{
	std::shared_ptr<UINode> m_node;
	SplitterOrientation m_orientation;
	float m_ratio;
	float m_minRatio;
	float m_maxRatio;
	float m_handleSize;
	float m_totalWidth;
	float m_totalHeight;
	bool m_dragging = false;
	bool m_hovered = false;
	float m_dragStartRatio = 0.0f;
	float m_dragStartPos = 0.0f;

	std::function<void(float)> m_onRatioChanged;

public:
	/// @brief 設定から splitter を構築する。
	/// @param config splitter の設定。
	explicit UISplitter(const UISplitterConfig& config)
		: m_orientation(config.orientation)
		, m_ratio(std::clamp(config.initialRatio, config.minRatio, config.maxRatio))
		, m_minRatio(config.minRatio)
		, m_maxRatio(config.maxRatio)
		, m_handleSize(config.handleSize)
		, m_totalWidth(config.totalWidth)
		, m_totalHeight(config.totalHeight)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.totalWidth, config.totalHeight);
		data.properties["widget_type"] = "splitter";
		data.properties["orientation"] = (m_orientation == SplitterOrientation::Horizontal) ? "horizontal" : "vertical";
		data.properties["handle_size"] = std::to_string(m_handleSize);
		data.properties["min_ratio"] = std::to_string(m_minRatio);
		data.properties["max_ratio"] = std::to_string(m_maxRatio);
		data.properties["handle_image"] = config.handleImageKey;
		data.properties["handle_hover_image"] = config.handleHoverImageKey;
		data.properties["handle_drag_image"] = config.handleDragImageKey;
		data.properties["panel_a_bg_image"] = config.panelABackgroundImageKey;
		data.properties["panel_b_bg_image"] = config.panelBBackgroundImageKey;

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の分割比を取得する。
	[[nodiscard]] float getRatio() const noexcept { return m_ratio; }

	/// @brief handle が drag 中か判定する。
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief handle が hover 中か判定する。
	[[nodiscard]] bool isHovered() const noexcept { return m_hovered; }

	/// @brief panel A の bounds を (x, y, width, height) で取得する。
	struct PanelBounds { float x; float y; float width; float height; };

	/// @brief panel A の bounds を取得する。
	[[nodiscard]] PanelBounds panelABounds() const noexcept
	{
		if (m_orientation == SplitterOrientation::Horizontal)
		{
			const float panelAWidth = m_totalWidth * m_ratio - m_handleSize * 0.5f;
			return {0.0f, 0.0f, std::max(0.0f, panelAWidth), m_totalHeight};
		}
		const float panelAHeight = m_totalHeight * m_ratio - m_handleSize * 0.5f;
		return {0.0f, 0.0f, m_totalWidth, std::max(0.0f, panelAHeight)};
	}

	/// @brief panel B の bounds を取得する。
	[[nodiscard]] PanelBounds panelBBounds() const noexcept
	{
		if (m_orientation == SplitterOrientation::Horizontal)
		{
			const float handleEnd = m_totalWidth * m_ratio + m_handleSize * 0.5f;
			return {handleEnd, 0.0f, std::max(0.0f, m_totalWidth - handleEnd), m_totalHeight};
		}
		const float handleEnd = m_totalHeight * m_ratio + m_handleSize * 0.5f;
		return {0.0f, handleEnd, m_totalWidth, std::max(0.0f, m_totalHeight - handleEnd)};
	}

	/// @brief handle の bounds を取得する。
	[[nodiscard]] PanelBounds handleBounds() const noexcept
	{
		if (m_orientation == SplitterOrientation::Horizontal)
		{
			const float handleX = m_totalWidth * m_ratio - m_handleSize * 0.5f;
			return {handleX, 0.0f, m_handleSize, m_totalHeight};
		}
		const float handleY = m_totalHeight * m_ratio - m_handleSize * 0.5f;
		return {0.0f, handleY, m_totalWidth, m_handleSize};
	}

	// ── 設定 ────────────────────────────────────────

	/// @brief 比変更時の callback を設定する。
	/// @param callback 比が変わったときに呼ばれる関数。
	void setOnRatioChanged(std::function<void(float)> callback) { m_onRatioChanged = std::move(callback); }

	/// @brief 分割比をプログラム的に設定する。
	/// @param ratio 新しい比 ([minRatio, maxRatio] に clamp される)。
	void setRatio(float ratio)
	{
		const float clamped = std::clamp(ratio, m_minRatio, m_maxRatio);
		if (clamped != m_ratio)
		{
			m_ratio = clamped;
			syncNodeState();
			if (m_onRatioChanged) { m_onRatioChanged(m_ratio); }
		}
	}

	/// @brief 全体サイズを設定する。
	/// @param width 全体の幅。
	/// @param height 全体の高さ。
	void setSize(float width, float height)
	{
		m_totalWidth = width;
		m_totalHeight = height;
		m_node->setBounds(sgc::Rectf(0.0f, 0.0f, width, height));
		syncNodeState();
	}

	// ── 操作 ──────────────────────────────────────────

	/// @brief pointer が handle 上を hover していることを通知する。
	void onHandleHoverEnter()
	{
		m_hovered = true;
		m_node->setProperty("handle_state", "hovered");
	}

	/// @brief pointer が handle から離れたことを通知する。
	void onHandleHoverLeave()
	{
		if (!m_dragging) { m_hovered = false; }
		m_node->setProperty("handle_state", m_dragging ? "dragging" : "normal");
	}

	/// @brief handle の drag を開始する。
	/// @param pointerPos 分割軸に沿った現在の pointer 位置。
	void beginDrag(float pointerPos)
	{
		m_dragging = true;
		m_dragStartRatio = m_ratio;
		m_dragStartPos = pointerPos;
		m_node->setProperty("handle_state", "dragging");
	}

	/// @brief 新しい pointer 位置で drag を更新する。
	/// @param pointerPos 分割軸に沿った現在の pointer 位置。
	void updateDrag(float pointerPos)
	{
		if (!m_dragging) { return; }

		const float totalLength = (m_orientation == SplitterOrientation::Horizontal) ? m_totalWidth : m_totalHeight;
		if (totalLength <= 0.0f) { return; }

		const float delta = (pointerPos - m_dragStartPos) / totalLength;
		setRatio(m_dragStartRatio + delta);
	}

	/// @brief drag を終了する。
	void endDrag()
	{
		m_dragging = false;
		m_node->setProperty("handle_state", m_hovered ? "hovered" : "normal");
	}

	/// @brief handle を double-click して 50/50 にリセットする。
	void resetToCenter()
	{
		setRatio(0.5f);
	}

private:
	/// @brief state を UINode に同期する。
	void syncNodeState()
	{
		m_node->setProperty("ratio", std::to_string(m_ratio));

		const auto panelA = panelABounds();
		m_node->setProperty("panel_a_x", std::to_string(panelA.x));
		m_node->setProperty("panel_a_y", std::to_string(panelA.y));
		m_node->setProperty("panel_a_width", std::to_string(panelA.width));
		m_node->setProperty("panel_a_height", std::to_string(panelA.height));

		const auto panelB = panelBBounds();
		m_node->setProperty("panel_b_x", std::to_string(panelB.x));
		m_node->setProperty("panel_b_y", std::to_string(panelB.y));
		m_node->setProperty("panel_b_width", std::to_string(panelB.width));
		m_node->setProperty("panel_b_height", std::to_string(panelB.height));

		const auto handle = handleBounds();
		m_node->setProperty("handle_x", std::to_string(handle.x));
		m_node->setProperty("handle_y", std::to_string(handle.y));
		m_node->setProperty("handle_width", std::to_string(handle.width));
		m_node->setProperty("handle_height", std::to_string(handle.height));
	}
};

} // namespace mitiru::ui
