#pragma once

#include <cstdint>
#include <stdexcept>

/// @file Rng.hpp
/// @brief 決定論的な線形合同法（LCG）乱数生成器

namespace mitiru::util
{

/// @brief 決定論的 LCG 乱数生成器
/// @note シード値が同じなら常に同じ乱数列を生成する。再現性が必要なゲームロジックに適する
class Rng
{
public:
	/// @brief コンストラクタ
	/// @param seed 初期シード値
	explicit Rng(std::uint32_t seed = 42) noexcept
		: m_state(seed)
	{
	}

	/// @brief LCG を 1 ステップ進めて乱数を返す
	/// @return 0〜32767 の範囲の乱数値
	std::uint32_t next() noexcept
	{
		m_state = m_state * 1103515245u + 12345u;
		return (m_state >> 16) & 0x7FFF;
	}

	/// @brief 指定範囲の整数乱数を返す
	/// @param min 最小値（含む）
	/// @param max 最大値（含む）
	/// @return [min, max] の範囲の整数
	/// @throws std::invalid_argument min > max の場合
	int nextInt(int min, int max)
	{
		if (min > max)
		{
			throw std::invalid_argument("Rng::nextInt: min must be <= max");
		}
		return min + static_cast<int>(next() % static_cast<std::uint32_t>(max - min + 1));
	}

	/// @brief 指定範囲の浮動小数点乱数を返す
	/// @param min 最小値（含む）
	/// @param max 最大値（含む）
	/// @return [min, max] の範囲の浮動小数点数
	/// @throws std::invalid_argument min > max の場合
	float nextFloat(float min = 0.0f, float max = 1.0f)
	{
		if (min > max)
		{
			throw std::invalid_argument("Rng::nextFloat: min must be <= max");
		}
		return min + (static_cast<float>(next()) / 32767.0f) * (max - min);
	}

	/// @brief 確率に基づいて bool 値を返す
	/// @param probability true を返す確率（0.0〜1.0）
	/// @return 指定確率で true
	bool nextBool(float probability = 0.5f)
	{
		return nextFloat() < probability;
	}

	/// @brief シード値をリセットする
	/// @param seed 新しいシード値
	void reset(std::uint32_t seed) noexcept
	{
		m_state = seed;
	}

private:
	/// @brief LCG 内部状態
	std::uint32_t m_state;
};

} // namespace mitiru::util
