#pragma once

/// @file UISplitter.hpp
/// @brief Resizable panel divider widget with drag handle.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Splitter orientation (which axis the divider runs along).
enum class SplitterOrientation : std::uint8_t
{
	Horizontal,  ///< Panels are side by side (left | right), handle is vertical.
	Vertical     ///< Panels are stacked (top / bottom), handle is horizontal.
};

/// @brief Configuration for creating a UISplitter.
struct UISplitterConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	SplitterOrientation orientation = SplitterOrientation::Horizontal;
	float initialRatio = 0.5f;            ///< Initial split ratio (0.0-1.0).
	float minRatio = 0.1f;                ///< Minimum ratio for panel A.
	float maxRatio = 0.9f;                ///< Maximum ratio for panel A.
	float handleSize = 6.0f;              ///< Handle thickness in pixels.
	float totalWidth = 800.0f;            ///< Total widget width.
	float totalHeight = 600.0f;           ///< Total widget height.
	std::string handleImageKey;            ///< Default handle image.
	std::string handleHoverImageKey;       ///< Handle image on hover.
	std::string handleDragImageKey;        ///< Handle image while dragging.
	std::string panelABackgroundImageKey;  ///< Panel A background image.
	std::string panelBBackgroundImageKey;  ///< Panel B background image.
};

/// @brief Resizable panel divider widget.
///
/// Splits available space into two panels (A and B) separated by a
/// draggable handle. Supports horizontal and vertical orientations,
/// ratio clamping, and double-click to reset.
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
	/// @brief Construct a splitter from configuration.
	/// @param config Splitter configuration.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current split ratio.
	[[nodiscard]] float getRatio() const noexcept { return m_ratio; }

	/// @brief Check if the handle is being dragged.
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief Check if the handle is being hovered.
	[[nodiscard]] bool isHovered() const noexcept { return m_hovered; }

	/// @brief Get panel A bounds as (x, y, width, height).
	struct PanelBounds { float x; float y; float width; float height; };

	/// @brief Get the bounds of panel A.
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

	/// @brief Get the bounds of panel B.
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

	/// @brief Get the bounds of the handle.
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

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the ratio-changed callback.
	/// @param callback Function invoked when ratio changes.
	void setOnRatioChanged(std::function<void(float)> callback) { m_onRatioChanged = std::move(callback); }

	/// @brief Set the split ratio programmatically.
	/// @param ratio New ratio (clamped to [minRatio, maxRatio]).
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

	/// @brief Set the total dimensions.
	/// @param width Total width.
	/// @param height Total height.
	void setSize(float width, float height)
	{
		m_totalWidth = width;
		m_totalHeight = height;
		m_node->setBounds(sgc::Rectf(0.0f, 0.0f, width, height));
		syncNodeState();
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Notify that the pointer is hovering over the handle.
	void onHandleHoverEnter()
	{
		m_hovered = true;
		m_node->setProperty("handle_state", "hovered");
	}

	/// @brief Notify that the pointer has left the handle.
	void onHandleHoverLeave()
	{
		if (!m_dragging) { m_hovered = false; }
		m_node->setProperty("handle_state", m_dragging ? "dragging" : "normal");
	}

	/// @brief Begin dragging the handle.
	/// @param pointerPos Current pointer position along the split axis.
	void beginDrag(float pointerPos)
	{
		m_dragging = true;
		m_dragStartRatio = m_ratio;
		m_dragStartPos = pointerPos;
		m_node->setProperty("handle_state", "dragging");
	}

	/// @brief Update the drag with a new pointer position.
	/// @param pointerPos Current pointer position along the split axis.
	void updateDrag(float pointerPos)
	{
		if (!m_dragging) { return; }

		const float totalLength = (m_orientation == SplitterOrientation::Horizontal) ? m_totalWidth : m_totalHeight;
		if (totalLength <= 0.0f) { return; }

		const float delta = (pointerPos - m_dragStartPos) / totalLength;
		setRatio(m_dragStartRatio + delta);
	}

	/// @brief End the drag.
	void endDrag()
	{
		m_dragging = false;
		m_node->setProperty("handle_state", m_hovered ? "hovered" : "normal");
	}

	/// @brief Double-click the handle to reset to 50/50.
	void resetToCenter()
	{
		setRatio(0.5f);
	}

private:
	/// @brief Synchronize state to the UINode.
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
