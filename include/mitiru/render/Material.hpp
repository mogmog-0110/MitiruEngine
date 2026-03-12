#pragma once

/// @file Material.hpp
/// @brief マテリアル定義
/// @details 3Dオブジェクトの表面属性（環境光・拡散反射・鏡面反射・光沢度）を定義する。

#include <sstream>
#include <string>

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief マテリアル定義
/// @details Phongシェーディングモデルに対応するマテリアルパラメータ。
///
/// @code
/// auto mat = mitiru::render::Material::defaultMaterial();
/// mat.diffuse = sgc::Colorf::red();
/// mat.shininess = 64.0f;
/// @endcode
struct Material
{
	sgc::Colorf ambient{0.1f, 0.1f, 0.1f, 1.0f};   ///< 環境光色
	sgc::Colorf diffuse{0.8f, 0.8f, 0.8f, 1.0f};   ///< 拡散反射色
	sgc::Colorf specular{1.0f, 1.0f, 1.0f, 1.0f};  ///< 鏡面反射色
	float shininess = 32.0f;                          ///< 光沢度（Phong指数）

	/// @brief デフォルトマテリアルを作成する
	/// @return デフォルト値のマテリアル
	[[nodiscard]] static Material defaultMaterial() noexcept
	{
		return Material{};
	}

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::ostringstream oss;
		oss << R"json({"ambient":[)json"
			<< ambient.r << "," << ambient.g << ","
			<< ambient.b << "," << ambient.a
			<< R"json(],"diffuse":[)json"
			<< diffuse.r << "," << diffuse.g << ","
			<< diffuse.b << "," << diffuse.a
			<< R"json(],"specular":[)json"
			<< specular.r << "," << specular.g << ","
			<< specular.b << "," << specular.a
			<< R"json(],"shininess":)json" << shininess << "}";
		return oss.str();
	}
};

} // namespace mitiru::render
