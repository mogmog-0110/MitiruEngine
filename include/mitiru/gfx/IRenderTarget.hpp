#pragma once

/// @file IRenderTarget.hpp
/// @brief レンダーターゲット抽象インターフェース
/// @details 描画先となるレンダーターゲットの基底インターフェースを定義する。

#include <cstddef>
#include <cstdint>

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

class ITexture;

/// @brief レンダーターゲットの抽象インターフェース
/// @details フレームバッファやオフスクリーンレンダーターゲットの基底。
class IRenderTarget
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IRenderTarget() = default;

	/// @brief レンダーターゲット幅を取得する
	/// @return 幅（ピクセル）
	[[nodiscard]] virtual int width() const noexcept = 0;

	/// @brief レンダーターゲット高さを取得する
	/// @return 高さ（ピクセル）
	[[nodiscard]] virtual int height() const noexcept = 0;

	/// @brief 関連付けられたテクスチャを取得する
	/// @return テクスチャへのポインタ（バックバッファの場合はnullptr）
	[[nodiscard]] virtual ITexture* texture() noexcept = 0;

	/// @brief バックエンド固有のレンダーターゲットビューを取得する
	/// @return DX11なら`ID3D11RenderTargetView*`等（未対応バックエンド/RTはnullptr）。
	///         RenderTargetPool 経由で確保した RT を既存の raw API 呼び出しへ橋渡しするためのアクセサ。
	[[nodiscard]] virtual void* nativeRtv() noexcept { return nullptr; }

	/// @brief バックエンド固有のシェーダーリソースビューを取得する
	/// @return DX11なら`ID3D11ShaderResourceView*`等（未対応バックエンド/RTはnullptr）
	[[nodiscard]] virtual void* nativeSrv() noexcept { return nullptr; }

	/// @brief バックエンド固有の深度ステンシルビューを取得する
	/// @return DX11なら`ID3D11DepthStencilView*`等。depth-only RT 以外は nullptr
	[[nodiscard]] virtual void* nativeDsv() noexcept { return nullptr; }
};

/// @brief RenderTargetPool / IDevice::createRenderTarget が使う RT 記述子
struct RenderTargetDesc
{
	int width = 0;                            ///< 幅（ピクセル）
	int height = 0;                           ///< 高さ（ピクセル）
	PixelFormat format = PixelFormat::RGBA8;  ///< ピクセルフォーマット
	std::uint32_t flags = 0;                  ///< 呼び出し側定義の用途ビット（同サイズ同フォーマットでも別種として分けたい時に使う）

	[[nodiscard]] bool operator==(const RenderTargetDesc& other) const noexcept
	{
		return width == other.width && height == other.height
			&& format == other.format && flags == other.flags;
	}
};

/// @brief RenderTargetDesc のハッシュ（unordered_map キー用）
struct RenderTargetDescHash
{
	[[nodiscard]] std::size_t operator()(const RenderTargetDesc& d) const noexcept
	{
		std::size_t h = static_cast<std::size_t>(d.width);
		h = h * 131u + static_cast<std::size_t>(d.height);
		h = h * 131u + static_cast<std::size_t>(d.format);
		h = h * 131u + static_cast<std::size_t>(d.flags);
		return h;
	}
};

} // namespace mitiru::gfx
