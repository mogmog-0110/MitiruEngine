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
#include <mitiru/render/AdvancedPostProcess_shaders_tables.hpp>

namespace mitiru::render
{

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

		// SSAO メインパス: depth(t0) + normal(t1) -> 中間バッファ
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

		// SRV をアンバインドする
		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->PSSetShaderResources(0, 2, nullSRVs);

		// ブラーパス
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
			// cosine-weighted: 加速カーブでスケールする
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

		// bind: t0=current, t1=history, t2=motion
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

		// アンバインドする
		ID3D11ShaderResourceView* nullSRVs[3] = {};
		context->PSSetShaderResources(0, 3, nullSRVs);

		// 次フレーム用に現在の結果を history へコピーする
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

		// bind: t0=scene, t1=depth
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

		// アンバインドする
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
