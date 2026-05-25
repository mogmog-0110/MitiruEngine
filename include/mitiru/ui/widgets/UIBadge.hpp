#pragma once

/// @file UIBadge.hpp
/// @brief UI 要素に重ねる小型の counter / notification badge。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <string>

namespace mitiru::ui {

/// @brief 親要素に対する badge の anchor 位置。
enum class BadgePosition : std::uint8_t
{
	TopRight,
	TopLeft,
	BottomRight,
	BottomLeft
};

/// @brief UIBadge 生成用の設定。
struct UIBadgeConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	int maxCount = 99;                     ///< この値を超えると "N+" 表示。
	float fontSize = 11.0f;                ///< badge テキストの font size。
	float minSize = 20.0f;                 ///< badge の最小直径。
	float padding = 4.0f;                  ///< badge 内の水平 padding。
	std::string backgroundImageKey;        ///< badge 背景画像のキー。
	std::string textColor = "ffffff";       ///< badge テキスト色 (hex、# なし)。
	std::string backgroundColor = "ff0000"; ///< badge 背景色 (hex、# なし)。
	BadgePosition position = BadgePosition::TopRight;  ///< anchor 位置。
	float offsetX = 0.0f;                  ///< anchor からの水平オフセット。
	float offsetY = 0.0f;                  ///< anchor からの垂直オフセット。
	bool pulseAnimation = true;            ///< count 変化時に pulse する。
	bool hideWhenZero = true;              ///< count が 0 のとき badge を隠す。
	float pulseDuration = 0.4f;            ///< pulse animation の長さ (秒)。
	float pulseScale = 1.3f;               ///< pulse 中の最大スケール。
};

/// @brief 通知件数を表す小型 counter badge。
///
/// 数字付きの円または角丸矩形を表示し、通常は別の UI 要素に付随させる。
/// maxCount を超える count は "N+" と表示する。count 変化時には pulse
/// animation を提供する。
///
/// @code
///   UIBadgeConfig cfg;
///   cfg.id = 400;
///   cfg.name = "inbox_badge";
///   cfg.maxCount = 99;
///   cfg.position = BadgePosition::TopRight;
///   UIBadge badge(cfg);
///
///   badge.setCount(5);
///   badge.update(dt);
/// @endcode
class UIBadge
{
	std::shared_ptr<UINode> m_node;
	int m_count = 0;
	int m_maxCount;
	float m_minSize;
	float m_padding;
	BadgePosition m_position;
	float m_offsetX;
	float m_offsetY;
	bool m_pulseAnimation;
	bool m_hideWhenZero;
	bool m_visible = true;

	// pulse animation の状態。
	float m_pulseDuration;
	float m_pulseScale;
	float m_pulseTimer = 0.0f;
	bool m_pulsing = false;

public:
	/// @brief 設定から badge を構築する。
	/// @param config badge の設定。
	explicit UIBadge(const UIBadgeConfig& config)
		: m_maxCount(config.maxCount)
		, m_minSize(config.minSize)
		, m_padding(config.padding)
		, m_position(config.position)
		, m_offsetX(config.offsetX)
		, m_offsetY(config.offsetY)
		, m_pulseAnimation(config.pulseAnimation)
		, m_hideWhenZero(config.hideWhenZero)
		, m_pulseDuration(config.pulseDuration)
		, m_pulseScale(config.pulseScale)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Label;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.minSize, config.minSize);
		data.properties["widget_type"] = "badge";
		data.properties["max_count"] = std::to_string(config.maxCount);
		data.properties["font_size"] = std::to_string(config.fontSize);
		data.properties["min_size"] = std::to_string(config.minSize);
		data.properties["padding"] = std::to_string(config.padding);
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["text_color"] = config.textColor;
		data.properties["background_color"] = config.backgroundColor;
		data.properties["position"] = positionToString(config.position);
		data.properties["offset_x"] = std::to_string(config.offsetX);
		data.properties["offset_y"] = std::to_string(config.offsetY);
		data.properties["pulse_animation"] = config.pulseAnimation ? "true" : "false";
		data.properties["hide_when_zero"] = config.hideWhenZero ? "true" : "false";
		data.properties["pulse_duration"] = std::to_string(config.pulseDuration);
		data.properties["pulse_scale"] = std::to_string(config.pulseScale);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の count を取得する。
	[[nodiscard]] int getCount() const noexcept { return m_count; }

	/// @brief badge が表示中か判定する。
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief pulse animation が動作中か判定する。
	[[nodiscard]] bool isPulsing() const noexcept { return m_pulsing; }

	/// @brief 表示テキストを取得する (例: "5" や "99+")。
	[[nodiscard]] std::string displayText() const
	{
		if (m_count <= 0) { return "0"; }
		if (m_count > m_maxCount)
		{
			return std::to_string(m_maxCount) + "+";
		}
		return std::to_string(m_count);
	}

	// -- Setters -------------------------------------------------------------

	/// @brief badge の count を設定する。
	/// @param count 新しい count 値。
	void setCount(int count)
	{
		const int newCount = std::max(0, count);
		if (newCount == m_count) { return; }

		const bool increased = (newCount > m_count);
		m_count = newCount;

		// count 増加時に pulse を発火する。
		if (increased && m_pulseAnimation)
		{
			m_pulsing = true;
			m_pulseTimer = 0.0f;
		}

		// 表示状態を更新する。
		if (m_hideWhenZero)
		{
			m_visible = (m_count > 0);
		}

		syncNodeState();
	}

	/// @brief badge を表示する。
	void show()
	{
		m_visible = true;
		syncNodeState();
	}

	/// @brief badge を隠す。
	void hide()
	{
		m_visible = false;
		syncNodeState();
	}

	// -- Update --------------------------------------------------------------

	/// @brief pulse animation を更新する。
	/// @param dt フレーム間の経過時間 (秒)。
	void update(float dt)
	{
		if (!m_pulsing) { return; }

		m_pulseTimer += dt;
		if (m_pulseTimer >= m_pulseDuration)
		{
			m_pulsing = false;
			m_pulseTimer = 0.0f;
			m_node->setProperty("pulse_progress", "0");
			m_node->setProperty("current_scale", "1");
			return;
		}

		const float progress = m_pulseTimer / m_pulseDuration;
		// sin 波: 拡大してから元に戻る。
		const float scaleFactor = 1.0f +
			(m_pulseScale - 1.0f) * std::sin(progress * std::numbers::pi_v<float>);

		m_node->setProperty("pulse_progress", std::to_string(progress));
		m_node->setProperty("current_scale", std::to_string(scaleFactor));
	}

private:
	/// @brief position enum を文字列へ変換する。
	[[nodiscard]] static const char* positionToString(BadgePosition pos) noexcept
	{
		switch (pos)
		{
		case BadgePosition::TopRight:    return "top_right";
		case BadgePosition::TopLeft:     return "top_left";
		case BadgePosition::BottomRight: return "bottom_right";
		case BadgePosition::BottomLeft:  return "bottom_left";
		}
		return "top_right";
	}

	/// @brief 状態を UINode へ同期する。
	void syncNodeState()
	{
		m_node->setText(displayText());
		m_node->setVisible(m_visible);
		m_node->setProperty("count", std::to_string(m_count));
	}
};

} // namespace mitiru::ui
