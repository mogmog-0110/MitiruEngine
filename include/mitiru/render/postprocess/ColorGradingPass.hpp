#pragma once

/// @file ColorGradingPass.hpp
/// @brief カラーグレーディングパス

#ifdef _WIN32

#include <cstdint>
#include <string_view>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>

namespace mitiru::render
{

/// @brief カラーグレーディングの設定
struct ColorGradingConfig
{
	float brightness = 1.0f;          ///< 明度倍率
	float contrast = 1.0f;            ///< コントラスト倍率
	float saturation = 1.0f;          ///< 彩度倍率
	float gamma = 1.0f;               ///< ガンマ値
	float tintR = 1.0f;               ///< ティントR
	float tintG = 1.0f;               ///< ティントG
	float tintB = 1.0f;               ///< ティントB
};

/// @brief カラーグレーディングパス
/// @details 明度・コントラスト・彩度・ガンマ・ティントをピクセル単位で適用する。
class ColorGradingPass final : public PostProcessPass
{
public:
	/// @brief コンストラクタ
	ColorGradingPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_COLOR_GRADING_PS);
		m_cb = createConstantBuffer(device, sizeof(ColorGradingCB));
	}

	/// @brief 設定を変更する
	void setConfig(const ColorGradingConfig& cfg) noexcept
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
		ColorGradingCB cbData = {};
		cbData.brightness = m_config.brightness;
		cbData.contrast = m_config.contrast;
		cbData.saturation = m_config.saturation;
		cbData.gamma = m_config.gamma;
		cbData.tint[0] = m_config.tintR;
		cbData.tint[1] = m_config.tintG;
		cbData.tint[2] = m_config.tintB;
		cbData.tint[3] = 1.0f;
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
		return "ColorGrading";
	}

private:
	/// @brief 定数バッファレイアウト
	struct ColorGradingCB
	{
		float brightness;
		float contrast;
		float saturation;
		float gamma;
		float tint[4];
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	ColorGradingConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
