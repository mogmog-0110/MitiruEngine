#pragma once

/// @file UIButton.hpp
/// @brief normal/hover/pressed/disabled 状態と toggle モードを持つクリック可能 button widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief button の表示 / 操作状態。
enum class ButtonState : std::uint8_t
{
	Normal,
	Hovered,
	Pressed,
	Disabled
};

/// @brief UIButton 生成用の設定。
struct UIButtonConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string text;
	float width = 120.0f;
	float height = 32.0f;
	bool enabled = true;
	bool toggleable = false;
};

/// @brief press/release/click ロジックで UINode をラップする button widget。
///
/// button は role Button の UINode を構成し、操作の状態遷移を管理する。
/// 描画は外部の UIRenderer が担当する。
///
/// @code
///   UIButtonConfig cfg;
///   cfg.id = 10;
///   cfg.text = "Start";
///   cfg.toggleable = true;
///   UIButton btn(cfg);
///
///   btn.setOnClick([] { /* handle click */ });
///   btn.onPointerEnter();
///   btn.onPointerDown();
///   btn.onPointerUp();  // triggers onClick
/// @endcode
class UIButton
{
	std::shared_ptr<UINode> m_node;
	ButtonState m_state = ButtonState::Normal;
	bool m_enabled = true;
	bool m_toggleable = false;
	bool m_toggled = false;
	bool m_pointerInside = false;
	std::function<void()> m_onClick;

public:
	/// @brief 設定から button を構築する。
	/// @param config button の設定。
	explicit UIButton(const UIButtonConfig& config)
		: m_enabled(config.enabled)
		, m_toggleable(config.toggleable)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Button;
		data.text = config.text;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "button";
		data.properties["toggleable"] = m_toggleable ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の button 状態を取得する。
	[[nodiscard]] ButtonState state() const noexcept { return m_state; }

	/// @brief button が有効か判定する。
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

	/// @brief button が現在 toggle ON か判定する (toggleable 時のみ意味を持つ)。
	[[nodiscard]] bool isToggled() const noexcept { return m_toggled; }

	/// @brief button のテキストを取得する。
	[[nodiscard]] const std::string& text() const noexcept { return m_node->text(); }

	// ── 設定 ────────────────────────────────────────

	/// @brief click コールバックを設定する。
	/// @param callback click 時に呼ばれる関数。
	void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

	/// @brief button の有効 / 無効を設定する。
	/// @param enabled 有効にするなら true。
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		if (!m_enabled)
		{
			m_state = ButtonState::Disabled;
		}
		else if (m_state == ButtonState::Disabled)
		{
			m_state = ButtonState::Normal;
		}
		syncNodeState();
	}

	/// @brief button の表示テキストを設定する。
	/// @param text 新しいテキスト。
	void setText(const std::string& text)
	{
		m_node->setText(text);
	}

	// ── 操作 (event system から呼ばれる) ─────────────────

	/// @brief pointer が button 領域に入ったときに呼ばれる。
	void onPointerEnter()
	{
		m_pointerInside = true;
		if (!m_enabled) { return; }
		if (m_state != ButtonState::Pressed)
		{
			m_state = ButtonState::Hovered;
			syncNodeState();
		}
	}

	/// @brief pointer が button 領域から出たときに呼ばれる。
	void onPointerLeave()
	{
		m_pointerInside = false;
		if (!m_enabled) { return; }
		if (m_state != ButtonState::Pressed || !m_toggleable || !m_toggled)
		{
			m_state = ButtonState::Normal;
			syncNodeState();
		}
	}

	/// @brief button 上で pointer が押されたときに呼ばれる。
	void onPointerDown()
	{
		if (!m_enabled) { return; }
		m_state = ButtonState::Pressed;
		syncNodeState();
	}

	/// @brief pointer が離されたときに呼ばれる。
	void onPointerUp()
	{
		if (!m_enabled) { return; }
		if (m_state == ButtonState::Pressed && m_pointerInside)
		{
			if (m_toggleable)
			{
				m_toggled = !m_toggled;
			}

			if (m_onClick) { m_onClick(); }
		}

		if (m_pointerInside)
		{
			m_state = (m_toggleable && m_toggled) ? ButtonState::Pressed : ButtonState::Hovered;
		}
		else
		{
			m_state = ButtonState::Normal;
		}
		syncNodeState();
	}

private:
	/// @brief 操作状態を UINode の properties に同期する。
	void syncNodeState()
	{
		const char* stateStr = "normal";
		switch (m_state)
		{
		case ButtonState::Hovered:  stateStr = "hovered";  break;
		case ButtonState::Pressed:  stateStr = "pressed";  break;
		case ButtonState::Disabled: stateStr = "disabled"; break;
		default: break;
		}
		m_node->setProperty("state", stateStr);
		m_node->setProperty("toggled", m_toggled ? "true" : "false");
		m_node->setProperty("enabled", m_enabled ? "true" : "false");
	}
};

} // namespace mitiru::ui
