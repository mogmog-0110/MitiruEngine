#pragma once

/// @file UIDraggableWindow.hpp
/// @brief Draggable, resizable window/panel widget with title bar, close, and minimize.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Configuration for creating a UIDraggableWindow.
struct UIDraggableWindowConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string title;
	float width = 400.0f;
	float height = 300.0f;
	float minWidth = 150.0f;
	float minHeight = 100.0f;
	float maxWidth = 2000.0f;
	float maxHeight = 2000.0f;
	bool resizable = true;
	bool closeable = true;
	bool minimizable = true;
	bool draggable = true;
	float titleBarHeight = 28.0f;
	std::string titleBarImageKey;
	std::string backgroundImageKey;
	std::string closeButtonImageKey;
	std::string minimizeButtonImageKey;
	std::string resizeHandleImageKey;
	float borderWidth = 1.0f;
	float padding = 4.0f;
	float titleFontSize = 14.0f;
	float initialX = 100.0f;
	float initialY = 100.0f;
};

/// @brief Edge/corner region for resize hit-testing.
enum class WindowResizeEdge : std::uint8_t
{
	None,
	Left,
	Right,
	Top,
	Bottom,
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};

/// @brief Draggable, resizable window widget.
///
/// Manages a panel with a title bar, optional close/minimize buttons,
/// drag-to-move, and edge/corner resize. Content area accepts child widgets.
/// Z-order is managed externally; call bringToFront() to request re-ordering.
///
/// @code
///   UIDraggableWindowConfig cfg;
///   cfg.id = 100;
///   cfg.title = "Inventory";
///   cfg.closeable = true;
///   UIDraggableWindow win(cfg);
///
///   win.setOnClose([] { /* handle close */ });
///   win.open();
/// @endcode
class UIDraggableWindow
{
	std::shared_ptr<UINode> m_node;
	std::string m_title;
	float m_width;
	float m_height;
	float m_minWidth;
	float m_minHeight;
	float m_maxWidth;
	float m_maxHeight;
	float m_titleBarHeight;
	float m_borderWidth;
	float m_padding;
	bool m_resizable;
	bool m_closeable;
	bool m_minimizable;
	bool m_draggable;

	bool m_open = true;
	bool m_minimized = false;
	bool m_dragging = false;
	bool m_resizing = false;
	WindowResizeEdge m_resizeEdge = WindowResizeEdge::None;
	float m_dragOffsetX = 0.0f;
	float m_dragOffsetY = 0.0f;
	float m_posX;
	float m_posY;
	std::uint32_t m_zOrder = 0;

	std::function<void()> m_onClose;
	std::function<void()> m_onMinimize;
	std::function<void()> m_onRestore;
	std::function<void(std::uint32_t)> m_onBringToFront;

	std::vector<std::shared_ptr<UINode>> m_children;

public:
	/// @brief Construct a draggable window from configuration.
	/// @param config Window configuration.
	explicit UIDraggableWindow(const UIDraggableWindowConfig& config)
		: m_title(config.title)
		, m_width(config.width)
		, m_height(config.height)
		, m_minWidth(config.minWidth)
		, m_minHeight(config.minHeight)
		, m_maxWidth(config.maxWidth)
		, m_maxHeight(config.maxHeight)
		, m_titleBarHeight(config.titleBarHeight)
		, m_borderWidth(config.borderWidth)
		, m_padding(config.padding)
		, m_resizable(config.resizable)
		, m_closeable(config.closeable)
		, m_minimizable(config.minimizable)
		, m_draggable(config.draggable)
		, m_posX(config.initialX)
		, m_posY(config.initialY)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Panel;
		data.text = config.title;
		data.bounds = sgc::Rectf(m_posX, m_posY, m_width, m_height);
		data.properties["widget_type"] = "draggable_window";
		data.properties["title_bar_image"] = config.titleBarImageKey;
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["close_button_image"] = config.closeButtonImageKey;
		data.properties["minimize_button_image"] = config.minimizeButtonImageKey;
		data.properties["resize_handle_image"] = config.resizeHandleImageKey;
		data.properties["title_font_size"] = std::to_string(config.titleFontSize);
		data.properties["border_width"] = std::to_string(config.borderWidth);
		data.properties["padding"] = std::to_string(config.padding);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Check if the window is open.
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief Check if the window is minimized.
	[[nodiscard]] bool isMinimized() const noexcept { return m_minimized; }

	/// @brief Check if the window is being dragged.
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief Check if the window is being resized.
	[[nodiscard]] bool isResizing() const noexcept { return m_resizing; }

	/// @brief Get the window position X.
	[[nodiscard]] float posX() const noexcept { return m_posX; }

	/// @brief Get the window position Y.
	[[nodiscard]] float posY() const noexcept { return m_posY; }

	/// @brief Get the window width.
	[[nodiscard]] float width() const noexcept { return m_width; }

	/// @brief Get the window height.
	[[nodiscard]] float height() const noexcept { return m_height; }

	/// @brief Get the current z-order value.
	[[nodiscard]] std::uint32_t zOrder() const noexcept { return m_zOrder; }

	/// @brief Get the title string.
	[[nodiscard]] const std::string& title() const noexcept { return m_title; }

	/// @brief Get the content area bounds (inside title bar and padding).
	[[nodiscard]] sgc::Rectf contentBounds() const noexcept
	{
		if (m_minimized) { return sgc::Rectf(0.0f, 0.0f, 0.0f, 0.0f); }
		const float x = m_posX + m_borderWidth + m_padding;
		const float y = m_posY + m_titleBarHeight + m_padding;
		const float w = m_width - 2.0f * (m_borderWidth + m_padding);
		const float h = m_height - m_titleBarHeight - m_borderWidth - 2.0f * m_padding;
		return sgc::Rectf(x, y, std::max(0.0f, w), std::max(0.0f, h));
	}

	/// @brief Get the title bar bounds.
	[[nodiscard]] sgc::Rectf titleBarBounds() const noexcept
	{
		return sgc::Rectf(m_posX, m_posY, m_width, m_titleBarHeight);
	}

	// ── Child management ─────────────────────────────────────

	/// @brief Add a child widget node to the content area.
	/// @param child The child UINode.
	void addChild(std::shared_ptr<UINode> child)
	{
		m_children.push_back(std::move(child));
	}

	/// @brief Get all child widget nodes.
	[[nodiscard]] const std::vector<std::shared_ptr<UINode>>& children() const noexcept
	{
		return m_children;
	}

	// ── Commands ─────────────────────────────────────────────

	/// @brief Open the window.
	void open()
	{
		m_open = true;
		m_minimized = false;
		syncNodeState();
	}

	/// @brief Close the window.
	void close()
	{
		m_open = false;
		m_dragging = false;
		m_resizing = false;
		syncNodeState();
		if (m_onClose) { m_onClose(); }
	}

	/// @brief Minimize the window to title bar only.
	void minimize()
	{
		if (!m_minimizable) { return; }
		m_minimized = true;
		m_resizing = false;
		syncNodeState();
		if (m_onMinimize) { m_onMinimize(); }
	}

	/// @brief Restore the window from minimized state.
	void restore()
	{
		m_minimized = false;
		syncNodeState();
		if (m_onRestore) { m_onRestore(); }
	}

	/// @brief Set the window title.
	/// @param title New title string.
	void setTitle(const std::string& title)
	{
		m_title = title;
		m_node->setText(title);
	}

	/// @brief Set the z-order value.
	/// @param z New z-order.
	void setZOrder(std::uint32_t z)
	{
		m_zOrder = z;
		m_node->setProperty("z_order", std::to_string(z));
	}

	/// @brief Request bringing this window to front (invokes callback).
	void bringToFront()
	{
		if (m_onBringToFront) { m_onBringToFront(m_node->id()); }
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief Set callback invoked when the window is closed.
	void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }

	/// @brief Set callback invoked when the window is minimized.
	void setOnMinimize(std::function<void()> callback) { m_onMinimize = std::move(callback); }

	/// @brief Set callback invoked when the window is restored.
	void setOnRestore(std::function<void()> callback) { m_onRestore = std::move(callback); }

	/// @brief Set callback to request z-order change (pass node id).
	void setOnBringToFront(std::function<void(std::uint32_t)> callback)
	{
		m_onBringToFront = std::move(callback);
	}

	// ── Interaction (called by event system) ─────────────────

	/// @brief Called when pointer is pressed on the window.
	/// @param px Pointer X in screen space.
	/// @param py Pointer Y in screen space.
	void onPointerDown(float px, float py)
	{
		if (!m_open) { return; }
		bringToFront();

		// Check close button (top-right corner of title bar)
		if (m_closeable && hitTestCloseButton(px, py))
		{
			close();
			return;
		}

		// Check minimize button (next to close)
		if (m_minimizable && hitTestMinimizeButton(px, py))
		{
			if (m_minimized) { restore(); } else { minimize(); }
			return;
		}

		// Check resize edges
		if (m_resizable && !m_minimized)
		{
			const auto edge = hitTestResizeEdge(px, py);
			if (edge != WindowResizeEdge::None)
			{
				m_resizing = true;
				m_resizeEdge = edge;
				m_dragOffsetX = px;
				m_dragOffsetY = py;
				return;
			}
		}

		// Check title bar drag
		if (m_draggable && hitTestTitleBar(px, py))
		{
			m_dragging = true;
			m_dragOffsetX = px - m_posX;
			m_dragOffsetY = py - m_posY;
		}
	}

	/// @brief Called when pointer moves while pressed.
	/// @param px Pointer X in screen space.
	/// @param py Pointer Y in screen space.
	void onPointerMove(float px, float py)
	{
		if (m_dragging)
		{
			m_posX = px - m_dragOffsetX;
			m_posY = py - m_dragOffsetY;
			syncBounds();
			return;
		}

		if (m_resizing)
		{
			applyResize(px, py);
			return;
		}
	}

	/// @brief Called when pointer is released.
	void onPointerUp()
	{
		m_dragging = false;
		m_resizing = false;
		m_resizeEdge = WindowResizeEdge::None;
	}

private:
	/// @brief Check if point is inside the title bar.
	[[nodiscard]] bool hitTestTitleBar(float px, float py) const noexcept
	{
		return px >= m_posX && px <= m_posX + m_width
			&& py >= m_posY && py <= m_posY + m_titleBarHeight;
	}

	/// @brief Check if point is on the close button (right side of title bar).
	[[nodiscard]] bool hitTestCloseButton(float px, float py) const noexcept
	{
		const float btnSize = m_titleBarHeight;
		const float btnX = m_posX + m_width - btnSize;
		return px >= btnX && px <= btnX + btnSize
			&& py >= m_posY && py <= m_posY + btnSize;
	}

	/// @brief Check if point is on the minimize button (left of close).
	[[nodiscard]] bool hitTestMinimizeButton(float px, float py) const noexcept
	{
		const float btnSize = m_titleBarHeight;
		const float offset = m_closeable ? btnSize : 0.0f;
		const float btnX = m_posX + m_width - offset - btnSize;
		return px >= btnX && px <= btnX + btnSize
			&& py >= m_posY && py <= m_posY + btnSize;
	}

	/// @brief Determine which resize edge/corner the point is on.
	[[nodiscard]] WindowResizeEdge hitTestResizeEdge(float px, float py) const noexcept
	{
		const float grab = std::max(m_borderWidth, 6.0f);
		const bool onLeft = px >= m_posX - grab && px <= m_posX + grab;
		const bool onRight = px >= m_posX + m_width - grab && px <= m_posX + m_width + grab;
		const bool onTop = py >= m_posY - grab && py <= m_posY + grab;
		const bool onBottom = py >= m_posY + m_height - grab && py <= m_posY + m_height + grab;

		if (onTop && onLeft) { return WindowResizeEdge::TopLeft; }
		if (onTop && onRight) { return WindowResizeEdge::TopRight; }
		if (onBottom && onLeft) { return WindowResizeEdge::BottomLeft; }
		if (onBottom && onRight) { return WindowResizeEdge::BottomRight; }
		if (onLeft) { return WindowResizeEdge::Left; }
		if (onRight) { return WindowResizeEdge::Right; }
		if (onTop) { return WindowResizeEdge::Top; }
		if (onBottom) { return WindowResizeEdge::Bottom; }
		return WindowResizeEdge::None;
	}

	/// @brief Apply resize delta from pointer position.
	void applyResize(float px, float py)
	{
		const float dx = px - m_dragOffsetX;
		const float dy = py - m_dragOffsetY;
		m_dragOffsetX = px;
		m_dragOffsetY = py;

		float newX = m_posX;
		float newY = m_posY;
		float newW = m_width;
		float newH = m_height;

		switch (m_resizeEdge)
		{
		case WindowResizeEdge::Right:       newW += dx; break;
		case WindowResizeEdge::Bottom:      newH += dy; break;
		case WindowResizeEdge::Left:        newX += dx; newW -= dx; break;
		case WindowResizeEdge::Top:         newY += dy; newH -= dy; break;
		case WindowResizeEdge::TopLeft:     newX += dx; newW -= dx; newY += dy; newH -= dy; break;
		case WindowResizeEdge::TopRight:    newW += dx; newY += dy; newH -= dy; break;
		case WindowResizeEdge::BottomLeft:  newX += dx; newW -= dx; newH += dy; break;
		case WindowResizeEdge::BottomRight: newW += dx; newH += dy; break;
		default: break;
		}

		// Clamp to min/max
		newW = std::clamp(newW, m_minWidth, m_maxWidth);
		newH = std::clamp(newH, m_minHeight, m_maxHeight);

		// If clamped, don't move origin for left/top edges
		if (m_resizeEdge == WindowResizeEdge::Left || m_resizeEdge == WindowResizeEdge::TopLeft
			|| m_resizeEdge == WindowResizeEdge::BottomLeft)
		{
			newX = m_posX + (m_width - newW);
		}
		if (m_resizeEdge == WindowResizeEdge::Top || m_resizeEdge == WindowResizeEdge::TopLeft
			|| m_resizeEdge == WindowResizeEdge::TopRight)
		{
			newY = m_posY + (m_height - newH);
		}

		m_posX = newX;
		m_posY = newY;
		m_width = newW;
		m_height = newH;
		syncBounds();
	}

	/// @brief Update node bounds from current position/size.
	void syncBounds()
	{
		const float h = m_minimized ? m_titleBarHeight : m_height;
		m_node->setBounds(sgc::Rectf(m_posX, m_posY, m_width, h));
	}

	/// @brief Synchronize all state to the UINode properties.
	void syncNodeState()
	{
		m_node->setProperty("open", m_open ? "true" : "false");
		m_node->setProperty("minimized", m_minimized ? "true" : "false");
		m_node->setProperty("resizable", m_resizable ? "true" : "false");
		m_node->setProperty("closeable", m_closeable ? "true" : "false");
		m_node->setProperty("minimizable", m_minimizable ? "true" : "false");
		m_node->setProperty("draggable", m_draggable ? "true" : "false");
		m_node->setVisible(m_open);
		syncBounds();
	}
};

} // namespace mitiru::ui
