#pragma once

/// @file ScreenShake.hpp
/// @brief トラウマベースのスクリーンシェイク
/// @details Vlambeer方式のtrauma^2カーブで自然な画面揺れを実現する。
///
/// @code
/// mitiru::effects::ScreenShake shake;
/// shake.addTrauma(0.5f); // ダメージ時
/// // 毎フレーム:
/// shake.update(dt);
/// camera.offset += shake.offset();
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <sgc/math/Vec2.hpp>

namespace mitiru::effects
{

/// @brief トラウマベースのスクリーンシェイク
class ScreenShake
{
public:
	float trauma = 0.0f;        ///< 現在のトラウマ値 [0,1]
	float traumaDecay = 2.0f;   ///< トラウマの減衰速度（秒あたり）
	float maxOffset = 20.0f;    ///< 最大ピクセルオフセット
	float maxRotation = 0.05f;  ///< 最大回転（ラジアン）

	/// @brief トラウマを加算する
	/// @param amount 加算量（0〜1にクランプされる）
	void addTrauma(float amount)
	{
		trauma = std::clamp(trauma + amount, 0.0f, 1.0f);
	}

	/// @brief 更新する（トラウマを減衰させる）
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		trauma = std::max(0.0f, trauma - traumaDecay * dt);
		++m_seed;
	}

	/// @brief 現在のオフセットを取得する
	/// @return ピクセルオフセット
	[[nodiscard]] sgc::Vec2f offset() const noexcept
	{
		const float shake = intensity();
		if (shake <= 0.0f) { return {0.0f, 0.0f}; }
		return {
			maxOffset * shake * noise(m_seed),
			maxOffset * shake * noise(m_seed + 1000)
		};
	}

	/// @brief 現在の回転を取得する
	/// @return 回転量（ラジアン）
	[[nodiscard]] float rotation() const noexcept
	{
		const float shake = intensity();
		if (shake <= 0.0f) { return 0.0f; }
		return maxRotation * shake * noise(m_seed + 2000);
	}

	/// @brief 現在のシェイク強度を取得する (trauma^2)
	/// @return 強度 [0,1]
	[[nodiscard]] float intensity() const noexcept
	{
		return trauma * trauma;
	}

	/// @brief トラウマをリセットする
	void reset() noexcept
	{
		trauma = 0.0f;
		m_seed = 0;
	}

private:
	uint32_t m_seed = 0; ///< ノイズシード

	/// @brief 簡易ハッシュベースノイズ [-1, 1]
	[[nodiscard]] static float noise(uint32_t seed) noexcept
	{
		uint32_t h = seed;
		h ^= h >> 16;
		h *= 0x45d9f3bU;
		h ^= h >> 16;
		h *= 0x45d9f3bU;
		h ^= h >> 16;
		return static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f;
	}
};

} // namespace mitiru::effects
