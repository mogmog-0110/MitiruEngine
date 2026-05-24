#pragma once

/// @file ResourceHandle.hpp
/// @brief リソースハンドル型定義
///
/// リソースマネージャで使用する型安全なハンドルと状態列挙。

#include "sgc/core/Handle.hpp"

namespace sgc
{

/// @brief リソースハンドル用タグ
struct ResourceTag {};

/// @brief リソースハンドル型
using ResourceHandle = Handle<ResourceTag>;

/// @brief リソース状態
enum class ResourceState
{
	Unloaded,  ///< 未ロード
	Loading,   ///< ロード中（非同期）
	Loaded,    ///< ロード完了
	Error      ///< ロード失敗
};

} // namespace sgc
