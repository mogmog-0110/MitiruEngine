#pragma once

/// @file UIDraggableWindow.hpp
/// @brief title bar / close / minimize を持つ、drag・resize 可能な window/panel widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief UIDraggableWindow 生成用の設定。
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

/// @brief resize の hit-test 用の edge/corner 領域。
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

/// @brief drag・resize 可能な window widget。
///
/// title bar、任意の close/minimize button、drag 移動、edge/corner resize を持つ
/// panel を管理する。content 領域は子 widget を受け付ける。
/// Z-order は外部で管理する。並び替え要求には bringToFront() を呼ぶ。
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
	/// @brief 設定から draggable window を構築する。
	/// @param config window 設定。
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

	// ── accessor ──────────────────────────────────────────────

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief window が open か確認する。
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief window が minimize されているか確認する。
	[[nodiscard]] bool isMinimized() const noexcept { return m_minimized; }

	/// @brief window が drag 中か確認する。
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief window が resize 中か確認する。
	[[nodiscard]] bool isResizing() const noexcept { return m_resizing; }

	/// @brief window の位置 X を取得する。
	[[nodiscard]] float posX() const noexcept { return m_posX; }

	/// @brief window の位置 Y を取得する。
	[[nodiscard]] float posY() const noexcept { return m_posY; }

	/// @brief window の幅を取得する。
	[[nodiscard]] float width() const noexcept { return m_width; }

	/// @brief window の高さを取得する。
	[[nodiscard]] float height() const noexcept { return m_height; }

	/// @brief 現在の z-order 値を取得する。
	[[nodiscard]] std::uint32_t zOrder() const noexcept { return m_zOrder; }

	/// @brief title 文字列を取得する。
	[[nodiscard]] const std::string& title() const noexcept { return m_title; }

	/// @brief content 領域の bounds を取得する (title bar と padding の内側)。
	[[nodiscard]] sgc::Rectf contentBounds() const noexcept
	{
		if (m_minimized) { return sgc::Rectf(0.0f, 0.0f, 0.0f, 0.0f); }
		const float x = m_posX + m_borderWidth + m_padding;
		const float y = m_posY + m_titleBarHeight + m_padding;
		const float w = m_width - 2.0f * (m_borderWidth + m_padding);
		const float h = m_height - m_titleBarHeight - m_borderWidth - 2.0f * m_padding;
		return sgc::Rectf(x, y, std::max(0.0f, w), std::max(0.0f, h));
	}

	/// @brief title bar の bounds を取得する。
	[[nodiscard]] sgc::Rectf titleBarBounds() const noexcept
	{
		return sgc::Rectf(m_posX, m_posY, m_width, m_titleBarHeight);
	}

	// ── 子の管理 ──────────────────────────────────────────────

	/// @brief content 領域に子 widget node を追加する。
	/// @param child 子の UINode。
	void addChild(std::shared_ptr<UINode> child)
	{
		m_children.push_back(std::move(child));
	}

	/// @brief 全ての子 widget node を取得する。
	[[nodiscard]] const std::vector<std::shared_ptr<UINode>>& children() const noexcept
	{
		return m_children;
	}

	// ── command ───────────────────────────────────────────────

	/// @brief window を開く。
	void open()
	{
		m_open = true;
		m_minimized = false;
		syncNodeState();
	}

	/// @brief window を閉じる。
	void close()
	{
		m_open = false;
		m_dragging = false;
		m_resizing = false;
		syncNodeState();
		if (m_onClose) { m_onClose(); }
	}

	/// @brief window を title bar のみに minimize する。
	void minimize()
	{
		if (!m_minimizable) { return; }
		m_minimized = true;
		m_resizing = false;
		syncNodeState();
		if (m_onMinimize) { m_onMinimize(); }
	}

	/// @brief minimize 状態から window を restore する。
	void restore()
	{
		m_minimized = false;
		syncNodeState();
		if (m_onRestore) { m_onRestore(); }
	}

	/// @brief window の title を設定する。
	/// @param title 新しい title 文字列。
	void setTitle(const std::string& title)
	{
		m_title = title;
		m_node->setText(title);
	}

	/// @brief z-order 値を設定する。
	/// @param z 新しい z-order。
	void setZOrder(std::uint32_t z)
	{
		m_zOrder = z;
		m_node->setProperty("z_order", std::to_string(z));
	}

	/// @brief この window を前面に出すよう要求する (callback を呼ぶ)。
	void bringToFront()
	{
		if (m_onBringToFront) { m_onBringToFront(m_node->id()); }
	}

	// ── callback ──────────────────────────────────────────────

	/// @brief window が閉じられたときに呼ばれる callback を設定する。
	void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }

	/// @brief window が minimize されたときに呼ばれる callback を設定する。
	void setOnMinimize(std::function<void()> callback) { m_onMinimize = std::move(callback); }

	/// @brief window が restore されたときに呼ばれる callback を設定する。
	void setOnRestore(std::function<void()> callback) { m_onRestore = std::move(callback); }

	/// @brief z-order 変更を要求する callback を設定する (node id を渡す)。
	void setOnBringToFront(std::function<void(std::uint32_t)> callback)
	{
		m_onBringToFront = std::move(callback);
	}

	// ── 操作 (event system から呼ばれる) ──────────────────────

	/// @brief window 上で pointer が押されたときに呼ばれる。
	/// @param px screen 空間での pointer X。
	/// @param py screen 空間での pointer Y。
	void onPointerDown(float px, float py)
	{
		if (!m_open) { return; }
		bringToFront();

		// close button を判定 (title bar の右上 corner)
		if (m_closeable && hitTestCloseButton(px, py))
		{
			close();
			return;
		}

		// minimize button を判定 (close の隣)
		if (m_minimizable && hitTestMinimizeButton(px, py))
		{
			if (m_minimized) { restore(); } else { minimize(); }
			return;
		}

		// resize edge を判定
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

		// title bar の drag を判定
		if (m_draggable && hitTestTitleBar(px, py))
		{
			m_dragging = true;
			m_dragOffsetX = px - m_posX;
			m_dragOffsetY = py - m_posY;
		}
	}

	/// @brief 押下中に pointer が移動したときに呼ばれる。
	/// @param px screen 空間での pointer X。
	/// @param py screen 空間での pointer Y。
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

	/// @brief pointer が離されたときに呼ばれる。
	void onPointerUp()
	{
		m_dragging = false;
		m_resizing = false;
		m_resizeEdge = WindowResizeEdge::None;
	}

private:
	/// @brief 点が title bar の内側にあるか確認する。
	[[nodiscard]] bool hitTestTitleBar(float px, float py) const noexcept
	{
		return px >= m_posX && px <= m_posX + m_width
			&& py >= m_posY && py <= m_posY + m_titleBarHeight;
	}

	/// @brief 点が close button 上にあるか確認する (title bar の右側)。
	[[nodiscard]] bool hitTestCloseButton(float px, float py) const noexcept
	{
		const float btnSize = m_titleBarHeight;
		const float btnX = m_posX + m_width - btnSize;
		return px >= btnX && px <= btnX + btnSize
			&& py >= m_posY && py <= m_posY + btnSize;
	}

	/// @brief 点が minimize button 上にあるか確認する (close の左)。
	[[nodiscard]] bool hitTestMinimizeButton(float px, float py) const noexcept
	{
		const float btnSize = m_titleBarHeight;
		const float offset = m_closeable ? btnSize : 0.0f;
		const float btnX = m_posX + m_width - offset - btnSize;
		return px >= btnX && px <= btnX + btnSize
			&& py >= m_posY && py <= m_posY + btnSize;
	}

	/// @brief 点がどの resize edge/corner 上にあるか判定する。
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

	/// @brief pointer 位置から resize の delta を適用する。
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

		// min/max に clamp する
		newW = std::clamp(newW, m_minWidth, m_maxWidth);
		newH = std::clamp(newH, m_minHeight, m_maxHeight);

		// clamp された場合、left/top edge では origin を動かさない
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

	/// @brief 現在の位置/サイズから node の bounds を更新する。
	void syncBounds()
	{
		const float h = m_minimized ? m_titleBarHeight : m_height;
		m_node->setBounds(sgc::Rectf(m_posX, m_posY, m_width, h));
	}

	/// @brief 全 state を UINode の properties に同期する。
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
