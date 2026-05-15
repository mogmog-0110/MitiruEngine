#pragma once

/// @file ColorFlash.hpp
/// @brief フルスクリーンカラーフラッシュ
/// @details ダメージや特殊効果時に画面全体を一瞬色付きオーバーレイで覆う。

#include <algorithm>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::effects
{

/// @brief カラーフラッシュエフェクト
class ColorFlash
{
public:
	/// @brief フラッシュを発動する
	/// @param color フラッシュ色
	/// @param duration 持続時間（秒）
	void flash(const sgc::Colorf& color, float duration = 0.2f)
	{
		m_color = color;
		m_duration = std::max(0.001f, duration);
		m_elapsed = 0.0f;
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

	/// @brief フラッシュを描画する
	/// @param screen 描画先サーフェス
	void draw(Screen& screen) const;

	/// @brief アクティブかどうか
	[[nodiscard]] bool isActive() const noexcept { return m_active; }

	/// @brief 進行度 [0,1]
	[[nodiscard]] float progress() const noexcept
	{
		if (!m_active || m_duration <= 0.0f) { return 1.0f; }
		return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
	}

	/// @brief 現在のアルファ値
	[[nodiscard]] float currentAlpha() const noexcept
	{
		if (!m_active) { return 0.0f; }
		return m_color.a * (1.0f - progress());
	}

private:
	sgc::Colorf m_color{1.0f, 1.0f, 1.0f, 1.0f}; ///< フラッシュ色
	float m_duration = 0.2f;                         ///< 持続時間
	float m_elapsed = 0.0f;                          ///< 経過時間
	bool m_active = false;                           ///< アクティブフラグ
};

} // namespace mitiru::effects

// ── Screen依存の実装 ──
#include <mitiru/core/Screen.hpp>

inline void mitiru::effects::ColorFlash::draw(Screen& screen) const
{
	if (!m_active) { return; }
	const float alpha = currentAlpha();
	if (alpha <= 0.0f) { return; }
	const sgc::Colorf drawColor{m_color.r, m_color.g, m_color.b, alpha};
	screen.drawRect(
		sgc::Rectf{0.0f, 0.0f,
			static_cast<float>(screen.width()),
			static_cast<float>(screen.height())},
		drawColor);
}
