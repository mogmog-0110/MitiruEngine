#pragma once

/// @file UIToggle.hpp
/// @brief 真偽値用の toggle (checkbox / switch) widget。

#include <mitiru/ui/UINode.hpp>

#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief toggle widget の表示スタイル。
enum class ToggleStyle : std::uint8_t
{
	Checkbox,  ///< チェックマーク付きの四角い checkbox。
	Switch     ///< スライドする switch (pill 形状)。
};

/// @brief UIToggle 生成用の設定。
struct UIToggleConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string label;
	bool checked = false;
	bool enabled = true;
	ToggleStyle style = ToggleStyle::Checkbox;
	float width = 0.0f;   ///< 0 = label から自動サイズ。
	float height = 24.0f;
};

/// @brief click-to-toggle ロジックで UINode をラップする toggle widget。
///
/// toggle は真偽値の checked 状態を管理し、callback で通知する。
/// toggle インジケータの子と label の子を持つコンテナ node を生成する。
///
/// @code
///   UIToggleConfig cfg;
///   cfg.id = 30;
///   cfg.label = "Show FPS";
///   cfg.checked = true;
///   UIToggle toggle(cfg);
///
///   toggle.setOnChanged([](bool checked) {
///       /* handle state change */
///   });
///   toggle.onClick();  // flips checked state
/// @endcode
class UIToggle
{
	std::shared_ptr<UINode> m_node;
	bool m_checked;
	bool m_enabled;
	ToggleStyle m_style;
	std::function<void(bool)> m_onChanged;

public:
	/// @brief 設定から toggle を構築する。
	/// @param config toggle の設定。
	explicit UIToggle(const UIToggleConfig& config)
		: m_checked(config.checked)
		, m_enabled(config.enabled)
		, m_style(config.style)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Toggle;
		data.text = config.label;
		data.value = m_checked ? 1.0f : 0.0f;
		data.bounds = sgc::Rectf(0.0f, 0.0f,
			config.width > 0.0f ? config.width : 200.0f,
			config.height);
		data.properties["widget_type"] = "toggle";
		data.properties["toggle_style"] = (m_style == ToggleStyle::Checkbox) ? "checkbox" : "switch";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在 toggle ON か判定する。
	[[nodiscard]] bool isChecked() const noexcept { return m_checked; }

	/// @brief toggle が有効か判定する。
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

	/// @brief label テキストを取得する。
	[[nodiscard]] const std::string& label() const noexcept { return m_node->text(); }

	// ── 設定 ────────────────────────────────────────

	/// @brief 状態変化 callback を設定する。
	/// @param callback 新しい checked 状態を引数に呼ばれる関数。
	void setOnChanged(std::function<void(bool)> callback) { m_onChanged = std::move(callback); }

	/// @brief checked 状態をプログラムから設定する。
	/// @param checked 新しい checked 状態。
	void setChecked(bool checked)
	{
		if (m_checked != checked)
		{
			m_checked = checked;
			syncNodeState();
			if (m_onChanged) { m_onChanged(m_checked); }
		}
	}

	/// @brief label テキストを設定する。
	/// @param label 新しい label。
	void setLabel(const std::string& label)
	{
		m_node->setText(label);
	}

	/// @brief 有効状態を設定する。
	/// @param enabled 操作を有効にするなら true。
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		syncNodeState();
	}

	// ── 操作 ──────────────────────────────────────────

	/// @brief toggle が click されたときに呼ばれる。
	void onClick()
	{
		if (!m_enabled) { return; }
		m_checked = !m_checked;
		syncNodeState();
		if (m_onChanged) { m_onChanged(m_checked); }
	}

private:
	/// @brief 状態を UINode へ同期する。
	void syncNodeState()
	{
		m_node->setValue(m_checked ? 1.0f : 0.0f);
		m_node->setProperty("checked", m_checked ? "true" : "false");
		m_node->setProperty("enabled", m_enabled ? "true" : "false");
	}
};

} // namespace mitiru::ui
