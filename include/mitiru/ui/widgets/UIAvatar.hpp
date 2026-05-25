#pragma once

/// @file UIAvatar.hpp
/// @brief status indicator と fallback initials を持つ円形 avatar 画像表示。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief avatar 表示用のオンライン status。
enum class AvatarStatus : std::uint8_t
{
	None,
	Online,
	Offline,
	Away,
	Busy
};

/// @brief UIAvatar 生成用の設定。
struct UIAvatarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float size = 48.0f;                    ///< avatar 円の直径。
	std::string imageKey;                  ///< avatar 画像の image key。
	std::string fallbackText;              ///< 画像未設定時の initials text。
	float borderWidth = 2.0f;              ///< 円周りの border の太さ。
	std::string borderColor = "cccccc";    ///< border 色 (hex、# なし)。
	std::string borderImageKey;            ///< border の image key (設定時は borderColor を上書き)。
	AvatarStatus statusIndicator = AvatarStatus::None;  ///< 初期 status。
	std::string statusColor = "00ff00";    ///< status dot の色 (hex、# なし)。
	float statusSize = 12.0f;              ///< status dot の直径。
	std::string statusPosition = "bottom_right"; ///< status dot を出す位置。
	std::string backgroundImageKey;        ///< 円の背景画像。
	bool clickable = false;                ///< avatar の click で callback を起動するか。
	float fallbackFontSize = 18.0f;        ///< fallback initials の font size。
	std::string fallbackBackgroundColor = "888888"; ///< initials fallback の背景色。
};

/// @brief 画像・status indicator・fallback initials を持つ円形 avatar widget。
///
/// 円形に切り抜いた画像を表示する。画像未設定なら、initials 入りの
/// 色付き円を表示する。任意で status dot の overlay (Online/Offline/Away/Busy)
/// を出す。
///
/// @code
///   UIAvatarConfig cfg;
///   cfg.id = 500;
///   cfg.name = "player_avatar";
///   cfg.size = 64.0f;
///   cfg.imageKey = "player_portrait";
///   cfg.statusIndicator = AvatarStatus::Online;
///   UIAvatar avatar(cfg);
///
///   avatar.setOnClick([] { /* show profile */ });
///   avatar.setStatus(AvatarStatus::Away);
/// @endcode
class UIAvatar
{
	std::shared_ptr<UINode> m_node;
	std::string m_imageKey;
	std::string m_fallbackText;
	AvatarStatus m_status;
	bool m_clickable;
	bool m_hovered = false;
	std::function<void()> m_onClick;

public:
	/// @brief 設定から avatar を構築する。
	/// @param config avatar の設定。
	explicit UIAvatar(const UIAvatarConfig& config)
		: m_imageKey(config.imageKey)
		, m_fallbackText(config.fallbackText)
		, m_status(config.statusIndicator)
		, m_clickable(config.clickable)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Image;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.size, config.size);
		data.text = config.fallbackText;
		data.properties["widget_type"] = "avatar";
		data.properties["size"] = std::to_string(config.size);
		data.properties["image_key"] = config.imageKey;
		data.properties["fallback_text"] = config.fallbackText;
		data.properties["border_width"] = std::to_string(config.borderWidth);
		data.properties["border_color"] = config.borderColor;
		data.properties["border_image"] = config.borderImageKey;
		data.properties["status_color"] = config.statusColor;
		data.properties["status_size"] = std::to_string(config.statusSize);
		data.properties["status_position"] = config.statusPosition;
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["clickable"] = config.clickable ? "true" : "false";
		data.properties["fallback_font_size"] = std::to_string(config.fallbackFontSize);
		data.properties["fallback_bg_color"] = config.fallbackBackgroundColor;

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の image key を取得する。
	[[nodiscard]] const std::string& imageKey() const noexcept { return m_imageKey; }

	/// @brief fallback text (initials) を取得する。
	[[nodiscard]] const std::string& fallbackText() const noexcept { return m_fallbackText; }

	/// @brief 現在の status を取得する。
	[[nodiscard]] AvatarStatus status() const noexcept { return m_status; }

	/// @brief avatar に画像が設定されているか判定する。
	[[nodiscard]] bool hasImage() const noexcept { return !m_imageKey.empty(); }

	/// @brief avatar が click 可能か判定する。
	[[nodiscard]] bool isClickable() const noexcept { return m_clickable; }

	// -- 設定 -------------------------------------------------------

	/// @brief click 時の callback を設定する。
	/// @param callback click 時に呼ばれる関数。
	void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

	// -- setter -------------------------------------------------------------

	/// @brief avatar 画像を設定する。
	/// @param key avatar 画像の image key。
	void setImage(const std::string& key)
	{
		m_imageKey = key;
		m_node->setProperty("image_key", key);
		syncNodeState();
	}

	/// @brief オンライン status を設定する。
	/// @param newStatus 新しい status 値。
	void setStatus(AvatarStatus newStatus)
	{
		m_status = newStatus;
		syncNodeState();
	}

	/// @brief fallback text (initials) を設定する。
	/// @param text 画像未設定時に表示する initials。
	void setFallbackText(const std::string& text)
	{
		m_fallbackText = text;
		m_node->setText(text);
		m_node->setProperty("fallback_text", text);
		syncNodeState();
	}

	// -- 操作 ---------------------------------------------------------

	/// @brief pointer が avatar 領域に入ったときに呼ばれる。
	void onPointerEnter()
	{
		m_hovered = true;
		m_node->setProperty("hovered", "true");
	}

	/// @brief pointer が avatar 領域から離れたときに呼ばれる。
	void onPointerLeave()
	{
		m_hovered = false;
		m_node->setProperty("hovered", "false");
	}

	/// @brief avatar が click されたときに呼ばれる。
	void onPointerUp()
	{
		if (m_clickable && m_hovered && m_onClick)
		{
			m_onClick();
		}
	}

private:
	/// @brief status enum を文字列に変換する。
	[[nodiscard]] static const char* statusToString(AvatarStatus s) noexcept
	{
		switch (s)
		{
		case AvatarStatus::None:    return "none";
		case AvatarStatus::Online:  return "online";
		case AvatarStatus::Offline: return "offline";
		case AvatarStatus::Away:    return "away";
		case AvatarStatus::Busy:    return "busy";
		}
		return "none";
	}

	/// @brief state を UINode に同期する。
	void syncNodeState()
	{
		m_node->setProperty("status", statusToString(m_status));
		m_node->setProperty("has_image", m_imageKey.empty() ? "false" : "true");
	}
};

} // namespace mitiru::ui
