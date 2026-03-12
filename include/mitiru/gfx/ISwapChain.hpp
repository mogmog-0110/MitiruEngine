#pragma once

/// @file ISwapChain.hpp
/// @brief スワップチェーン抽象インターフェース
/// @details ダブル/トリプルバッファリングを管理するスワップチェーンの基底。

namespace mitiru::gfx
{

class IRenderTarget;

/// @brief スワップチェーンの抽象インターフェース
/// @details 画面表示用のバックバッファ切り替えを管理する。
class ISwapChain
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ISwapChain() = default;

	/// @brief バックバッファを画面に表示する
	virtual void present() = 0;

	/// @brief スワップチェーンのサイズを変更する
	/// @param width 新しい幅（ピクセル）
	/// @param height 新しい高さ（ピクセル）
	virtual void resize(int width, int height) = 0;

	/// @brief 現在のバックバッファを取得する
	/// @return バックバッファのレンダーターゲット
	[[nodiscard]] virtual IRenderTarget* backBuffer() noexcept = 0;
};

} // namespace mitiru::gfx
