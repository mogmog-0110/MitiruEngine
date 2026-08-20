#pragma once

/// @file DX12Shaders.hpp
/// @brief DX12用定数バッファ構造体
/// @details Renderer3D_DX12 の GPU 定数バッファの CPU 側構造体を定義する。

#ifdef _WIN32

#include <cstdint>

namespace mitiru::render
{

// ─────────────────────────────────────────────────────────────
//  DX12用定数バッファ構造体（256バイトアラインメント必須）
// ─────────────────────────────────────────────────────────────

/// @brief トランスフォーム用定数バッファ (register(b0))
/// @details D3D12のCBVは256バイトアラインメントを要求する。
struct alignas(256) DX12CbTransform
{
	float world[4][4]{};       ///< ワールド行列
	float view[4][4]{};        ///< ビュー行列
	float projection[4][4]{};  ///< 射影行列
};

/// @brief ライティング用定数バッファ (register(b1))
/// @details D3D12のCBVは256バイトアラインメントを要求する。
struct alignas(256) DX12CbLighting
{
	float lightDir[4]{};          ///< ライト方向 (xyz) + パディング
	float lightColor[4]{};        ///< ライト色 (xyz) + パディング
	float ambientColor[4]{};      ///< アンビエント色 (xyz) + パディング
	float cameraPos[4]{};         ///< カメラ位置 (xyz) + パディング
	float materialDiffuse[4]{};   ///< マテリアル拡散色 (rgba)
	float materialSpecular[4]{};  ///< マテリアル鏡面色 (rgba)
	float materialShininess = 32.0f;  ///< マテリアル光沢度
	/// 影部の色 (トゥーン時のみ使用)。暗くするだけでなく色相を寄せるため rgb で持つ。
	float shadowTint[3]{0.60f, 0.64f, 0.76f};
	float fogColor[4]{};              ///< 距離フォグの色 (a は未使用)
	/// x=かかり始める距離 y=完全に染まる距離 z=有効フラグ (0/1)
	float fogParams[4]{};
	/// マテリアル由来の描画指定。x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=予備
	float materialParams[4]{0.5f, 0.0f, 0.0f, 0.0f};
};

} // namespace mitiru::render

#endif // _WIN32
