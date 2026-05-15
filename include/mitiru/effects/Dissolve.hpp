#pragma once

/// @file Dissolve.hpp
/// @brief ノイズベースのディゾルブ遷移エフェクト
/// @details ピクセル位置に基づくハッシュノイズの閾値でディゾルブする。

#include <algorithm>
#include <cstdint>

namespace mitiru::effects
{

/// @brief ディゾルブエフェクト
class Dissolve
{
public:
	/// @brief ディゾルブを開始する
	/// @param duration 持続時間（秒）
	void start(float duration = 1.0f)
	{
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

	/// @brief 完了したかどうか
	[[nodiscard]] bool isComplete() const noexcept { return !m_active; }

	/// @brief アクティブかどうか
	[[nodiscard]] bool isActive() const noexcept { return m_active; }

	/// @brief 現在のディゾルブ閾値 [0,1]
	/// @details この値を下回るノイズ値のピクセルが透明になる
	[[nodiscard]] float threshold() const noexcept
	{
		if (!m_active) { return 1.0f; }
		return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
	}

	/// @brief 指定ピクセル位置のノイズ値を取得する [0,1]
	/// @param x X座標
	/// @param y Y座標
	/// @return ノイズ値 [0,1]
	[[nodiscard]] static float pixelNoise(int x, int y) noexcept
	{
		uint32_t h = static_cast<uint32_t>(x) * 374761393u
			+ static_cast<uint32_t>(y) * 668265263u;
		h ^= h >> 13;
		h *= 1274126177u;
		h ^= h >> 16;
		return static_cast<float>(h & 0xFFFF) / 65535.0f;
	}

	/// @brief 指定ピクセルが可視かどうか判定する
	/// @param x X座標
	/// @param y Y座標
	/// @return 可視なら true
	[[nodiscard]] bool isPixelVisible(int x, int y) const noexcept
	{
		return pixelNoise(x, y) >= threshold();
	}

	/// @brief リセットする
	void reset() noexcept
	{
		m_elapsed = 0.0f;
		m_active = false;
	}

private:
	float m_duration = 1.0f;  ///< 持続時間
	float m_elapsed = 0.0f;   ///< 経過時間
	bool m_active = false;    ///< アクティブフラグ
};

} // namespace mitiru::effects
