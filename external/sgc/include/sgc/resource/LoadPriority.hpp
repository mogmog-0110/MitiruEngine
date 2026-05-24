
#pragma once

/// @file LoadPriority.hpp
/// @brief 非同期ロードの優先度定義
///
/// リソースの非同期ロード時に使用する優先度レベル。
/// Critical > High > Normal > Low の順に優先される。

#include <cstdint>

namespace sgc
{

/// @brief 非同期ロードの優先度
enum class LoadPriority : uint8_t
{
	Low = 0,       ///< 低優先度（バックグラウンドプリフェッチ等）
	Normal = 1,    ///< 通常優先度（デフォルト）
	High = 2,      ///< 高優先度（すぐ必要なリソース）
	Critical = 3,  ///< 最高優先度（ゲームプレイに必須）
};

} // namespace sgc
