#pragma once

/// @file TransitionEffects.hpp
/// @brief トランジションエフェクト（フェード・フロストグラス）

#ifdef _WIN32

#include <cstdint>
#include <string_view>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>

namespace mitiru::render
{

// ============================================================================
// FadePass。スクリーンフェード
// ============================================================================

/// @brief フェードの設定
struct FadeConfig
{
	float colorR = 0.0f;       ///< フェード色R
	float colorG = 0.0f;       ///< フェード色G
	float colorB = 0.0f;       ///< フェード色B
	float colorA = 1.0f;       ///< フェード色A
	float progress = 0.0f;     ///< フェード進行度（0=シーン、1=フェード色）
};

/// @brief スクリーンフェードパス
/// @details シーンとソリッドカラーの間をリニア補間する。
class FadePass final : public PostProcessPass
{
public:
	FadePass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_FADE_PS);
		m_cb = createConstantBuffer(device, sizeof(FadeCB));
	}

	void setConfig(const FadeConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		FadeCB cbData = {};
		cbData.fadeColor[0] = m_config.colorR;
		cbData.fadeColor[1] = m_config.colorG;
		cbData.fadeColor[2] = m_config.colorB;
		cbData.fadeColor[3] = m_config.colorA;
		cbData.progress = m_config.progress;
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
		return "Fade";
	}

private:
	struct FadeCB
	{
		float fadeColor[4];
		float progress;
		float padding[3];
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	FadeConfig m_config;
};

// ============================================================================
// FrostGlassPass。フロストグラスブラー
// ============================================================================

/// @brief フロストグラスの設定
struct FrostGlassConfig
{
	float blurAmount = 2.0f;     ///< ブラー量
	float tintR = 0.9f;          ///< ティントR
	float tintG = 0.95f;         ///< ティントG
	float tintB = 1.0f;          ///< ティントB
	float time = 0.0f;           ///< アニメーション時間（ノイズ変化用）
};

/// @brief フロストグラスパス
/// @details ランダムオフセットブラー＋カラーティントでUI背景向けの曇りガラス効果を実現する。
class FrostGlassPass final : public PostProcessPass
{
public:
	FrostGlassPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_FROST_GLASS_PS);
		m_cb = createConstantBuffer(device, sizeof(FrostCB));
	}

	void setConfig(const FrostGlassConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		FrostCB cbData = {};
		cbData.blurAmount = m_config.blurAmount;
		cbData.tint[0] = m_config.tintR;
		cbData.tint[1] = m_config.tintG;
		cbData.tint[2] = m_config.tintB;
		cbData.time = m_config.time;
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
		return "FrostGlass";
	}

private:
	struct FrostCB
	{
		float blurAmount;
		float tint[3];
		float time;
		float pad[3];
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	FrostGlassConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
