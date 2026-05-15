#pragma once

/// @file Fade.hpp
/// @brief フェードイン/アウトトランジション
/// @details シーン遷移時などに使用するフェード効果。

#include <algorithm>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::effects
{

/// @brief フェード方向
enum class FadeDirection
{
	In,  ///< フェードイン（暗→明）
	Out  ///< フェードアウト（明→暗）
};

/// @brief フェードエフェクト
class Fade
{
public:
	/// @brief フェードインを開始する
	/// @param duration 持続時間（秒）
	/// @param color フェード色
	void fadeIn(float duration = 1.0f, const sgc::Colorf& color = {0, 0, 0, 1})
	{
		m_color = color;
		m_duration = std::max(0.001f, duration);
		m_elapsed = 0.0f;
		m_direction = FadeDirection::In;
		m_active = true;
	}

	/// @brief フェードアウトを開始する
	/// @param duration 持続時間（秒）
	/// @param color フェード色
	void fadeOut(float duration = 1.0f, const sgc::Colorf& color = {0, 0, 0, 1})
	{
		m_color = color;
		m_duration = std::max(0.001f, duration);
		m_elapsed = 0.0f;
		m_direction = FadeDirection::Out;
		m_active = true;
	}

	/// @brief 更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (!m_active) { return; }
		m_elapsed += dt;
		if (m_elapsed >= m_duration)
		{
			m_active = false;
		}
	}

	/// @brief フェードを描画する
	/// @param screen 描画先サーフェス
	void draw(Screen& screen) const;

	/// @brief 完了したかどうか
	[[nodiscard]] bool isComplete() const noexcept { return !m_active; }

	/// @brief アクティブかどうか
	[[nodiscard]] bool isActive() const noexcept { return m_active; }

	/// @brief 進行度 [0,1]
	[[nodiscard]] float progress() const noexcept
	{
		if (!m_active || m_duration <= 0.0f) { return 1.0f; }
		return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
	}

	/// @brief 現在の方向
	[[nodiscard]] FadeDirection direction() const noexcept { return m_direction; }

private:
	sgc::Colorf m_color{0, 0, 0, 1};                ///< フェード色
	float m_duration = 1.0f;                          ///< 持続時間
	float m_elapsed = 0.0f;                           ///< 経過時間
	FadeDirection m_direction = FadeDirection::Out;    ///< 方向
	bool m_active = false;                            ///< アクティブフラグ
};

} // namespace mitiru::effects

// ── Screen依存の実装 ──
#include <mitiru/core/Screen.hpp>

inline void mitiru::effects::Fade::draw(Screen& screen) const
{
	if (!m_active) { return; }
	const float t = progress();
	/// フェードイン: alpha 1→0、フェードアウト: alpha 0→1
	const float alpha = (m_direction == FadeDirection::In)
		? m_color.a * (1.0f - t)
		: m_color.a * t;
	if (alpha <= 0.0f) { return; }
	const sgc::Colorf drawColor{m_color.r, m_color.g, m_color.b, alpha};
	screen.drawRect(
		sgc::Rectf{0.0f, 0.0f,
			static_cast<float>(screen.width()),
			static_cast<float>(screen.height())},
		drawColor);
}
