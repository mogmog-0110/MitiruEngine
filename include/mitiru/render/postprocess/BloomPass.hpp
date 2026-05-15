#pragma once

/// @file BloomPass.hpp
/// @brief ブルームパス（輝度抽出 → ガウスブラー → 加算合成）

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string_view>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>
#include <mitiru/render/postprocess/GaussianBlurPass.hpp>

namespace mitiru::render
{

/// @brief ブルームの設定
struct BloomConfig
{
	float threshold = 0.8f;      ///< 輝度抽出閾値
	float intensity = 1.0f;      ///< ブルーム強度
	int blurRadius = 8;          ///< ブラー半径
};

/// @brief ブルームパス
/// @details 輝度抽出 → ガウスブラー → シーンとの加算合成
class BloomPass final : public PostProcessPass
{
public:
	/// @brief コンストラクタ
	BloomPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler,
		std::uint32_t screenW,
		std::uint32_t screenH)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_extractPS = compilePostProcessPS(
			device, PP_BLOOM_EXTRACT_PS);
		m_combinePS = compilePostProcessPS(
			device, PP_BLOOM_COMBINE_PS);
		m_extractCB = createConstantBuffer(
			device, sizeof(BloomExtractCB));

		m_brightRT = createRenderTarget(device, screenW, screenH);
		m_blurredRT = createRenderTarget(device, screenW, screenH);

		m_blurPass = std::make_unique<GaussianBlurPass>(
			device, fullscreenVS, sampler, screenW, screenH);
	}

	/// @brief ブルーム設定を変更する
	void setConfig(const BloomConfig& cfg) noexcept
	{
		m_config = cfg;
		GaussianBlurConfig blurCfg;
		blurCfg.radius = cfg.blurRadius;
		blurCfg.sigma = static_cast<float>(cfg.blurRadius) / 2.0f;
		m_blurPass->setConfig(blurCfg);
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		/// ステップ1: 輝度抽出（scene → brightRT）
		BloomExtractCB extractData = {};
		extractData.threshold = m_config.threshold;
		extractData.intensity = m_config.intensity;
		updateConstantBuffer(context, m_extractCB.Get(),
			&extractData, sizeof(extractData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_extractPS.Get(),
			inputSRV, m_brightRT.rtv.Get(),
			m_sampler.Get(), m_extractCB.Get(),
			screenW, screenH);

		/// ステップ2: ガウスブラー（brightRT → blurredRT）
		m_blurPass->applyBlur(context,
			m_brightRT.srv.Get(), m_blurredRT.rtv.Get(),
			screenW, screenH);

		/// ステップ3: 加算合成（scene + blurred → output）
		/// スロット0にシーン、スロット1にブルーム
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);
		context->OMSetRenderTargets(1, &outputRTV, nullptr);
		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_combinePS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11ShaderResourceView* srvs[2] = {
			inputSRV, m_blurredRT.srv.Get()
		};
		context->PSSetShaderResources(0, 2, srvs);
		auto* samplerPtr = m_sampler.Get();
		context->PSSetSamplers(0, 1, &samplerPtr);

		context->Draw(3, 0);

		/// SRVバインド解除
		ID3D11ShaderResourceView* nullSRVs[2] = {
			nullptr, nullptr
		};
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "Bloom";
	}

private:
	/// @brief 輝度抽出用定数バッファレイアウト
	struct BloomExtractCB
	{
		float threshold;       ///< 輝度閾値
		float intensity;       ///< 強度
		float padding[2];      ///< アライメントパディング
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_extractPS;
	ComPtr<ID3D11PixelShader> m_combinePS;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_extractCB;
	PostProcessRT m_brightRT;
	PostProcessRT m_blurredRT;
	std::unique_ptr<GaussianBlurPass> m_blurPass;
	BloomConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
