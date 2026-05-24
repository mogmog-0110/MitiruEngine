#pragma once

/// @file FontSizeSnap.hpp
/// @brief フォントサイズスナップユーティリティ
///
/// fitTextInRect()等で算出された小数フォントサイズを、
/// 登録済みフォントサイズの中から最も近いサイズにスナップする。

#include <span>
#include <cmath>
#include <cstdint>

namespace sgc::ui
{

/// @brief 登録済みフォントサイズの中から最も近いサイズを返す
/// @param targetSize 目標サイズ
/// @param registeredSizes 登録済みサイズの配列（ソート不要）
/// @return 最も近い登録済みサイズ。空なら targetSize をそのまま返す
[[nodiscard]] constexpr float snapToNearestSize(
	float targetSize,
	std::span<const float> registeredSizes) noexcept
{
	if (registeredSizes.empty()) return targetSize;

	float bestSize = registeredSizes[0];
	float bestDiff = (targetSize >= bestSize) ? (targetSize - bestSize) : (bestSize - targetSize);

	for (std::size_t i = 1; i < registeredSizes.size(); ++i)
	{
		const float diff = (targetSize >= registeredSizes[i])
			? (targetSize - registeredSizes[i])
			: (registeredSizes[i] - targetSize);
		if (diff < bestDiff)
		{
			bestDiff = diff;
			bestSize = registeredSizes[i];
		}
	}

	return bestSize;
}

/// @brief 登録済みサイズの中から targetSize 以下で最大のサイズを返す
/// @param targetSize 目標サイズ
/// @param registeredSizes 登録済みサイズの配列（ソート不要）
/// @return targetSize以下の最大登録済みサイズ。該当なしなら最小サイズを返す
[[nodiscard]] constexpr float snapFloor(
	float targetSize,
	std::span<const float> registeredSizes) noexcept
{
	if (registeredSizes.empty()) return targetSize;

	float bestSize = -1.0f;

	for (const float s : registeredSizes)
	{
		if (s <= targetSize)
		{
			if (bestSize < 0.0f || s > bestSize)
			{
				bestSize = s;
			}
		}
	}

	// 全て targetSize より大きい場合は最小サイズを返す
	if (bestSize < 0.0f)
	{
		bestSize = registeredSizes[0];
		for (std::size_t i = 1; i < registeredSizes.size(); ++i)
		{
			if (registeredSizes[i] < bestSize)
			{
				bestSize = registeredSizes[i];
			}
		}
	}

	return bestSize;
}

/// @brief 登録済みサイズの中から targetSize 以上で最小のサイズを返す
/// @param targetSize 目標サイズ
/// @param registeredSizes 登録済みサイズの配列（ソート不要）
/// @return targetSize以上の最小登録済みサイズ。該当なしなら最大サイズを返す
[[nodiscard]] constexpr float snapCeil(
	float targetSize,
	std::span<const float> registeredSizes) noexcept
{
	if (registeredSizes.empty()) return targetSize;

	float bestSize = -1.0f;

	for (const float s : registeredSizes)
	{
		if (s >= targetSize)
		{
			if (bestSize < 0.0f || s < bestSize)
			{
				bestSize = s;
			}
		}
	}

	// 全て targetSize より小さい場合は最大サイズを返す
	if (bestSize < 0.0f)
	{
		bestSize = registeredSizes[0];
		for (std::size_t i = 1; i < registeredSizes.size(); ++i)
		{
			if (registeredSizes[i] > bestSize)
			{
				bestSize = registeredSizes[i];
			}
		}
	}

	return bestSize;
}

} // namespace sgc::ui
