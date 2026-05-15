#pragma once

/// @file UIFloatingText.hpp
/// @brief Animated floating text widget for damage numbers, item pickups, status effects.

#include <mitiru/ui/Easing.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Font weight hint for floating text rendering.
enum class FloatingTextFontWeight : std::uint8_t
{
	Normal,
	Bold,
	ExtraBold,
};

/// @brief Visual style for a floating text entry.
struct UIFloatingTextStyle
{
	float fontSize              = 24.0f;                           ///< Font size in pixels.
	float color[4]              = {1.0f, 1.0f, 1.0f, 1.0f};      ///< Text RGBA color.
	float outlineColor[4]       = {0.0f, 0.0f, 0.0f, 1.0f};      ///< Outline RGBA color.
	float outlineWidth          = 0.0f;                            ///< Outline width in pixels.
	float shadowColor[4]        = {0.0f, 0.0f, 0.0f, 0.0f};      ///< Shadow RGBA color.
	float shadowOffsetX         = 0.0f;                            ///< Shadow X offset.
	float shadowOffsetY         = 0.0f;                            ///< Shadow Y offset.
	FloatingTextFontWeight fontWeight = FloatingTextFontWeight::Normal; ///< Font weight hint.
	std::string backgroundImageKey;                                ///< Optional background image key.
};

/// @brief Global configuration for the floating text manager.
struct UIFloatingTextConfig
{
	float defaultDuration       = 1.0f;    ///< Default lifetime in seconds.
	float defaultRiseSpeed      = 80.0f;   ///< Default upward speed in pixels/sec.
	float defaultFadeStart      = 0.6f;    ///< Normalized time (0-1) when fade begins.
	std::size_t maxActive       = 32;      ///< Maximum simultaneous entries.
	bool stackSamePosition      = true;    ///< Offset entries at same position to avoid overlap.
	float stackOffset           = 20.0f;   ///< Vertical offset between stacked entries.
};

/// @brief A single floating text entry with animation state.
struct UIFloatingTextEntry
{
	std::string text;                              ///< Display text content.
	float x             = 0.0f;                    ///< Spawn X position.
	float y             = 0.0f;                    ///< Spawn Y position.
	UIFloatingTextStyle style;                     ///< Visual style.
	float duration      = 1.0f;                    ///< Lifetime in seconds.
	float riseSpeed     = 80.0f;                   ///< Upward speed in pixels/sec.
	float fadeStart     = 0.6f;                    ///< Normalized time when fade begins.
	float scaleStart    = 1.0f;                    ///< Scale at spawn (1.0 = normal).
	float scaleEnd      = 1.0f;                    ///< Scale at expiry.
	EasingType easing   = EasingType::EaseOutQuad; ///< Easing curve for rise motion.
	bool shakeEnabled   = false;                   ///< Enable shake effect (critical hits).
	float shakeAmount   = 3.0f;                    ///< Shake amplitude in pixels.
	float shakeFrequency = 20.0f;                  ///< Shake oscillation frequency (Hz).

	// ── Runtime state (managed by UIFloatingTextManager) ────
	float elapsed       = 0.0f;                    ///< Elapsed time in seconds.
	float currentX      = 0.0f;                    ///< Current rendered X.
	float currentY      = 0.0f;                    ///< Current rendered Y.
	float currentAlpha  = 1.0f;                    ///< Current opacity (0-1).
	float currentScale  = 1.0f;                    ///< Current scale factor.
};

// ── Preset style factory functions ──────────────────────────────

/// @brief Red, large, bold style with bounce easing for damage numbers.
[[nodiscard]] inline UIFloatingTextStyle damageStyle() noexcept
{
	UIFloatingTextStyle s;
	s.fontSize          = 32.0f;
	s.color[0] = 1.0f; s.color[1] = 0.2f; s.color[2] = 0.2f; s.color[3] = 1.0f;
	s.outlineColor[0] = 0.0f; s.outlineColor[1] = 0.0f; s.outlineColor[2] = 0.0f; s.outlineColor[3] = 1.0f;
	s.outlineWidth      = 2.0f;
	s.fontWeight        = FloatingTextFontWeight::Bold;
	return s;
}

/// @brief Green, medium style for healing numbers.
[[nodiscard]] inline UIFloatingTextStyle healStyle() noexcept
{
	UIFloatingTextStyle s;
	s.fontSize          = 26.0f;
	s.color[0] = 0.2f; s.color[1] = 1.0f; s.color[2] = 0.3f; s.color[3] = 1.0f;
	s.outlineColor[0] = 0.0f; s.outlineColor[1] = 0.0f; s.outlineColor[2] = 0.0f; s.outlineColor[3] = 0.8f;
	s.outlineWidth      = 1.5f;
	s.fontWeight        = FloatingTextFontWeight::Normal;
	return s;
}

/// @brief Yellow, extra-large, bold style with shake for critical hits.
[[nodiscard]] inline UIFloatingTextStyle criticalStyle() noexcept
{
	UIFloatingTextStyle s;
	s.fontSize          = 40.0f;
	s.color[0] = 1.0f; s.color[1] = 0.9f; s.color[2] = 0.1f; s.color[3] = 1.0f;
	s.outlineColor[0] = 0.6f; s.outlineColor[1] = 0.0f; s.outlineColor[2] = 0.0f; s.outlineColor[3] = 1.0f;
	s.outlineWidth      = 3.0f;
	s.shadowColor[0] = 0.0f; s.shadowColor[1] = 0.0f; s.shadowColor[2] = 0.0f; s.shadowColor[3] = 0.5f;
	s.shadowOffsetX     = 2.0f;
	s.shadowOffsetY     = 2.0f;
	s.fontWeight        = FloatingTextFontWeight::ExtraBold;
	return s;
}

/// @brief White, small style for item pickup notifications.
[[nodiscard]] inline UIFloatingTextStyle itemPickupStyle() noexcept
{
	UIFloatingTextStyle s;
	s.fontSize          = 18.0f;
	s.color[0] = 1.0f; s.color[1] = 1.0f; s.color[2] = 1.0f; s.color[3] = 1.0f;
	s.outlineColor[0] = 0.0f; s.outlineColor[1] = 0.0f; s.outlineColor[2] = 0.0f; s.outlineColor[3] = 0.6f;
	s.outlineWidth      = 1.0f;
	s.fontWeight        = FloatingTextFontWeight::Normal;
	return s;
}

/// @brief Purple, medium style for experience gain.
[[nodiscard]] inline UIFloatingTextStyle expGainStyle() noexcept
{
	UIFloatingTextStyle s;
	s.fontSize          = 22.0f;
	s.color[0] = 0.7f; s.color[1] = 0.3f; s.color[2] = 1.0f; s.color[3] = 1.0f;
	s.outlineColor[0] = 0.2f; s.outlineColor[1] = 0.0f; s.outlineColor[2] = 0.4f; s.outlineColor[3] = 0.9f;
	s.outlineWidth      = 1.5f;
	s.fontWeight        = FloatingTextFontWeight::Bold;
	return s;
}

/// @brief Manager that owns, spawns, updates, and expires floating text entries.
///
/// @code
///   UIFloatingTextConfig config;
///   config.maxActive = 64;
///   config.defaultRiseSpeed = 100.0f;
///   UIFloatingTextManager mgr(config);
///
///   mgr.spawn("999", 400.0f, 300.0f, criticalStyle());
///   mgr.spawn("+50 HP", 400.0f, 320.0f, healStyle());
///
///   // Each frame:
///   mgr.update(deltaTime);
///   for (const auto& entry : mgr.activeEntries()) {
///       // Render entry.text at (entry.currentX, entry.currentY)
///       // with alpha = entry.currentAlpha, scale = entry.currentScale
///   }
/// @endcode
class UIFloatingTextManager
{
	UIFloatingTextConfig m_config;
	std::vector<UIFloatingTextEntry> m_entries;

public:
	/// @brief Construct with default configuration.
	UIFloatingTextManager() = default;

	/// @brief Construct with custom configuration.
	/// @param config Manager configuration.
	explicit UIFloatingTextManager(const UIFloatingTextConfig& config)
		: m_config(config)
	{
	}

	/// @brief Get the current configuration.
	[[nodiscard]] const UIFloatingTextConfig& config() const noexcept { return m_config; }

	/// @brief Set a new configuration.
	/// @param config New configuration.
	void setConfig(const UIFloatingTextConfig& config) { m_config = config; }

	/// @brief Spawn a new floating text entry.
	/// @param text Display text.
	/// @param x Spawn X position.
	/// @param y Spawn Y position.
	/// @param style Visual style.
	/// @return Pointer to the spawned entry, or nullptr if maxActive reached.
	UIFloatingTextEntry* spawn(const std::string& text, float x, float y,
	                           const UIFloatingTextStyle& style)
	{
		if (m_entries.size() >= m_config.maxActive)
		{
			return nullptr;
		}

		UIFloatingTextEntry entry;
		entry.text          = text;
		entry.x             = x;
		entry.y             = y;
		entry.style         = style;
		entry.duration      = m_config.defaultDuration;
		entry.riseSpeed     = m_config.defaultRiseSpeed;
		entry.fadeStart     = m_config.defaultFadeStart;
		entry.currentX      = x;
		entry.currentY      = y;
		entry.currentAlpha  = 1.0f;
		entry.currentScale  = entry.scaleStart;

		// Apply stack offset to avoid overlap with existing entries near the same position.
		if (m_config.stackSamePosition)
		{
			applyStackOffset(entry);
		}

		m_entries.push_back(std::move(entry));
		return &m_entries.back();
	}

	/// @brief Spawn a floating text entry with full customization.
	/// @param entry Pre-configured entry (elapsed and current* fields will be reset).
	/// @return Pointer to the spawned entry, or nullptr if maxActive reached.
	UIFloatingTextEntry* spawnCustom(UIFloatingTextEntry entry)
	{
		if (m_entries.size() >= m_config.maxActive)
		{
			return nullptr;
		}

		entry.elapsed      = 0.0f;
		entry.currentX     = entry.x;
		entry.currentY     = entry.y;
		entry.currentAlpha = 1.0f;
		entry.currentScale = entry.scaleStart;

		if (m_config.stackSamePosition)
		{
			applyStackOffset(entry);
		}

		m_entries.push_back(std::move(entry));
		return &m_entries.back();
	}

	/// @brief Update all active entries. Removes expired entries.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		for (auto& entry : m_entries)
		{
			entry.elapsed += dt;
			const float t = std::clamp(entry.elapsed / entry.duration, 0.0f, 1.0f);

			// Rise motion with easing.
			const float easedT = easing::apply(entry.easing, t);
			const float totalRise = entry.riseSpeed * entry.duration;
			entry.currentY = entry.y - (totalRise * easedT);

			// Shake effect (critical hits).
			float shakeOffsetX = 0.0f;
			if (entry.shakeEnabled && t < entry.fadeStart)
			{
				const float shakePhase = entry.elapsed * entry.shakeFrequency * 6.2831853f;
				const float shakeDampen = 1.0f - (t / entry.fadeStart);
				shakeOffsetX = std::sin(shakePhase) * entry.shakeAmount * shakeDampen;
			}
			entry.currentX = entry.x + shakeOffsetX;

			// Fade.
			if (t >= entry.fadeStart && entry.fadeStart < 1.0f)
			{
				const float fadeT = (t - entry.fadeStart) / (1.0f - entry.fadeStart);
				entry.currentAlpha = std::clamp(1.0f - fadeT, 0.0f, 1.0f);
			}
			else
			{
				entry.currentAlpha = 1.0f;
			}

			// Scale interpolation.
			entry.currentScale = entry.scaleStart + (entry.scaleEnd - entry.scaleStart) * t;
		}

		// Remove expired entries.
		m_entries.erase(
			std::remove_if(m_entries.begin(), m_entries.end(),
				[](const UIFloatingTextEntry& e) { return e.elapsed >= e.duration; }),
			m_entries.end());
	}

	/// @brief Get all currently active entries (read-only).
	[[nodiscard]] const std::vector<UIFloatingTextEntry>& activeEntries() const noexcept
	{
		return m_entries;
	}

	/// @brief Get the number of currently active entries.
	[[nodiscard]] std::size_t activeCount() const noexcept { return m_entries.size(); }

	/// @brief Remove all active entries immediately.
	void clearAll() { m_entries.clear(); }

private:
	/// @brief Offset a new entry vertically if others exist near the same spawn position.
	void applyStackOffset(UIFloatingTextEntry& entry)
	{
		constexpr float proximityThreshold = 10.0f;
		int stackCount = 0;

		for (const auto& existing : m_entries)
		{
			const float dx = existing.x - entry.x;
			const float dy = existing.y - entry.y;
			if (dx * dx + dy * dy < proximityThreshold * proximityThreshold)
			{
				++stackCount;
			}
		}

		if (stackCount > 0)
		{
			entry.y -= static_cast<float>(stackCount) * m_config.stackOffset;
			entry.currentY = entry.y;
		}
	}
};

} // namespace mitiru::ui
