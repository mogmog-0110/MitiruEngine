#pragma once

/// @file MultiLightSystem.hpp
/// @brief マルチライトフォワードレンダリングシステム（DX11）
/// @details 最大16灯のライト（ディレクショナル・ポイント・スポット）を同時に適用する
///          フォワードレンダリングシステム。定数バッファ register(b2) を使用し、
///          既存の CbTransform(b0) / CbLighting(b1) と競合しない。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Light.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

#endif // _WIN32

namespace mitiru::render
{

/// @brief マルチライト用の最大ライト数
inline constexpr int kMaxLights = 16;

/// @brief GPU側ライトデータ（16バイトアライメント）
struct alignas(16) GpuLightData
{
	float position[3]{};   ///< 位置 (Point/Spot)
	float type = 0.0f;     ///< 0=Directional, 1=Point, 2=Spot

	float direction[3]{};  ///< 方向 (Directional/Spot)
	float range = 100.0f;  ///< 到達距離 (Point/Spot)

	float color[4]{};      ///< ライト色 (rgb) + 強度 (w)

	float innerCone = 0.0f;   ///< スポット内側コーン角（cos値）
	float outerCone = 0.0f;   ///< スポット外側コーン角（cos値）
	float padding[2]{};       ///< パディング
};

/// @brief マルチライト用定数バッファ（CbLights: register(b2)）
struct alignas(16) CbLights
{
	GpuLightData lights[kMaxLights]{};  ///< ライト配列
	int lightCount = 0;                 ///< アクティブなライト数
	float ambientColor[3]{0.15f, 0.15f, 0.15f};  ///< アンビエント色
};

/// @brief マルチライト対応Phongピクセルシェーダー（HLSL SM5.0）
/// @details 最大16灯のライトを反復処理し、ライト種別ごとの減衰を適用する。
///          CbLighting(b1) のマテリアル情報と CbLights(b2) のライト情報を参照する。
constexpr const char* kMultiLightPS = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;
    float  _pad0;
    float3 LightColor;
    float  _pad1;
    float3 AmbientColorLegacy;
    float  _pad2;
    float3 CameraPos;
    float  _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
};

struct LightData
{
    float3 position;
    float  type;     // 0=Directional, 1=Point, 2=Spot
    float3 direction;
    float  range;
    float4 color;    // rgb + intensity in w
    float  innerCone;
    float  outerCone;
    float2 padding;
};

cbuffer CbLights : register(b2)
{
    LightData lights[16];
    int   lightCount;
    float3 ambientColor;
};

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 V = normalize(CameraPos - input.WorldPos);

    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    for (int i = 0; i < lightCount; i++)
    {
        float3 lightColor = lights[i].color.rgb * lights[i].color.w;
        float3 L;
        float attenuation = 1.0;

        if (lights[i].type < 0.5)
        {
            // Directional light
            L = normalize(-lights[i].direction);
        }
        else
        {
            // Point or Spot light
            float3 toLight = lights[i].position - input.WorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);

            // Distance attenuation with range cutoff
            float rangeSq = lights[i].range * lights[i].range;
            attenuation = max(1.0 - (dist * dist) / rangeSq, 0.0);
            attenuation *= attenuation;

            if (lights[i].type > 1.5)
            {
                // Spot light: cone angle falloff
                float cosAngle = dot(-L, normalize(lights[i].direction));
                float spotFactor = saturate(
                    (cosAngle - lights[i].outerCone) /
                    max(lights[i].innerCone - lights[i].outerCone, 0.0001));
                attenuation *= spotFactor * spotFactor;
            }
        }

        // Diffuse
        float NdotL = max(dot(N, L), 0.0);
        totalDiffuse += lightColor * NdotL * attenuation;

        // Specular (Blinn-Phong)
        float3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float specFactor = pow(NdotH, MaterialShininess);
        totalSpecular += lightColor * MaterialSpecular.rgb * specFactor * attenuation;
    }

    float3 ambient = ambientColor * MaterialDiffuse.rgb;
    float3 diffuse = totalDiffuse * MaterialDiffuse.rgb;
    float3 finalColor = ambient + diffuse + totalSpecular;
    float alpha = MaterialDiffuse.a * input.Color.a;

    return float4(finalColor * input.Color.rgb, alpha);
}
)hlsl";

#ifdef _WIN32

/// @brief マルチライトフォワードレンダリングシステム
/// @details 最大16灯のライトを管理し、定数バッファ(b2)経由でGPUに転送する。
///          Renderer3Dと組み合わせて使用する。
///
/// @code
/// mitiru::render::MultiLightSystem multiLight;
/// multiLight.init(d3dDevice, d3dContext);
///
/// // ライトを設定する
/// multiLight.setLights({
///     Light::directional({0, -1, 0.5f}),
///     Light::point({5, 3, 0}, 50.0f),
///     Light::spot({0, 10, 0}, {0, -1, 0}, 30.0f, 80.0f),
/// });
/// multiLight.setAmbient({0.1f, 0.1f, 0.15f});
///
/// // 描画時
/// multiLight.bind(d3dContext);
/// // ... drawMesh() with kMultiLightPS pixel shader ...
/// multiLight.unbind(d3dContext);
/// @endcode
class MultiLightSystem
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	MultiLightSystem() noexcept = default;

	/// @brief 初期化済みかどうか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	/// @brief アクティブなライト数を取得する
	[[nodiscard]] int lightCount() const noexcept { return m_lightCount; }

	/// @brief 初期化する
	/// @param device DX11デバイス
	void init(ID3D11Device* device)
	{
		if (!device)
		{
			return;
		}

		m_device = device;
		createLightConstantBuffer();
		m_initialized = true;
	}

	/// @brief ライトを設定する
	/// @param lights ライト配列（最大16灯、超過分は切り捨て）
	void setLights(const std::vector<Light>& lights)
	{
		m_lightCount = static_cast<int>(
			(std::min)(lights.size(), static_cast<size_t>(kMaxLights)));

		for (int i = 0; i < m_lightCount; ++i)
		{
			convertLight(lights[static_cast<size_t>(i)], m_gpuLights[static_cast<size_t>(i)]);
		}

		m_dirty = true;
	}

	/// @brief アンビエント色を設定する
	/// @param color アンビエント色
	void setAmbient(const sgc::Colorf& color) noexcept
	{
		m_ambientColor = color;
		m_dirty = true;
	}

	/// @brief ライト定数バッファをバインドする（register(b2)）
	/// @param context DX11デバイスコンテキスト
	void bind(ID3D11DeviceContext* context)
	{
		if (!context || !m_initialized)
		{
			return;
		}

		if (m_dirty)
		{
			uploadLightBuffer(context);
			m_dirty = false;
		}

		ID3D11Buffer* buf = m_cbLights.Get();
		context->PSSetConstantBuffers(2, 1, &buf);
	}

	/// @brief ライト定数バッファをアンバインドする
	/// @param context DX11デバイスコンテキスト
	void unbind(ID3D11DeviceContext* context)
	{
		if (!context)
		{
			return;
		}

		ID3D11Buffer* nullBuf = nullptr;
		context->PSSetConstantBuffers(2, 1, &nullBuf);
	}

private:
	/// @brief Light構造体からGPU用データに変換する
	static void convertLight(const Light& src, GpuLightData& dst)
	{
		dst.position[0] = src.position.x;
		dst.position[1] = src.position.y;
		dst.position[2] = src.position.z;

		switch (src.type)
		{
		case LightType::Directional: dst.type = 0.0f; break;
		case LightType::Point:       dst.type = 1.0f; break;
		case LightType::Spot:        dst.type = 2.0f; break;
		}

		dst.direction[0] = src.direction.x;
		dst.direction[1] = src.direction.y;
		dst.direction[2] = src.direction.z;

		dst.range = src.range;

		dst.color[0] = src.color.r;
		dst.color[1] = src.color.g;
		dst.color[2] = src.color.b;
		dst.color[3] = src.intensity;

		/// スポット角度をcos値に変換する（シェーダーで比較に使用）
		constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
		const float halfAngle = src.spotAngle * 0.5f * kDeg2Rad;
		dst.innerCone = std::cos(halfAngle * 0.8f);  ///< 内側コーン（角度の80%）
		dst.outerCone = std::cos(halfAngle);          ///< 外側コーン

		dst.padding[0] = 0.0f;
		dst.padding[1] = 0.0f;
	}

	/// @brief ライト定数バッファを作成する
	void createLightConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbLights);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbLights.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("MultiLightSystem: CreateBuffer(CbLights) failed");
		}
	}

	/// @brief ライトデータをGPUにアップロードする
	void uploadLightBuffer(ID3D11DeviceContext* context)
	{
		CbLights cb{};
		for (int i = 0; i < m_lightCount; ++i)
		{
			cb.lights[i] = m_gpuLights[static_cast<size_t>(i)];
		}
		cb.lightCount = m_lightCount;
		cb.ambientColor[0] = m_ambientColor.r;
		cb.ambientColor[1] = m_ambientColor.g;
		cb.ambientColor[2] = m_ambientColor.b;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_cbLights.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			context->Unmap(m_cbLights.Get(), 0);
		}
	}

	ComPtr<ID3D11Device> m_device;
	bool m_initialized = false;
	bool m_dirty = true;
	int m_lightCount = 0;

	/// @brief ライトデータ
	std::array<GpuLightData, kMaxLights> m_gpuLights{};
	sgc::Colorf m_ambientColor{0.15f, 0.15f, 0.15f, 1.0f};

	/// @brief 定数バッファ
	ComPtr<ID3D11Buffer> m_cbLights;
};

#endif // _WIN32

} // namespace mitiru::render
