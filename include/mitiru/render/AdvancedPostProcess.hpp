#pragma once

/// @file AdvancedPostProcess.hpp
/// @brief 高度なポストプロセスパス (SSAO, HDR Tone Mapping, TAA, DoF)

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string_view>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/render/PostProcess.hpp>

namespace mitiru::render
{

// ============================================================================
// HLSL — SSAO ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_SSAO_PS = R"hlsl(
Texture2D depthTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState pointSampler : register(s0);

cbuffer SSAOParams : register(b0)
{
	float4x4 projection;
	float4x4 invProjection;
	float4 samples[16];
	float2 noiseScale;
	float radius;
	float bias;
	float intensity;
	int kernelSize;
	float2 ssaoPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float3 reconstructViewPos(float2 uv) {
	float depth = depthTexture.Sample(pointSampler, uv).r;
	float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0); ndc.y = -ndc.y;
	float4 vp = mul(invProjection, ndc); return vp.xyz / vp.w;
}
float rand2d(float2 co) {
	return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 viewPos = reconstructViewPos(input.texCoord);
	float3 normal = normalize(
		normalTexture.Sample(pointSampler, input.texCoord).xyz * 2.0 - 1.0);

	float angle = rand2d(input.texCoord * noiseScale) * 6.283185;
	float3 randomVec = float3(cos(angle), sin(angle), 0.0);
	float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	float3x3 TBN = float3x3(tangent, cross(normal, tangent), normal);

	float occlusion = 0.0;
	for (int i = 0; i < kernelSize; ++i) {
		float3 samplePos = viewPos + mul(samples[i].xyz, TBN) * radius;
		float4 offset = mul(projection, float4(samplePos, 1.0));
		offset.xy = (offset.xy / offset.w) * 0.5 + 0.5;
		offset.y = 1.0 - offset.y;
		float sd = reconstructViewPos(offset.xy).z;
		float rc = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sd));
		occlusion += (sd >= samplePos.z + bias ? 1.0 : 0.0) * rc;
	}
	occlusion = 1.0 - (occlusion / float(kernelSize)) * intensity;
	return float4(occlusion, occlusion, occlusion, 1.0);
}
)hlsl";

constexpr std::string_view PP_SSAO_BLUR_PS = R"hlsl(
Texture2D ssaoTexture : register(t0);
SamplerState pointSampler : register(s0);

cbuffer BlurParams : register(b0)
{
	float2 texelSize;
	float2 blurPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float result = 0.0;
	for (int x = -2; x <= 2; ++x)
		for (int y = -2; y <= 2; ++y)
			result += ssaoTexture.Sample(pointSampler,
				input.texCoord + float2(x, y) * texelSize).r;
	return float4((result / 25.0).xxx, 1.0);
}
)hlsl";

// ============================================================================
// HLSL — HDR Tone Mapping ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_HDR_TONEMAP_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer HDRParams : register(b0)
{
	float exposure;
	int tonemapOp;      // 0=Reinhard,1=ACES,2=Uncharted2,3=Exposure
	float whitePoint;
	float gamma;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float3 reinhardTonemap(float3 c, float wp) {
	return c * (1.0 + c / (wp * wp)) / (1.0 + c);
}
float3 acesFilmic(float3 x) {
	float a=2.51, b=0.03, c=2.43, d=0.59, e=0.14;
	return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}
float3 uc2(float3 x) {
	float A=0.15,B=0.50,C=0.10,D=0.20,E=0.02,F=0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}
float3 uncharted2Tonemap(float3 c, float wp) {
	return uc2(c) / uc2(float3(wp,wp,wp));
}
float3 exposureTonemap(float3 c) { return 1.0 - exp(-c); }

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 color = sceneTexture.Sample(linearSampler,
		input.texCoord).rgb * exposure;

	if (tonemapOp == 0)      color = reinhardTonemap(color, whitePoint);
	else if (tonemapOp == 1) color = acesFilmic(color);
	else if (tonemapOp == 2) color = uncharted2Tonemap(color, whitePoint);
	else                     color = exposureTonemap(color);

	// gamma correction
	color = pow(max(color, 0.0), 1.0 / gamma);
	return float4(color, 1.0);
}
)hlsl";

// ============================================================================
// HLSL — TAA ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_TAA_PS = R"hlsl(
Texture2D currentTexture : register(t0);
Texture2D historyTexture : register(t1);
Texture2D motionTexture : register(t2);
SamplerState linearSampler : register(s0);

cbuffer TAAParams : register(b0)
{
	float blendFactor;
	float2 rcpFrame;
	float taaPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = input.texCoord;
	float2 motion = motionTexture.Sample(linearSampler, uv).rg;
	float3 current = currentTexture.Sample(linearSampler, uv).rgb;
	// neighborhood clamping (3x3 AABB)
	float3 nMin = 9999, nMax = -9999;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y) {
			float3 s = currentTexture.Sample(linearSampler,
				uv + float2(x, y) * rcpFrame).rgb;
			nMin = min(nMin, s); nMax = max(nMax, s);
		}
	float3 history = historyTexture.Sample(linearSampler, uv - motion).rgb;
	history = clamp(history, nMin, nMax);
	return float4(lerp(history, current, blendFactor), 1.0);
}
)hlsl";

// ============================================================================
// HLSL — Depth of Field ピクセルシェーダー
// ============================================================================

constexpr std::string_view PP_DOF_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer DoFParams : register(b0)
{
	float focusDistance;
	float aperture;
	float focalLength;
	float maxBlur;
	float2 rcpFrame;
	float nearPlane;
	float farPlane;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float linearDepth(float d) {
	return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}
float4 PSMain(PSInput input) : SV_TARGET
{
	float depth = depthTexture.Sample(linearSampler, input.texCoord).r;
	float linD = linearDepth(depth);
	float coc = clamp(abs(linD - focusDistance) * aperture / linD * focalLength,
		0.0, maxBlur);
	float3 color = 0; float tw = 0;
	int taps = clamp(int(coc * 4.0), 1, 8);
	for (int x = -taps; x <= taps; ++x)
		for (int y = -taps; y <= taps; ++y) {
			float dist = length(float2(x, y));
			if (dist > float(taps)) continue;
			float w = 1.0 / (1.0 + dist);
			color += sceneTexture.Sample(linearSampler,
				input.texCoord + float2(x,y) * rcpFrame * coc).rgb * w;
			tw += w;
		}
	return float4(color / max(tw, 0.001), 1.0);
}
)hlsl";

// ============================================================================
// SSAOConfig / SSAOPass
// ============================================================================

struct SSAOConfig
{
	float radius = 0.5f;
	float bias = 0.025f;
	float intensity = 1.0f;
	int kernelSize = 16;      ///< 8-16（CB上限16）
	int blurPasses = 1;        ///< SSAOブラーパス回数
};

class SSAOPass final : public PostProcessPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	SSAOPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler,
		std::uint32_t screenW,
		std::uint32_t screenH)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
		, m_device(device)
		, m_width(screenW)
		, m_height(screenH)
	{
		m_ps = compilePostProcessPS(device, PP_SSAO_PS);
		m_blurPS = compilePostProcessPS(device, PP_SSAO_BLUR_PS);
		m_cb = createConstantBuffer(device, sizeof(SSAOCB));
		m_blurCB = createConstantBuffer(device, sizeof(BlurCB));
		m_intermediate = createRenderTarget(device, screenW, screenH,
			DXGI_FORMAT_R8_UNORM);
		generateKernel();
	}

	void setConfig(const SSAOConfig& cfg) noexcept
	{
		m_config = cfg;
		m_config.kernelSize = std::clamp(m_config.kernelSize, 8, 16);
	}

	void setProjection(const float* proj4x4, const float* invProj4x4) noexcept
	{
		std::memcpy(m_projection, proj4x4, 64);
		std::memcpy(m_invProjection, invProj4x4, 64);
	}

	/// @brief 法線バッファSRVを設定する
	void setNormalSRV(ID3D11ShaderResourceView* normalSRV) noexcept
	{
		m_normalSRV = normalSRV;
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		// リサイズ検出: 中間バッファを再生成する
		if (screenW != m_width || screenH != m_height)
		{
			m_intermediate = createRenderTarget(m_device.Get(), screenW, screenH,
				DXGI_FORMAT_R8_UNORM);
			m_width = screenW;
			m_height = screenH;
		}

		SSAOCB cbData = {};
		std::memcpy(&cbData.projection, m_projection, 64);
		std::memcpy(&cbData.invProjection, m_invProjection, 64);
		for (int i = 0; i < 16; ++i)
			std::memcpy(cbData.samples[i], m_kernel[i].data(), 16);
		cbData.noiseScale[0] = static_cast<float>(screenW) / 4.0f;
		cbData.noiseScale[1] = static_cast<float>(screenH) / 4.0f;
		cbData.radius = m_config.radius;
		cbData.bias = m_config.bias;
		cbData.intensity = m_config.intensity;
		cbData.kernelSize = std::min(m_config.kernelSize, 16);
		updateConstantBuffer(context, m_cb.Get(), &cbData, sizeof(cbData));

		// SSAO main pass: depth(t0) + normal(t1) -> intermediate
		ID3D11ShaderResourceView* srvs[2] = { inputSRV, m_normalSRV };
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
		context->OMSetRenderTargets(1,
			m_intermediate.rtv.GetAddressOf(), nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_ps.Get(), nullptr, 0);
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
		context->Draw(3, 0);

		// Unbind SRVs
		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->PSSetShaderResources(0, 2, nullSRVs);

		// Blur pass
		BlurCB blurData = {};
		blurData.texelSize[0] = 1.0f / static_cast<float>(screenW);
		blurData.texelSize[1] = 1.0f / static_cast<float>(screenH);
		updateConstantBuffer(context, m_blurCB.Get(),
			&blurData, sizeof(blurData));
		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_blurPS.Get(),
			m_intermediate.srv.Get(), outputRTV,
			m_sampler.Get(), m_blurCB.Get(),
			screenW, screenH);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "SSAO";
	}

	[[nodiscard]] const SSAOConfig& config() const noexcept
	{
		return m_config;
	}

private:
	struct SSAOCB
	{
		float projection[16];
		float invProjection[16];
		float samples[16][4];
		float noiseScale[2];
		float radius;
		float bias;
		float intensity;
		int kernelSize;
		float pad[2];
	};

	struct BlurCB
	{
		float texelSize[2];
		float pad[2];
	};

	void generateKernel()
	{
		std::mt19937 rng(42);
		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
		std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);
		for (int i = 0; i < 16; ++i)
		{
			float x = distNeg(rng);
			float y = distNeg(rng);
			float z = dist01(rng);
			float len = std::sqrt(x * x + y * y + z * z);
			if (len < 0.0001f) len = 1.0f;
			x /= len; y /= len; z /= len;
			// cosine-weighted: scale by accelerating curve
			float scale = static_cast<float>(i) / 16.0f;
			scale = 0.1f + scale * scale * 0.9f;
			m_kernel[i] = { x * scale, y * scale, z * scale, 0.0f };
		}
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11PixelShader> m_blurPS;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	ComPtr<ID3D11Buffer> m_blurCB;
	PostProcessRT m_intermediate;
	SSAOConfig m_config;
	std::array<std::array<float, 4>, 16> m_kernel;
	float m_projection[16] = {};
	float m_invProjection[16] = {};
	ID3D11ShaderResourceView* m_normalSRV = nullptr;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
};

// ============================================================================
// HDRToneMappingConfig / HDRToneMappingPass
// ============================================================================

enum class ToneMapOperator
{
	Reinhard = 0,
	ACES = 1,
	Uncharted2 = 2,
	Exposure = 3
};

struct HDRConfig
{
	float exposure = 1.0f;
	ToneMapOperator op = ToneMapOperator::ACES;
	float whitePoint = 4.0f;
	float gamma = 2.2f;
};

class HDRToneMappingPass final : public PostProcessPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	HDRToneMappingPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_HDR_TONEMAP_PS);
		m_cb = createConstantBuffer(device, sizeof(HDRCB));
	}

	void setConfig(const HDRConfig& cfg) noexcept { m_config = cfg; }

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		HDRCB cbData = {};
		cbData.exposure = m_config.exposure;
		cbData.tonemapOp = static_cast<int>(m_config.op);
		cbData.whitePoint = m_config.whitePoint;
		cbData.gamma = m_config.gamma;
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_ps.Get(),
			inputSRV, outputRTV,
			m_sampler.Get(), m_cb.Get(),
			screenW, screenH);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "HDRToneMapping";
	}

	[[nodiscard]] const HDRConfig& config() const noexcept
	{
		return m_config;
	}

private:
	struct HDRCB
	{
		float exposure;
		int tonemapOp;
		float whitePoint;
		float gamma;
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	HDRConfig m_config;
};

// ============================================================================
// TAAConfig / TAAPass
// ============================================================================

struct TAAConfig
{
	float blendFactor = 0.1f;   ///< 0.05-0.2 推奨
	float jitterScale = 1.0f;
	bool enabled = true;
};

class TAAPass final : public PostProcessPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	TAAPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler,
		std::uint32_t screenW,
		std::uint32_t screenH)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_TAA_PS);
		m_cb = createConstantBuffer(device, sizeof(TAACB));
		m_historyBuffer = createRenderTarget(device, screenW, screenH);
	}

	void setConfig(const TAAConfig& cfg) noexcept { m_config = cfg; }

	/// @brief モーションベクターSRVを設定する
	void setMotionVectorSRV(
		ID3D11ShaderResourceView* motionSRV) noexcept
	{
		m_motionSRV = motionSRV;
	}

	/// @brief Halton(2,3)ジッターオフセットを取得する
	[[nodiscard]] std::array<float, 2> getJitter(
		std::uint32_t frameIndex,
		std::uint32_t screenW,
		std::uint32_t screenH) const noexcept
	{
		auto halton = [](std::uint32_t idx, std::uint32_t base) -> float {
			float f = 1.0f;
			float r = 0.0f;
			std::uint32_t i = idx;
			while (i > 0)
			{
				f /= static_cast<float>(base);
				r += f * static_cast<float>(i % base);
				i /= base;
			}
			return r;
		};
		std::uint32_t i = (frameIndex % 16) + 1;
		float jx = (halton(i, 2) - 0.5f) * m_config.jitterScale
			* 2.0f / static_cast<float>(screenW);
		float jy = (halton(i, 3) - 0.5f) * m_config.jitterScale
			* 2.0f / static_cast<float>(screenH);
		return { jx, jy };
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		if (!m_config.enabled) {
			/// TAA無効時: 入力をそのまま出力にコピーする
			ComPtr<ID3D11Resource> inputRes;
			inputSRV->GetResource(inputRes.GetAddressOf());
			ComPtr<ID3D11Resource> outputRes;
			outputRTV->GetResource(outputRes.GetAddressOf());
			context->CopyResource(outputRes.Get(), inputRes.Get());
			return;
		}

		TAACB cbData = {};
		cbData.blendFactor = m_config.blendFactor;
		cbData.rcpFrame[0] = 1.0f / static_cast<float>(screenW);
		cbData.rcpFrame[1] = 1.0f / static_cast<float>(screenH);
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		// Bind: t0=current, t1=history, t2=motion
		ID3D11ShaderResourceView* srvs[3] = {
			inputSRV, m_historyBuffer.srv.Get(), m_motionSRV
		};
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
		context->OMSetRenderTargets(1, &outputRTV, nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_ps.Get(), nullptr, 0);
		context->PSSetShaderResources(0, 3, srvs);
		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
		context->Draw(3, 0);

		// Unbind
		ID3D11ShaderResourceView* nullSRVs[3] = {};
		context->PSSetShaderResources(0, 3, nullSRVs);

		// Copy current result to history for next frame
		ComPtr<ID3D11Resource> outputRes;
		outputRTV->GetResource(outputRes.GetAddressOf());
		context->CopyResource(
			m_historyBuffer.texture.Get(), outputRes.Get());
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "TAA";
	}

	[[nodiscard]] const TAAConfig& config() const noexcept
	{
		return m_config;
	}

private:
	struct TAACB
	{
		float blendFactor;
		float rcpFrame[2];
		float pad;
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	PostProcessRT m_historyBuffer;
	TAAConfig m_config;
	ID3D11ShaderResourceView* m_motionSRV = nullptr;
};

// ============================================================================
// DoFConfig / DepthOfFieldPass
// ============================================================================

enum class BokehShape
{
	Circle,
	Hexagon
};

struct DoFConfig
{
	float focusDistance = 10.0f;
	float aperture = 0.1f;
	float focalLength = 50.0f;
	float maxBlur = 8.0f;
	float nearPlane = 0.1f;
	float farPlane = 1000.0f;
	BokehShape bokehShape = BokehShape::Circle;
};

class DepthOfFieldPass final : public PostProcessPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	DepthOfFieldPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_DOF_PS);
		m_cb = createConstantBuffer(device, sizeof(DoFCB));
	}

	void setConfig(const DoFConfig& cfg) noexcept { m_config = cfg; }

	/// @brief 深度バッファSRVを設定する
	void setDepthSRV(
		ID3D11ShaderResourceView* depthSRV) noexcept
	{
		m_depthSRV = depthSRV;
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		DoFCB cbData = {};
		cbData.focusDistance = m_config.focusDistance;
		cbData.aperture = m_config.aperture;
		cbData.focalLength = m_config.focalLength / 1000.0f;
		cbData.maxBlur = m_config.maxBlur;
		cbData.rcpFrame[0] = 1.0f / static_cast<float>(screenW);
		cbData.rcpFrame[1] = 1.0f / static_cast<float>(screenH);
		cbData.nearPlane = m_config.nearPlane;
		cbData.farPlane = m_config.farPlane;
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		// Bind: t0=scene, t1=depth
		ID3D11ShaderResourceView* srvs[2] = {
			inputSRV, m_depthSRV
		};
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
		context->OMSetRenderTargets(1, &outputRTV, nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_ps.Get(), nullptr, 0);
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		context->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
		context->Draw(3, 0);

		// Unbind
		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "DepthOfField";
	}

	[[nodiscard]] const DoFConfig& config() const noexcept
	{
		return m_config;
	}

private:
	struct DoFCB
	{
		float focusDistance;
		float aperture;
		float focalLength;
		float maxBlur;
		float rcpFrame[2];
		float nearPlane;
		float farPlane;
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	DoFConfig m_config;
	ID3D11ShaderResourceView* m_depthSRV = nullptr;
};

} // namespace mitiru::render

#endif // _WIN32
