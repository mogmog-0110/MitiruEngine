#pragma once

/// @file IRenderTarget.hpp
/// @brief レンダーターゲット抽象インターフェース
/// @details 描画先となるレンダーターゲットの基底インターフェースを定義する。

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
};

} // namespace mitiru::gfx
