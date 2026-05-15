#pragma once

/// @file DX12Shaders.hpp
/// @brief DX12用2Dオーバーレイシェーダーと定数バッファ構造体
/// @details Renderer3D_DX12で使用するHLSLシェーダー文字列と
///          GPU定数バッファのCPU側構造体を定義する。

#ifdef _WIN32

#include <cstdint>

#include <sgc/math/Mat4.hpp>

#include <mitiru/render/Mesh.hpp>

namespace mitiru::render
{

// ─────────────────────────────────────────────────────────────
//  2Dオーバーレイ用 HLSL シェーダー
// ─────────────────────────────────────────────────────────────

/// @brief 2Dオーバーレイ用頂点シェーダー（正射影、スクリーン座標）
constexpr const char* OVERLAY2D_VS = R"hlsl(
cbuffer CbOverlay2D : register(b0)
{
    float4x4 OrthoProjection;
};
struct VS_IN  { float2 pos : POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
VS_OUT VSMain(VS_IN i)
{
    VS_OUT o;
    o.pos = mul(OrthoProjection, float4(i.pos, 0.0, 1.0));
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
)hlsl";

/// @brief 2Dオーバーレイ用ピクセルシェーダー（ソリッドカラー）
constexpr const char* OVERLAY2D_PS = R"hlsl(
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
float4 PSMain(VS_OUT i) : SV_TARGET
{
    return i.color;
}
)hlsl";

/// @brief 2Dオーバーレイ用頂点データ
struct Overlay2DVertex
{
	float x, y;        ///< スクリーン座標
	float u, v;        ///< テクスチャ座標
	float r, g, b, a;  ///< 頂点色
};

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

// ─────────────────────────────────────────────────────────────
//  アウトラインパス用の遅延描画コマンド
// ─────────────────────────────────────────────────────────────

/// @brief アウトライン描画コマンド（メッシュとワールド行列のペア）
struct OutlineDrawCommand
{
	const Mesh* mesh = nullptr;     ///< 描画対象メッシュ（非所有）
	sgc::Mat4f worldTransform{};    ///< ワールド変換行列
};

} // namespace mitiru::render

#endif // _WIN32
