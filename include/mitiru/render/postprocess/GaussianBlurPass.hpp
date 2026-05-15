#pragma once

/// @file GaussianBlurPass.hpp
/// @brief 分離型ガウシアンブラーパス

#ifdef _WIN32

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>

namespace mitiru::render
{

/// @brief ガウシアンブラーの設定
struct GaussianBlurConfig
{
	int radius = 8;         ///< ブラー半径（1-32）
	float sigma = 4.0f;     ///< ガウス分布のシグマ
};

/// @brief 分離型ガウシアンブラーパス
/// @details 水平→垂直の2パスでブラーを適用する。
///          ブルーム・フロストグラス・被写界深度等で再利用可能。
class GaussianBlurPass final : public PostProcessPass
{
public:
	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアサンプラー（共有）
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	GaussianBlurPass(
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
		m_blurPS = compilePostProcessPS(device, PP_GAUSSIAN_BLUR_PS);
		m_cb = createConstantBuffer(device, sizeof(BlurCB));
		m_intermediate = createRenderTarget(device, screenW, screenH);
	}

	/// @brief ブラー設定を変更する
	void setConfig(const GaussianBlurConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief 内部の中間バッファに直接ブラーを適用する
	/// @details ブルームパスから再利用される
	void applyBlur(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		// リサイズ検出: 中間バッファを再生成する
		if (screenW != m_width || screenH != m_height)
		{
			m_intermediate = createRenderTarget(m_device.Get(), screenW, screenH);
			m_width = screenW;
			m_height = screenH;
		}

		/// 水平パス: input → intermediate
		BlurCB cbData = {};
		cbData.texelDir[0] = 1.0f / static_cast<float>(screenW);
		cbData.texelDir[1] = 0.0f;
		cbData.radius = std::clamp(m_config.radius, 1, 32);
		cbData.sigma = m_config.sigma;
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_blurPS.Get(),
			inputSRV, m_intermediate.rtv.Get(),
			m_sampler.Get(), m_cb.Get(),
			screenW, screenH);

		/// 垂直パス: intermediate → output
		cbData.texelDir[0] = 0.0f;
		cbData.texelDir[1] = 1.0f / static_cast<float>(screenH);
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_blurPS.Get(),
			m_intermediate.srv.Get(), outputRTV,
			m_sampler.Get(), m_cb.Get(),
			screenW, screenH);
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		applyBlur(context, inputSRV, outputRTV, screenW, screenH);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "GaussianBlur";
	}

	/// @brief 中間バッファのSRVを取得する（外部パスからの参照用）
	[[nodiscard]] ID3D11ShaderResourceView* intermediateSRV() const noexcept
	{
		return m_intermediate.srv.Get();
	}

private:
	/// @brief ブラー定数バッファレイアウト
	struct BlurCB
	{
		float texelDir[2];    ///< テクセル方向
		int radius;           ///< ブラー半径
		float sigma;          ///< シグマ
	};

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_blurPS;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	PostProcessRT m_intermediate;
	GaussianBlurConfig m_config;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
};

} // namespace mitiru::render

#endif // _WIN32
