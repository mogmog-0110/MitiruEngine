#pragma once

/// @file IShader.hpp
/// @brief 3Dシェーダーの抽象インターフェース
///
/// フレームワーク非依存のシェーダーAPIを定義する。
/// ユニフォーム変数の設定とバインド/アンバインドを抽象化する。
///
/// @code
/// class MyShader : public sgc::graphics3d::IShader {
///     void bind() const override { /* ... */ }
///     // ...
/// };
/// @endcode

#include <cstdint>
#include <string>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc::graphics3d
{

/// @brief シェーダーの抽象インターフェース
///
/// シェーダーのバインド・アンバインドとユニフォーム変数の設定を提供する。
class IShader
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IShader() = default;

	/// @brief シェーダーをバインドする（使用開始）
	virtual void bind() const = 0;

	/// @brief シェーダーをアンバインドする（使用終了）
	virtual void unbind() const = 0;

	/// @brief float型ユニフォームを設定する
	/// @param name ユニフォーム名
	/// @param value 値
	virtual void setUniformFloat(const std::string& name, float value) = 0;

	/// @brief Vec3f型ユニフォームを設定する
	/// @param name ユニフォーム名
	/// @param value ベクトル値
	virtual void setUniformVec3(const std::string& name, const Vec3f& value) = 0;

	/// @brief Mat4f型ユニフォームを設定する
	/// @param name ユニフォーム名
	/// @param value 行列値
	virtual void setUniformMat4(const std::string& name, const Mat4f& value) = 0;

	/// @brief int32_t型ユニフォームを設定する
	/// @param name ユニフォーム名
	/// @param value 整数値
	virtual void setUniformInt(const std::string& name, int32_t value) = 0;
};

} // namespace sgc::graphics3d
