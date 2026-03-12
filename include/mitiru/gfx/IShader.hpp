#pragma once

/// @file IShader.hpp
/// @brief シェーダー抽象インターフェース
/// @details GPU上で実行されるシェーダープログラムの基底インターフェース。

namespace mitiru::gfx
{

/// @brief シェーダー種別
enum class ShaderType
{
	Vertex,  ///< 頂点シェーダー
	Pixel,   ///< ピクセル（フラグメント）シェーダー
	Compute  ///< コンピュートシェーダー
};

/// @brief シェーダーの抽象インターフェース
/// @details バックエンド固有のシェーダー実装がこのインターフェースを実装する。
class IShader
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IShader() = default;

	/// @brief シェーダー種別を取得する
	/// @return シェーダーの種別
	[[nodiscard]] virtual ShaderType type() const noexcept = 0;
};

} // namespace mitiru::gfx
