#pragma once

/// @file UIAvatar.hpp
/// @brief Circular avatar image display with status indicator and fallback initials.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Online status for avatar display.
enum class AvatarStatus : std::uint8_t
{
	None,
	Online,
	Offline,
	Away,
	Busy
};

/// @brief Configuration for creating a UIAvatar.
struct UIAvatarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float size = 48.0f;                    ///< Diameter of the avatar circle.
	std::string imageKey;                  ///< Image key for the avatar picture.
	std::string fallbackText;              ///< Initials text when no image is set.
	float borderWidth = 2.0f;              ///< Border thickness around the circle.
	std::string borderColor = "cccccc";    ///< Border color (hex, no #).
	std::string borderImageKey;            ///< Border image key (overrides borderColor if set).
	AvatarStatus statusIndicator = AvatarStatus::None;  ///< Initial status.
	std::string statusColor = "00ff00";    ///< Status dot color (hex, no #).
	float statusSize = 12.0f;              ///< Diameter of the status dot.
	std::string statusPosition = "bottom_right"; ///< Where the status dot appears.
	std::string backgroundImageKey;        ///< Background image for the circle.
	bool clickable = false;                ///< Whether clicking the avatar triggers a callback.
	float fallbackFontSize = 18.0f;        ///< Font size for fallback initials.
	std::string fallbackBackgroundColor = "888888"; ///< Background color for initials fallback.
};

/// @brief Circular avatar widget with image, status indicator, and fallback initials.
///
/// Displays a circular-cropped image. When no image is set, shows colored circle
/// with initials. Optionally shows a status dot overlay (Online/Offline/Away/Busy).
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
	/// @brief Construct an avatar from configuration.
	/// @param config Avatar configuration.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current image key.
	[[nodiscard]] const std::string& imageKey() const noexcept { return m_imageKey; }

	/// @brief Get the fallback text (initials).
	[[nodiscard]] const std::string& fallbackText() const noexcept { return m_fallbackText; }

	/// @brief Get the current status.
	[[nodiscard]] AvatarStatus status() const noexcept { return m_status; }

	/// @brief Check if the avatar has an image set.
	[[nodiscard]] bool hasImage() const noexcept { return !m_imageKey.empty(); }

	/// @brief Check if the avatar is clickable.
	[[nodiscard]] bool isClickable() const noexcept { return m_clickable; }

	// -- Configuration -------------------------------------------------------

	/// @brief Set the click callback.
	/// @param callback Function invoked on click.
	void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

	// -- Setters -------------------------------------------------------------

	/// @brief Set the avatar image.
	/// @param key Image key for the avatar picture.
	void setImage(const std::string& key)
	{
		m_imageKey = key;
		m_node->setProperty("image_key", key);
		syncNodeState();
	}

	/// @brief Set the online status.
	/// @param newStatus New status value.
	void setStatus(AvatarStatus newStatus)
	{
		m_status = newStatus;
		syncNodeState();
	}

	/// @brief Set the fallback text (initials).
	/// @param text Initials to display when no image is set.
	void setFallbackText(const std::string& text)
	{
		m_fallbackText = text;
		m_node->setText(text);
		m_node->setProperty("fallback_text", text);
		syncNodeState();
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief Called when the pointer enters the avatar area.
	void onPointerEnter()
	{
		m_hovered = true;
		m_node->setProperty("hovered", "true");
	}

	/// @brief Called when the pointer leaves the avatar area.
	void onPointerLeave()
	{
		m_hovered = false;
		m_node->setProperty("hovered", "false");
	}

	/// @brief Called when the avatar is clicked.
	void onPointerUp()
	{
		if (m_clickable && m_hovered && m_onClick)
		{
			m_onClick();
		}
	}

private:
	/// @brief Convert status enum to string.
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

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setProperty("status", statusToString(m_status));
		m_node->setProperty("has_image", m_imageKey.empty() ? "false" : "true");
	}
};

} // namespace mitiru::ui
