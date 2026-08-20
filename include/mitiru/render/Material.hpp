#pragma once

/// @file Material.hpp
/// @brief マテリアル定義
/// @details 3Dオブジェクトの表面属性（環境光・拡散反射・鏡面反射・光沢度）を定義する。

#include <sstream>
#include <string>

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

// 前方宣言。バックエンドが Texture を bindless に読み取る経路。
class Texture;


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

	/// PBR拡張フィールド（glTFインポート等で使用）
	std::string diffuseTexturePath;                   ///< ディフューズテクスチャパス（空=なし）
	std::string normalTexturePath;                    ///< 法線マップパス（空=なし）
	float metallic = 0.0f;                            ///< PBR メタリック [0,1]
	float roughness = 1.0f;                           ///< PBR ラフネス [0,1]

	/// @brief 不透明度の扱い (glTF alphaMode 相当)
	enum class AlphaMode
	{
		Opaque,   ///< アルファを無視して不透明で描く
		Mask,     ///< alphaCutoff 未満の画素を捨てる (葉・柵・格子)
		Blend,    ///< 半透明として合成する
	};
	AlphaMode alphaMode = AlphaMode::Opaque;          ///< 不透明度の扱い
	float alphaCutoff = 0.5f;                         ///< Mask のしきい値
	bool doubleSided = false;                         ///< 真なら背面カリングを切る
	/// アルベドを最近傍で拾うか。ドット絵の資産で線形補間に溶かされるのを防ぐ。
	bool nearestFilter = false;

	/// @brief アルベド（ディフューズ）テクスチャ — 非所有ポインタ
	/// @details 非 null の時、バックエンドはこの `Texture` を GPU にアップロードして
	///          ピクセルシェーダーで t0 にバインドする。null なら 1x1 白テクスチャを
	///          代わりに使う（既存マテリアルとの後方互換）。
	///          所有権は呼び出し側に残る — drawMesh の間はポインタが有効である必要がある。
	const Texture* albedoTexture = nullptr;

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
