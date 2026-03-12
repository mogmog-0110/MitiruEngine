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

/// @brief リソース状態（D3D12バリア用）
enum class ResourceState : uint32_t
{
	Common = 0,       ///< 共通状態
	RenderTarget,     ///< レンダーターゲット
	Present,          ///< プレゼント
	ShaderResource,   ///< シェーダーリソース
	DepthWrite,       ///< 深度書き込み
	CopyDest,         ///< コピー先
	CopySrc,          ///< コピー元
	UnorderedAccess,  ///< アンオーダードアクセス
};

/// @brief デスクリプタヒープ種別
enum class DescriptorHeapType : uint8_t
{
	CbvSrvUav,  ///< CBV/SRV/UAV
	Rtv,        ///< レンダーターゲットビュー
	Dsv,        ///< 深度ステンシルビュー
	Sampler,    ///< サンプラー
};

/// @brief GPUデスクリプタハンドル
struct GpuDescriptorHandle
{
	uint64_t ptr = 0;  ///< GPUアドレス

	/// @brief ハンドルが有効かどうかを判定する
	/// @return ptrが0以外ならtrue
	[[nodiscard]] constexpr bool isValid() const noexcept { return ptr != 0; }
};

/// @brief CPUデスクリプタハンドル
struct CpuDescriptorHandle
{
	uint64_t ptr = 0;  ///< CPUアドレス

	/// @brief ハンドルが有効かどうかを判定する
	/// @return ptrが0以外ならtrue
	[[nodiscard]] constexpr bool isValid() const noexcept { return ptr != 0; }
};

/// @brief フェンス値
using FenceValue = uint64_t;

} // namespace mitiru::gfx
