#pragma once

/// @file UIFloatingText.hpp
/// @brief ダメージ数値 / アイテム取得 / 状態効果向けのアニメーション floating text widget。

#include <mitiru/ui/Easing.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief floating text 描画用の font weight ヒント。
enum class FloatingTextFontWeight : std::uint8_t
{
	Normal,
	Bold,
	ExtraBold,
};

/// @brief floating text entry の visual style。
struct UIFloatingTextStyle
{
	float fontSize              = 24.0f;                           ///< font size (pixel)。
	float color[4]              = {1.0f, 1.0f, 1.0f, 1.0f};      ///< テキストの RGBA 色。
	float outlineColor[4]       = {0.0f, 0.0f, 0.0f, 1.0f};      ///< 縁取りの RGBA 色。
	float outlineWidth          = 0.0f;                            ///< 縁取りの幅 (pixel)。
	float shadowColor[4]        = {0.0f, 0.0f, 0.0f, 0.0f};      ///< 影の RGBA 色。
	float shadowOffsetX         = 0.0f;                            ///< 影の X offset。
	float shadowOffsetY         = 0.0f;                            ///< 影の Y offset。
	FloatingTextFontWeight fontWeight = FloatingTextFontWeight::Normal; ///< font weight ヒント。
	std::string backgroundImageKey;                                ///< 任意の背景画像 key。
};

/// @brief floating text manager 全体の構成設定。
struct UIFloatingTextConfig
{
	float defaultDuration       = 1.0f;    ///< 既定の寿命 (秒)。
	float defaultRiseSpeed      = 80.0f;   ///< 既定の上昇速度 (pixel/秒)。
	float defaultFadeStart      = 0.6f;    ///< fade 開始の正規化時刻 (0-1)。
	std::size_t maxActive       = 32;      ///< 同時 entry 数の上限。
	bool stackSamePosition      = true;    ///< 同位置の entry を重ならないようずらす。
	float stackOffset           = 20.0f;   ///< 重ねた entry 間の垂直 offset。
};

/// @brief animation 状態を持つ単一の floating text entry。
struct UIFloatingTextEntry
{
	std::string text;                              ///< 表示するテキスト内容。
	float x             = 0.0f;                    ///< 出現 X 位置。
	float y             = 0.0f;                    ///< 出現 Y 位置。
	UIFloatingTextStyle style;                     ///< visual style。
	float duration      = 1.0f;                    ///< 寿命 (秒)。
	float riseSpeed     = 80.0f;                   ///< 上昇速度 (pixel/秒)。
	float fadeStart     = 0.6f;                    ///< fade 開始の正規化時刻。
	float scaleStart    = 1.0f;                    ///< 出現時の scale (1.0 = 等倍)。
	float scaleEnd      = 1.0f;                    ///< 消滅時の scale。
	EasingType easing   = EasingType::EaseOutQuad; ///< 上昇動作の easing curve。
	bool shakeEnabled   = false;                   ///< shake 効果を有効化 (会心の一撃)。
	float shakeAmount   = 3.0f;                    ///< shake 振幅 (pixel)。
	float shakeFrequency = 20.0f;                  ///< shake 振動の周波数 (Hz)。

	// ── runtime 状態 (UIFloatingTextManager が管理) ────
	float elapsed       = 0.0f;                    ///< 経過時間 (秒)。
	float currentX      = 0.0f;                    ///< 現在の描画 X。
	float currentY      = 0.0f;                    ///< 現在の描画 Y。
	float currentAlpha  = 1.0f;                    ///< 現在の不透明度 (0-1)。
	float currentScale  = 1.0f;                    ///< 現在の scale 係数。
};

// ── preset style の factory 関数 ──────────────────────────────

/// @brief ダメージ数値向けの、赤・大きめ・bold で bounce easing の style。
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

/// @brief 回復数値向けの、緑・中サイズの style。
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

/// @brief 会心の一撃向けの、黄・特大・bold で shake 付きの style。
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

/// @brief アイテム取得通知向けの、白・小サイズの style。
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

/// @brief 経験値獲得向けの、紫・中サイズの style。
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

/// @brief floating text entry を所有し、生成・更新・消滅させる manager。
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
	/// @brief default 構成で構築する。
	UIFloatingTextManager() = default;

	/// @brief カスタム構成で構築する。
	/// @param config manager の構成設定。
	explicit UIFloatingTextManager(const UIFloatingTextConfig& config)
		: m_config(config)
	{
	}

	/// @brief 現在の構成設定を取得する。
	[[nodiscard]] const UIFloatingTextConfig& config() const noexcept { return m_config; }

	/// @brief 新しい構成設定を設定する。
	/// @param config 新しい構成設定。
	void setConfig(const UIFloatingTextConfig& config) { m_config = config; }

	/// @brief 新しい floating text entry を生成する。
	/// @param text 表示テキスト。
	/// @param x 出現 X 位置。
	/// @param y 出現 Y 位置。
	/// @param style visual style。
	/// @return 生成した entry へのポインタ。maxActive 到達時は nullptr。
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

		// 同じ位置付近の既存 entry と重ならないよう stack offset を適用する。
		if (m_config.stackSamePosition)
		{
			applyStackOffset(entry);
		}

		m_entries.push_back(std::move(entry));
		return &m_entries.back();
	}

	/// @brief 完全カスタム指定で floating text entry を生成する。
	/// @param entry 構成済み entry (elapsed と current* フィールドは reset される)。
	/// @return 生成した entry へのポインタ。maxActive 到達時は nullptr。
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

	/// @brief すべての active entry を更新する。寿命切れの entry は除去する。
	/// @param dt delta time (秒)。
	void update(float dt)
	{
		for (auto& entry : m_entries)
		{
			entry.elapsed += dt;
			const float t = std::clamp(entry.elapsed / entry.duration, 0.0f, 1.0f);

			// easing 付きの上昇動作。
			const float easedT = easing::apply(entry.easing, t);
			const float totalRise = entry.riseSpeed * entry.duration;
			entry.currentY = entry.y - (totalRise * easedT);

			// shake 効果 (会心の一撃)。
			float shakeOffsetX = 0.0f;
			if (entry.shakeEnabled && t < entry.fadeStart)
			{
				const float shakePhase = entry.elapsed * entry.shakeFrequency * 6.2831853f;
				const float shakeDampen = 1.0f - (t / entry.fadeStart);
				shakeOffsetX = std::sin(shakePhase) * entry.shakeAmount * shakeDampen;
			}
			entry.currentX = entry.x + shakeOffsetX;

			// fade。
			if (t >= entry.fadeStart && entry.fadeStart < 1.0f)
			{
				const float fadeT = (t - entry.fadeStart) / (1.0f - entry.fadeStart);
				entry.currentAlpha = std::clamp(1.0f - fadeT, 0.0f, 1.0f);
			}
			else
			{
				entry.currentAlpha = 1.0f;
			}

			// scale の補間。
			entry.currentScale = entry.scaleStart + (entry.scaleEnd - entry.scaleStart) * t;
		}

		// 寿命切れの entry を除去する。
		m_entries.erase(
			std::remove_if(m_entries.begin(), m_entries.end(),
				[](const UIFloatingTextEntry& e) { return e.elapsed >= e.duration; }),
			m_entries.end());
	}

	/// @brief 現在 active な entry をすべて取得する (read-only)。
	[[nodiscard]] const std::vector<UIFloatingTextEntry>& activeEntries() const noexcept
	{
		return m_entries;
	}

	/// @brief 現在 active な entry の数を取得する。
	[[nodiscard]] std::size_t activeCount() const noexcept { return m_entries.size(); }

	/// @brief active な entry をすべて即座に除去する。
	void clearAll() { m_entries.clear(); }

private:
	/// @brief 同じ出現位置付近に他の entry があれば、新しい entry を垂直方向へずらす。
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
