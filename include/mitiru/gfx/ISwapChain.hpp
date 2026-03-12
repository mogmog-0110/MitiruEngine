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

	/// @brief 現在のバックバッファインデックスを取得する
	/// @return バックバッファインデックス（デフォルトは0）
	/// @details D3D12のIDXGISwapChain3::GetCurrentBackBufferIndexに対応する。
	///          DX11/Null等のバックエンドでは常に0を返す。
	[[nodiscard]] virtual uint32_t currentBackBufferIndex() const { return 0; }

	/// @brief バックバッファ数を取得する
	/// @return バッファ数（デフォルトは2：ダブルバッファリング）
	/// @details D3D12のトリプルバッファリングでは3を返す。
	[[nodiscard]] virtual uint32_t bufferCount() const { return 2; }
};

} // namespace mitiru::gfx
