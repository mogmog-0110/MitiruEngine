#pragma once

/// @file GfxTypes.hpp
/// @brief グラフィックス型定義
/// @details レンダリングパイプラインで使用する列挙型・記述子構造体を定義する。
///          Phase 0 では最小限のみ。

#include <cstdint>

#include <mitiru/core/Config.hpp>

namespace mitiru::gfx
{

/// @brief 頂点フォーマット
enum class VertexFormat
{
	Position2D,      ///< 2D位置のみ (float x 2)
	Position3D,      ///< 3D位置のみ (float x 3)
	PositionColor,   ///< 位置 + 色 (float x 2 + float x 4)
	PositionTexCoord ///< 位置 + テクスチャ座標 (float x 2 + float x 2)
};

/// @brief ピクセルフォーマット
enum class PixelFormat
{
	RGBA8,    ///< 8bit x 4チャンネル
	BGRA8,    ///< 8bit x 4チャンネル（BGRA順）
	R8,       ///< 8bit 1チャンネル
	Depth24Stencil8  ///< 深度24bit + ステンシル8bit
};

/// @brief ブレンドモード
enum class BlendMode
{
	None,          ///< ブレンドなし
	Alpha,         ///< アルファブレンド
	Additive,      ///< 加算ブレンド
	Multiplicative ///< 乗算ブレンド
};

/// @brief テクスチャ記述子
struct TextureDesc
{
	int width = 0;                              ///< テクスチャ幅
	int height = 0;                             ///< テクスチャ高さ
	PixelFormat format = PixelFormat::RGBA8;     ///< ピクセルフォーマット
	bool renderTarget = false;                  ///< レンダーターゲットとして使用するか
};

/// @brief バッファ記述子
struct BufferDesc
{
	/// @brief バッファ用途
	enum class Usage
	{
		Vertex,   ///< 頂点バッファ
		Index,    ///< インデックスバッファ
		Constant  ///< 定数バッファ
	};

	Usage usage = Usage::Vertex;       ///< バッファ用途
	std::uint32_t sizeBytes = 0;       ///< バッファサイズ（バイト）
	bool dynamic = false;              ///< 動的更新が必要か
};

} // namespace mitiru::gfx
