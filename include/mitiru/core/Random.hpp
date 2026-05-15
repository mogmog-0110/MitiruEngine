#pragma once

/// @file Random.hpp
/// @brief DLL境界安全な乱数生成器
/// @details std::randomを使わずLCGベースで実装。
///          DLLプラグインとホスト間で安全に共有できる。

#include <cstdint>

namespace mitiru
{

/// @brief LCGベースの乱数生成器（DLL境界安全）
/// @details std::random はDLL境界で安全に使えないため、
///          シンプルなLCG（線形合同法）で実装する。
///
/// @code
/// mitiru::Random rng(12345);
/// int damage = rng.nextInt(10, 50);
/// float angle = rng.nextFloat(0.0f, 360.0f);
/// bool crit = rng.nextBool(0.15f);
/// @endcode
class Random
{
public:
	/// @brief コンストラクタ
	/// @param seed 初期シード値
	explicit Random(uint32_t seed = 42) noexcept
		: m_state(seed)
	{
	}

	/// @brief シード値を再設定する
	/// @param seed 新しいシード値
	void setSeed(uint32_t seed) noexcept { m_state = seed; }

	/// @brief 次の乱数値を生成する（0〜32767）
	/// @return 疑似乱数値
	[[nodiscard]] uint32_t next() noexcept
	{
		m_state = m_state * 1103515245 + 12345;
		return (m_state >> 16) & 0x7FFF;
	}

	/// @brief 指定範囲の整数乱数を生成する
	/// @param min 最小値（含む）
	/// @param max 最大値（含む）
	/// @return [min, max] の範囲の乱数
	[[nodiscard]] int nextInt(int min, int max) noexcept
	{
		if (min >= max) return min;
		return min + static_cast<int>(next() % static_cast<uint32_t>(max - min + 1));
	}

	/// @brief 指定範囲の浮動小数点乱数を生成する
	/// @param min 最小値
	/// @param max 最大値
	/// @return [min, max) の範囲の乱数
	[[nodiscard]] float nextFloat(float min = 0.0f, float max = 1.0f) noexcept
	{
		return min + (static_cast<float>(next()) / 32767.0f) * (max - min);
	}

	/// @brief 確率に基づくブール値を生成する
	/// @param probability trueを返す確率 [0, 1]
	/// @return 確率に基づくブール値
	[[nodiscard]] bool nextBool(float probability = 0.5f) noexcept
	{
		return nextFloat() < probability;
	}

	/// @brief コンテナサイズからランダムなインデックスを選ぶ
	/// @param size コンテナのサイズ
	/// @return [0, size-1] の範囲のインデックス（size <= 0 なら 0）
	[[nodiscard]] int pick(int size) noexcept
	{
		if (size <= 0) return 0;
		return static_cast<int>(next() % static_cast<uint32_t>(size));
	}

private:
	uint32_t m_state;  ///< LCG内部状態
};

} // namespace mitiru
