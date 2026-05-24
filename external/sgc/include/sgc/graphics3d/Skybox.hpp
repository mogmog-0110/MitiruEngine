#pragma once

/// @file Skybox.hpp
/// @brief スカイボックス / 環境マッピングユーティリティ
///
/// スカイボックス頂点生成、キューブマップフェイス列挙、
/// 手続き的スカイカラー勾配生成を提供する。
///
/// @code
/// auto skybox = sgc::graphics3d::createSkyboxData(
///     "sky_right", "sky_left", "sky_top", "sky_bottom", "sky_front", "sky_back");
/// // skybox.vertices で単位キューブの頂点36個を取得
/// // skybox.faceTextures でフェイスごとのテクスチャIDを取得
///
/// auto color = sgc::graphics3d::calculateSkyColor(
///     sunDir, viewDir, skyParams);
/// @endcode

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::graphics3d
{

/// @brief キューブマップのフェイス
enum class CubeFace : int32_t
{
	PositiveX = 0,  ///< +X（右）
	NegativeX,      ///< -X（左）
	PositiveY,      ///< +Y（上）
	NegativeY,      ///< -Y（下）
	PositiveZ,      ///< +Z（前）
	NegativeZ,      ///< -Z（後）
	Count           ///< フェイス数（番兵値）
};

/// @brief スカイボックスの頂点データ
struct SkyboxVertex
{
	Vec3f position;  ///< 頂点位置（単位キューブ上）
};

/// @brief スカイボックスデータ
struct SkyboxData
{
	std::vector<SkyboxVertex> vertices;                                        ///< 頂点配列（36頂点）
	std::array<std::string, static_cast<std::size_t>(CubeFace::Count)> faceTextures;  ///< フェイスごとのテクスチャID
};

/// @brief 手続き的スカイの設定
struct SkyParams
{
	Colorf zenithColor{0.1f, 0.3f, 0.8f, 1.0f};    ///< 天頂の色
	Colorf horizonColor{0.6f, 0.8f, 1.0f, 1.0f};   ///< 地平線の色
	Colorf sunColor{1.0f, 0.95f, 0.8f, 1.0f};      ///< 太陽の色
	float sunSize = 0.05f;                            ///< 太陽の角度サイズ（ラジアン）
	float sunIntensity = 2.0f;                        ///< 太陽の強度
	float atmosphereFalloff = 2.0f;                   ///< 大気散乱の減衰指数
};

/// @brief 単位キューブの頂点データを生成する（スカイボックス用）
/// @return 36頂点の配列（6面×2三角形×3頂点）
[[nodiscard]] inline std::vector<SkyboxVertex> generateSkyboxVertices()
{
	// 単位キューブの頂点8個
	constexpr Vec3f v0{-1.0f, -1.0f, -1.0f};
	constexpr Vec3f v1{ 1.0f, -1.0f, -1.0f};
	constexpr Vec3f v2{ 1.0f,  1.0f, -1.0f};
	constexpr Vec3f v3{-1.0f,  1.0f, -1.0f};
	constexpr Vec3f v4{-1.0f, -1.0f,  1.0f};
	constexpr Vec3f v5{ 1.0f, -1.0f,  1.0f};
	constexpr Vec3f v6{ 1.0f,  1.0f,  1.0f};
	constexpr Vec3f v7{-1.0f,  1.0f,  1.0f};

	// 6面×2三角形×3頂点 = 36頂点（内向き：スカイボックスは内側から見る）
	return {
		// +X面（右）
		{v1}, {v5}, {v6},  {v6}, {v2}, {v1},
		// -X面（左）
		{v4}, {v0}, {v3},  {v3}, {v7}, {v4},
		// +Y面（上）
		{v3}, {v2}, {v6},  {v6}, {v7}, {v3},
		// -Y面（下）
		{v4}, {v5}, {v1},  {v1}, {v0}, {v4},
		// +Z面（前）
		{v5}, {v4}, {v7},  {v7}, {v6}, {v5},
		// -Z面（後）
		{v0}, {v1}, {v2},  {v2}, {v3}, {v0},
	};
}

/// @brief スカイボックスデータを生成する
/// @param posX +Xフェイスのテクスチャ名
/// @param negX -Xフェイスのテクスチャ名
/// @param posY +Yフェイスのテクスチャ名
/// @param negY -Yフェイスのテクスチャ名
/// @param posZ +Zフェイスのテクスチャ名
/// @param negZ -Zフェイスのテクスチャ名
/// @return スカイボックスデータ
[[nodiscard]] inline SkyboxData createSkyboxData(
	const std::string& posX,
	const std::string& negX,
	const std::string& posY,
	const std::string& negY,
	const std::string& posZ,
	const std::string& negZ)
{
	SkyboxData data;
	data.vertices = generateSkyboxVertices();
	data.faceTextures[static_cast<std::size_t>(CubeFace::PositiveX)] = posX;
	data.faceTextures[static_cast<std::size_t>(CubeFace::NegativeX)] = negX;
	data.faceTextures[static_cast<std::size_t>(CubeFace::PositiveY)] = posY;
	data.faceTextures[static_cast<std::size_t>(CubeFace::NegativeY)] = negY;
	data.faceTextures[static_cast<std::size_t>(CubeFace::PositiveZ)] = posZ;
	data.faceTextures[static_cast<std::size_t>(CubeFace::NegativeZ)] = negZ;
	return data;
}

/// @brief 手続き的スカイカラーを計算する
///
/// 太陽方向と視線方向に基づき、大気散乱風のスカイカラーを返す。
/// @param sunDir 太陽方向（正規化済み）
/// @param viewDir 視線方向（正規化済み）
/// @param params スカイパラメータ
/// @return 計算されたスカイカラー
[[nodiscard]] inline Colorf calculateSkyColor(
	const Vec3f& sunDir,
	const Vec3f& viewDir,
	const SkyParams& params)
{
	// 視線の仰角に基づく天頂-地平線のブレンド
	const float elevation = viewDir.y;  // [-1, 1]
	const float t = (elevation + 1.0f) * 0.5f;  // [0, 1]

	// 大気散乱の減衰
	const float atmosphereT = std::pow(t, 1.0f / params.atmosphereFalloff);

	// 天頂色と地平線色をブレンド
	Colorf skyColor{
		params.horizonColor.r + (params.zenithColor.r - params.horizonColor.r) * atmosphereT,
		params.horizonColor.g + (params.zenithColor.g - params.horizonColor.g) * atmosphereT,
		params.horizonColor.b + (params.zenithColor.b - params.horizonColor.b) * atmosphereT,
		1.0f
	};

	// 太陽光の寄与
	const float sunDot = viewDir.dot(sunDir);
	if (sunDot > 0.0f)
	{
		// 太陽ディスク（角度が小さいほど明るい）
		const float sunAngle = std::acos(
			sunDot > 1.0f ? 1.0f : sunDot);
		if (sunAngle < params.sunSize)
		{
			// 太陽ディスク内：太陽色を強く加算
			const float sunFactor = (1.0f - sunAngle / params.sunSize) * params.sunIntensity;
			skyColor.r += params.sunColor.r * sunFactor;
			skyColor.g += params.sunColor.g * sunFactor;
			skyColor.b += params.sunColor.b * sunFactor;
		}
		else
		{
			// 太陽周辺のグロー
			const float glowFactor = std::pow(sunDot, 8.0f) * 0.3f;
			skyColor.r += params.sunColor.r * glowFactor;
			skyColor.g += params.sunColor.g * glowFactor;
			skyColor.b += params.sunColor.b * glowFactor;
		}
	}

	// [0, 1]にクランプ
	auto clampf = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
	skyColor.r = clampf(skyColor.r);
	skyColor.g = clampf(skyColor.g);
	skyColor.b = clampf(skyColor.b);

	return skyColor;
}

/// @brief キューブマップフェイスの方向ベクトルを取得する
/// @param face キューブマップフェイス
/// @return フェイスの法線方向ベクトル
[[nodiscard]] inline Vec3f getCubeFaceDirection(CubeFace face)
{
	switch (face)
	{
	case CubeFace::PositiveX: return Vec3f::right();
	case CubeFace::NegativeX: return Vec3f::left();
	case CubeFace::PositiveY: return Vec3f::up();
	case CubeFace::NegativeY: return Vec3f::down();
	case CubeFace::PositiveZ: return Vec3f::back();
	case CubeFace::NegativeZ: return Vec3f::forward();
	default: return Vec3f::zero();
	}
}

/// @brief キューブマップフェイスの上方向ベクトルを取得する
/// @param face キューブマップフェイス
/// @return フェイスの上方向ベクトル
[[nodiscard]] inline Vec3f getCubeFaceUp(CubeFace face)
{
	switch (face)
	{
	case CubeFace::PositiveX: return Vec3f::up();
	case CubeFace::NegativeX: return Vec3f::up();
	case CubeFace::PositiveY: return Vec3f::back();
	case CubeFace::NegativeY: return Vec3f::forward();
	case CubeFace::PositiveZ: return Vec3f::up();
	case CubeFace::NegativeZ: return Vec3f::up();
	default: return Vec3f::up();
	}
}

} // namespace sgc::graphics3d
