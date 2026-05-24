#pragma once

/// @file ShadowMap.hpp
/// @brief シャドウマッピングユーティリティ
///
/// ライトスペース行列計算、カスケードシャドウ設定、
/// PCFフィルタカーネル生成を提供する。
///
/// @code
/// auto cascades = sgc::graphics3d::calculateCascades(
///     camera, lightDir, 4, 0.1f, 100.0f);
/// for (const auto& c : cascades) {
///     // c.lightViewProj でシャドウマップを描画
/// }
///
/// auto kernel = sgc::graphics3d::generatePCFKernel(3);
/// // 3x3フィルタ用のオフセット配列
/// @endcode

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "sgc/graphics3d/Camera3D.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/math/Vec3.hpp"

namespace sgc::graphics3d
{

/// @brief シャドウカスケード1段の情報
struct ShadowCascade
{
	float nearPlane = 0.0f;       ///< カスケードの近距離
	float farPlane = 0.0f;        ///< カスケードの遠距離
	Mat4f lightViewProj;          ///< ライトスペースのビュー投影行列

	/// @brief 比較演算子
	[[nodiscard]] bool operator==(const ShadowCascade&) const noexcept = default;
};

/// @brief シャドウマップの設定
struct ShadowConfig
{
	int32_t cascadeCount = 4;           ///< カスケード数
	float shadowDistance = 100.0f;      ///< シャドウ最大距離
	float splitLambda = 0.5f;           ///< 対数/線形分割のブレンド係数 [0=線形, 1=対数]
	float orthoSize = 50.0f;            ///< 正射影のサイズ（半幅）
	float depthRange = 200.0f;          ///< 深度範囲
	float bias = 0.005f;                ///< 深度バイアス
	float normalBias = 0.02f;           ///< 法線バイアス
};

/// @brief PCFフィルタのサンプルオフセット
struct PCFSample
{
	float offsetX = 0.0f;  ///< Xオフセット（テクセル単位）
	float offsetY = 0.0f;  ///< Yオフセット（テクセル単位）
	float weight = 1.0f;   ///< サンプルウェイト
};

/// @brief 平行光源のライトスペースビュー行列を計算する
/// @param lightDir ライト方向（正規化済み）
/// @param center シャドウの中心位置
/// @return ライトのビュー行列
[[nodiscard]] inline Mat4f calculateLightViewMatrix(
	const Vec3f& lightDir,
	const Vec3f& center)
{
	// ライト位置をcenterの反対方向に配置
	const Vec3f lightPos = center - lightDir * 100.0f;

	// 上方向ベクトル（ライト方向がほぼY軸に平行ならZ軸を使用）
	Vec3f upDir = Vec3f::up();
	const float dotUp = std::abs(lightDir.dot(upDir));
	if (dotUp > 0.99f)
	{
		upDir = Vec3f::forward();
	}

	return Mat4f::lookAt(lightPos, center, upDir);
}

/// @brief 平行光源のライトスペースビュー投影行列を計算する
/// @param lightDir ライト方向（正規化済み）
/// @param center シャドウの中心位置
/// @param orthoSize 正射影の半サイズ
/// @param depthRange 深度範囲
/// @return ライトのビュー投影行列
[[nodiscard]] inline Mat4f calculateLightSpaceMatrix(
	const Vec3f& lightDir,
	const Vec3f& center,
	float orthoSize,
	float depthRange)
{
	const auto view = calculateLightViewMatrix(lightDir, center);
	const auto proj = Mat4f::orthographic(
		-orthoSize, orthoSize,
		-orthoSize, orthoSize,
		-depthRange * 0.5f, depthRange * 0.5f);
	return proj * view;
}

/// @brief カスケード分割距離を計算する（対数/線形ブレンド）
/// @param cascadeCount カスケード数
/// @param nearPlane カメラのニア面
/// @param farPlane シャドウ最大距離
/// @param lambda 対数/線形のブレンド係数 [0=線形, 1=対数]
/// @return 各カスケードの分割距離（cascadeCount+1個）
[[nodiscard]] inline std::vector<float> calculateSplitDistances(
	int32_t cascadeCount,
	float nearPlane,
	float farPlane,
	float lambda)
{
	std::vector<float> splits(static_cast<std::size_t>(cascadeCount) + 1);
	splits[0] = nearPlane;

	for (int32_t i = 1; i <= cascadeCount; ++i)
	{
		const float p = static_cast<float>(i) / static_cast<float>(cascadeCount);

		// 対数分割
		const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);

		// 線形分割
		const float linearSplit = nearPlane + (farPlane - nearPlane) * p;

		// ブレンド
		splits[static_cast<std::size_t>(i)] = lambda * logSplit + (1.0f - lambda) * linearSplit;
	}

	return splits;
}

/// @brief カスケードシャドウマップを計算する
/// @param camera カメラ
/// @param lightDir ライト方向（正規化済み）
/// @param config シャドウ設定
/// @return カスケード配列
[[nodiscard]] inline std::vector<ShadowCascade> calculateCascades(
	const Camera3D& camera,
	const Vec3f& lightDir,
	const ShadowConfig& config)
{
	const auto splits = calculateSplitDistances(
		config.cascadeCount,
		camera.nearClip,
		config.shadowDistance,
		config.splitLambda);

	std::vector<ShadowCascade> cascades;
	cascades.reserve(static_cast<std::size_t>(config.cascadeCount));

	const Vec3f camForward = camera.forward();

	for (int32_t i = 0; i < config.cascadeCount; ++i)
	{
		const float near = splits[static_cast<std::size_t>(i)];
		const float far = splits[static_cast<std::size_t>(i) + 1];

		// カスケードの中心をカメラ前方の中間距離に配置
		const float midDist = (near + far) * 0.5f;
		const Vec3f center = camera.position + camForward * midDist;

		// カスケード範囲に応じた正射影サイズ
		const float cascadeSize = (far - near) * 0.5f + config.orthoSize * 0.5f;

		ShadowCascade cascade;
		cascade.nearPlane = near;
		cascade.farPlane = far;
		cascade.lightViewProj = calculateLightSpaceMatrix(
			lightDir, center, cascadeSize, config.depthRange);
		cascades.push_back(cascade);
	}

	return cascades;
}

/// @brief PCFフィルタカーネルを生成する
/// @param kernelSize カーネルサイズ（奇数: 1, 3, 5, 7等）
/// @return PCFサンプルオフセット配列
///
/// 均一ウェイトのグリッドカーネルを生成する。
/// テクセルサイズは呼び出し側で乗算すること。
[[nodiscard]] inline std::vector<PCFSample> generatePCFKernel(int32_t kernelSize)
{
	// 奇数に補正
	if (kernelSize < 1) kernelSize = 1;
	if (kernelSize % 2 == 0) kernelSize += 1;

	const int32_t halfSize = kernelSize / 2;
	const float totalSamples = static_cast<float>(kernelSize * kernelSize);
	const float weight = 1.0f / totalSamples;

	std::vector<PCFSample> samples;
	samples.reserve(static_cast<std::size_t>(kernelSize * kernelSize));

	for (int32_t y = -halfSize; y <= halfSize; ++y)
	{
		for (int32_t x = -halfSize; x <= halfSize; ++x)
		{
			samples.push_back({
				static_cast<float>(x),
				static_cast<float>(y),
				weight
			});
		}
	}

	return samples;
}

} // namespace sgc::graphics3d
