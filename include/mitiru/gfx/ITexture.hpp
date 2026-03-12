#pragma once

/// @file ITexture.hpp
/// @brief テクスチャ抽象インターフェース
/// @details GPUテクスチャリソースの基底インターフェースを定義する。

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

/// @brief テクスチャの抽象インターフェース
/// @details バックエンド固有のテクスチャ実装がこのインターフェースを実装する。
class ITexture
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITexture() = default;

	/// @brief テクスチャ幅を取得する
	/// @return テクスチャ幅（ピクセル）
	[[nodiscard]] virtual int width() const noexcept = 0;

	/// @brief テクスチャ高さを取得する
	/// @return テクスチャ高さ（ピクセル）
	[[nodiscard]] virtual int height() const noexcept = 0;

	/// @brief ピクセルフォーマットを取得する
	/// @return テクスチャのピクセルフォーマット
	[[nodiscard]] virtual PixelFormat format() const noexcept = 0;
};

} // namespace mitiru::gfx
