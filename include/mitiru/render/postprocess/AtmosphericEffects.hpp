#pragma once

/// @file AtmosphericEffects.hpp
/// @brief 大気エフェクト（ビネット・色収差・フィルムグレイン）

#ifdef _WIN32

#include <cstdint>
#include <string_view>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>

namespace mitiru::render
{

// ============================================================================
// VignettePass。周辺減光
// ============================================================================

/// @brief ビネットの設定
struct VignetteConfig
{
	float intensity = 0.5f;    ///< 減光強度
	float radius = 0.8f;       ///< 減光開始半径
	float softness = 0.5f;     ///< ソフトネス
};

/// @brief ビネットパス
/// @details スクリーン中心からの距離に応じて周辺を暗くする。
class VignettePass final : public PostProcessPass
{
public:
	VignettePass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_VIGNETTE_PS);
		m_cb = createConstantBuffer(device, sizeof(VignetteCB));
	}

	void setConfig(const VignetteConfig& cfg) noexcept
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
		VignetteCB cbData = {};
		cbData.intensity = m_config.intensity;
		cbData.radius = m_config.radius;
		cbData.softness = m_config.softness;
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
		return "Vignette";
	}

private:
	struct VignetteCB
	{
		float intensity;
		float radius;
		float softness;
		float padding;
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	VignetteConfig m_config;
};

// ============================================================================
// ChromaticAberrationPass。色収差
// ============================================================================

/// @brief 色収差の設定
struct ChromaticAberrationConfig
{
	float intensity = 1.0f;    ///< 色収差強度
};

/// @brief 色収差パス
/// @details R/Bチャンネルを中心から放射状にオフセットする。
class ChromaticAberrationPass final : public PostProcessPass
{
public:
	ChromaticAberrationPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(
			device, PP_CHROMATIC_ABERRATION_PS);
		m_cb = createConstantBuffer(device, sizeof(ChromaticCB));
	}

	void setConfig(
		const ChromaticAberrationConfig& cfg) noexcept
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
		ChromaticCB cbData = {};
		cbData.intensity = m_config.intensity;
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
		return "ChromaticAberration";
	}

private:
	struct ChromaticCB
	{
		float intensity;
		float padding[3];
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	ChromaticAberrationConfig m_config;
};

// ============================================================================
// FilmGrainPass。フィルムグレイン
// ============================================================================

/// @brief フィルムグレインの設定
struct FilmGrainConfig
{
	float intensity = 0.05f;   ///< ノイズ強度
	float speed = 1.0f;        ///< ノイズ変化速度
};

/// @brief フィルムグレインパス
/// @details プロシージャルノイズによるフィルム風粒状感を付加する。
class FilmGrainPass final : public PostProcessPass
{
public:
	FilmGrainPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_FILM_GRAIN_PS);
		m_cb = createConstantBuffer(device, sizeof(FilmGrainCB));
	}

	void setConfig(const FilmGrainConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief 経過時間を更新する
	/// @param deltaTime フレームデルタ時間（秒）
	void updateTime(float deltaTime) noexcept
	{
		m_time += deltaTime * m_config.speed;
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		FilmGrainCB cbData = {};
		cbData.intensity = m_config.intensity;
		cbData.time = m_time;
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
		return "FilmGrain";
	}

private:
	struct FilmGrainCB
	{
		float intensity;
		float time;
		float padding[2];
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	FilmGrainConfig m_config;
	float m_time = 0.0f;
};

} // namespace mitiru::render

#endif // _WIN32
