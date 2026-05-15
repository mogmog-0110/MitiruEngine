#pragma once

/// @file LightArrayCB.hpp
/// @brief マルチライト用定数バッファのパッキング
/// @details `IRenderer3D::setLights(span<Light>)` から GPU 側 cbuffer
///          `CbLightArray (b2)` に詰める CPU 表現と変換関数。
///
///          レイアウト（HLSL のアラインメント規則に合わせて 16 byte 境界）:
///
///          ```
///          cbuffer CbLightArray : register(b2)
///          {
///              int    lightCount;     // 実有効ライト数 (0..kMaxLights)
///              int3   _pad0;
///              float4 ambientColor;   // シーン環境光 (rgb, a 無視)
///              struct {
///                  float4 typeAndIntensity; // x=type, y=intensity,
///                                           // z=spotInnerCos, w=spotOuterCos
///                  float4 position;         // xyz=ワールド位置, w 未使用
///                  float4 direction;        // xyz=向き (Dir/Spot), w 未使用
///                  float4 color;            // rgb=色, a=range (Point/Spot)
///              } lights[kMaxLights];
///          };
///          ```
///
///          1 ライト = 64 byte × 8 = 512 byte + ヘッダ 32 byte = 544 byte。

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <span>

#include <sgc/types/Color.hpp>

#include <mitiru/render/Light.hpp>

namespace mitiru::render
{

/// @brief 1 ライト分のパック済みデータ（64 byte）
/// @details HLSL の cbuffer alignment と一致させるため、各メンバは 16 byte。
struct alignas(16) LightEntryGpu
{
	float typeAndIntensity[4]{};  ///< x=type(0=Dir/1=Point/2=Spot), y=intensity, z=spotInnerCos, w=spotOuterCos
	float position[4]{};          ///< xyz=position (Point/Spot)
	float direction[4]{};         ///< xyz=direction (Dir/Spot)
	float color[4]{};             ///< rgb=color, a=range
};

/// @brief マルチライト定数バッファ（544 byte / 16 byte aligned）
struct alignas(16) LightArrayCB
{
	/// @brief CB のサポートする最大ライト数
	static constexpr int kMaxLights = 8;

	std::int32_t count = 0;          ///< 有効ライト数
	std::int32_t _pad0[3]{};         ///< 16 byte 境界揃え
	float        ambient[4]{};       ///< シーン環境光 rgb
	LightEntryGpu lights[kMaxLights]{};

	/// @brief Light 配列と環境光から CB を構築する
	/// @param srcLights ライト span（kMaxLights を超える分は無視）
	/// @param sceneAmbient シーン環境光（A は無視）
	/// @return パック済み CB
	[[nodiscard]] static LightArrayCB fromLights(
		std::span<const Light> srcLights,
		const sgc::Colorf& sceneAmbient) noexcept
	{
		LightArrayCB cb;
		cb.ambient[0] = sceneAmbient.r;
		cb.ambient[1] = sceneAmbient.g;
		cb.ambient[2] = sceneAmbient.b;
		cb.ambient[3] = 1.0f;

		const int n = std::min(
			static_cast<int>(srcLights.size()),
			kMaxLights);
		cb.count = n;

		for (int i = 0; i < n; ++i)
		{
			pack(cb.lights[i], srcLights[i]);
		}
		return cb;
	}

private:
	/// @brief 1 ライトを LightEntryGpu に packing する
	static void pack(LightEntryGpu& dst, const Light& src) noexcept
	{
		dst.typeAndIntensity[0] = static_cast<float>(static_cast<int>(src.type));
		dst.typeAndIntensity[1] = src.intensity;

		if (src.type == LightType::Spot)
		{
			// spotAngle は度。HLSL 側で `dot(L, -dir) > cos(half_angle)` を
			// 使うため、半角の cos を事前に計算しておく。
			// inner = 0.9 * outer で柔らかいフォールオフを与える default。
			const float halfAngleRad =
				(src.spotAngle * 0.5f) * (3.14159265358979323846f / 180.0f);
			const float outerCos = std::cos(halfAngleRad);
			const float innerCos = std::cos(halfAngleRad * 0.9f);
			dst.typeAndIntensity[2] = innerCos;
			dst.typeAndIntensity[3] = outerCos;
		}
		else
		{
			dst.typeAndIntensity[2] = 1.0f;
			dst.typeAndIntensity[3] = 1.0f;
		}

		dst.position[0] = src.position.x;
		dst.position[1] = src.position.y;
		dst.position[2] = src.position.z;
		dst.position[3] = 1.0f;

		dst.direction[0] = src.direction.x;
		dst.direction[1] = src.direction.y;
		dst.direction[2] = src.direction.z;
		dst.direction[3] = 0.0f;

		dst.color[0] = src.color.r;
		dst.color[1] = src.color.g;
		dst.color[2] = src.color.b;
		dst.color[3] = src.range; // a 成分に range を載せる（Dir では未使用）
	}
};

// HLSL alignment 確認の静的アサート
static_assert(sizeof(LightEntryGpu) == 64,
	"LightEntryGpu must be 64 bytes for HLSL CB alignment");
static_assert(alignof(LightEntryGpu) == 16,
	"LightEntryGpu must be 16-byte aligned");
static_assert(sizeof(LightArrayCB) == 32 + 64 * LightArrayCB::kMaxLights,
	"LightArrayCB byte size mismatch — HLSL cbuffer layout will break");

} // namespace mitiru::render
