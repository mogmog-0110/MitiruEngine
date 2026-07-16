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
	float _pad[3]{};              ///< パディング
};

} // namespace mitiru::render

#endif // _WIN32
