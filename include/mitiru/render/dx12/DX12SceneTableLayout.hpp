#pragma once

/// @file DX12SceneTableLayout.hpp
/// @brief 前方の描画の「場面の表」(ルート引数 5、フレームに 1 枚) の SRV の並び
/// @details メインと屋外のルートシグネチャが同じ並びを張り、DX12Materials.hpp の ensureSceneTable が同じ順に書く。

#ifdef _WIN32

#include <d3d12.h>

namespace mitiru::render::dx12
{

/// { t1, t2 影 }、{ t8 irradiance, t9 prefiltered, t10 BRDF 表, t11 スポットの影 }、{ t35..t38 デカールと VFX テクスチャ }、
/// { t39 放射照度のプローブ, t40 プローブの距離の地図, t41 反射のプローブ, t42 前フレームの HZB, t43 前フレームの色 }
inline constexpr D3D12_DESCRIPTOR_RANGE kSceneTableRanges[4] = {
	{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0},
	{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 8, 0, 2},
	{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 35, 0, 6},
	{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 39, 0, 10},
};
inline constexpr UINT kSceneTableRangeCount = 4;
inline constexpr UINT kSceneTableSize = 15;
/// 表の中の t39 の位置 (間接光の 5 枚の先頭)
inline constexpr UINT kSceneTableIndirect = 10;

} // namespace mitiru::render::dx12

#endif // _WIN32
