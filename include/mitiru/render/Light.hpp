#pragma once

/// @file Light.hpp
/// @brief ライト定義
/// @details 3Dシーン照明用のライトデータ。ディレクショナル・ポイント・スポットに対応。

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief ライト種別
enum class LightType
{
	Directional,  ///< 平行光源（太陽光など）
	Point,        ///< 点光源（電球など）
	Spot          ///< スポットライト
};

/// @brief ライト定義
/// @details 3Dシーンで使用するライトの全パラメータを保持する。
///          ファクトリメソッドで主要なライト種別を簡単に生成できる。
///
/// @code
/// auto sun = mitiru::render::Light::directional({0, -1, 0.5f});
/// auto lamp = mitiru::render::Light::point({5, 3, 0}, 50.0f);
/// @endcode
struct Light
{
	LightType type = LightType::Directional;          ///< ライト種別
	sgc::Vec3f position{0.0f, 10.0f, 0.0f};          ///< 位置（Point/Spot用）
	sgc::Vec3f direction{0.0f, -1.0f, 0.0f};         ///< 方向（Directional/Spot用）
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};      ///< ライト色
	float intensity = 1.0f;                            ///< 強度
	float range = 100.0f;                              ///< 到達距離（Point/Spot用）
	float spotAngle = 45.0f;                           ///< スポット角度（度、Spot用）

	/// @brief ディレクショナルライトを作成する
	/// @param dir 光の方向ベクトル
	/// @param col ライト色
	/// @return ディレクショナルライト
	[[nodiscard]] static Light directional(
		const sgc::Vec3f& dir,
		const sgc::Colorf& col = {1.0f, 1.0f, 1.0f, 1.0f})
	{
		Light light;
		light.type = LightType::Directional;
		light.direction = dir;
		light.color = col;
		return light;
	}

	/// @brief ポイントライトを作成する
	/// @param pos 光源位置
	/// @param lightRange 到達距離
	/// @param col ライト色
	/// @return ポイントライト
	[[nodiscard]] static Light point(
		const sgc::Vec3f& pos,
		float lightRange = 100.0f,
		const sgc::Colorf& col = {1.0f, 1.0f, 1.0f, 1.0f})
	{
		Light light;
		light.type = LightType::Point;
		light.position = pos;
		light.range = lightRange;
		light.color = col;
		return light;
	}

	/// @brief スポットライトを作成する
	/// @param pos 光源位置
	/// @param dir 光の方向ベクトル
	/// @param angle スポット角度（度）
	/// @param lightRange 到達距離
	/// @param col ライト色
	/// @return スポットライト
	[[nodiscard]] static Light spot(
		const sgc::Vec3f& pos,
		const sgc::Vec3f& dir,
		float angle = 45.0f,
		float lightRange = 100.0f,
		const sgc::Colorf& col = {1.0f, 1.0f, 1.0f, 1.0f})
	{
		Light light;
		light.type = LightType::Spot;
		light.position = pos;
		light.direction = dir;
		light.spotAngle = angle;
		light.range = lightRange;
		light.color = col;
		return light;
	}
};

} // namespace mitiru::render
