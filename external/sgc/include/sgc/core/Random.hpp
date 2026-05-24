#pragma once

/// @file Random.hpp
/// @brief シード付き擬似乱数生成器ラッパー
///
/// std::mt19937をラップし、ゲーム開発でよく使う乱数操作を提供する。
/// シード指定により再現可能な乱数列を生成できる。

#include <random>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief シード付き擬似乱数生成器
///
/// @code
/// sgc::Random rng(42);  // シード固定（再現可能）
/// int damage = rng.range(10, 50);
/// float angle = rng.rangeF(0.0f, 360.0f);
/// bool crit = rng.chance(0.15f);  // 15%の確率
/// @endcode
class Random
{
public:
	/// @brief ランダムシードで構築する
	Random()
		: m_engine(std::random_device{}())
	{
	}

	/// @brief 指定シードで構築する（再現性が必要な場合）
	/// @param seed シード値
	explicit Random(std::uint32_t seed)
		: m_engine(seed)
	{
	}

	/// @brief [min, max] の整数乱数を返す
	/// @tparam T 整数型
	/// @param min 最小値（含む）
	/// @param max 最大値（含む）
	/// @return 範囲内のランダムな整数
	template <Integral T>
	[[nodiscard]] T range(T min, T max)
	{
		std::uniform_int_distribution<T> dist(min, max);
		return dist(m_engine);
	}

	/// @brief [min, max] の浮動小数点乱数を返す
	/// @tparam T 浮動小数点型
	/// @param min 最小値（含む）
	/// @param max 最大値（含む）
	/// @return 範囲内のランダムな浮動小数点値
	template <FloatingPoint T>
	[[nodiscard]] T rangeF(T min, T max)
	{
		std::uniform_real_distribution<T> dist(min, max);
		return dist(m_engine);
	}

	/// @brief [0.0, 1.0) の正規化された乱数を返す
	/// @tparam T 浮動小数点型（デフォルト: float）
	/// @return 0以上1未満のランダム値
	template <FloatingPoint T = float>
	[[nodiscard]] T normalized()
	{
		return rangeF(T{0}, T{1});
	}

	/// @brief 指定確率で true を返す
	/// @param probability 確率 [0.0, 1.0]
	/// @return probability の確率で true
	[[nodiscard]] bool chance(float probability)
	{
		return normalized() < probability;
	}

	/// @brief シードを再設定する
	/// @param seed 新しいシード値
	void reseed(std::uint32_t seed)
	{
		m_engine.seed(seed);
	}

private:
	std::mt19937 m_engine;
};

} // namespace sgc
