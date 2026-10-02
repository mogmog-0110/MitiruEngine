#pragma once

/// @file BloomPass.hpp
/// @brief ブルームパス（輝度抽出 → ガウスブラー → 加算合成）

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string_view>

#include <d3d11.h>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/RenderTargetPool.hpp>
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
	/// @param pool 中間 RT の貸し出し元（nullptr なら従来どおり自前で RT を生成する）
	BloomPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler,
		std::uint32_t screenW,
		std::uint32_t screenH,
		gfx::RenderTargetPool* pool = nullptr)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
		, m_device(device)
		, m_pool(pool)
	{
		m_extractPS = compilePostProcessPS(
			device, PP_BLOOM_EXTRACT_PS);
		m_combinePS = compilePostProcessPS(
			device, PP_BLOOM_COMBINE_PS);
		m_extractCB = createConstantBuffer(
			device, sizeof(BloomExtractCB));

		acquireIntermediates(screenW, screenH);

		m_blurPass = std::make_unique<GaussianBlurPass>(
			device, fullscreenVS, sampler, screenW, screenH, pool);
	}

	/// @brief デストラクタ。プール由来の中間 RT を返却する
	~BloomPass() override
	{
		releaseIntermediates();
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
		auto* brightTarget = brightRtv();
		if (!brightTarget)
		{
			// pool から借りたはずの輝度抽出 RT が resolve できない。
			// 知らせずに null RTV へ描くとブルームだけ消えて気づきにくいので明示する。
			debug::warnOnce("render.postprocess.bloom.rt_missing",
				"BloomPass::apply: 輝度抽出RTが取得できずブルームをスキップしました");
			return;
		}

		/// ステップ 1: 輝度抽出（scene → brightRT）
		BloomExtractCB extractData = {};
		extractData.threshold = m_config.threshold;
		extractData.intensity = m_config.intensity;
		updateConstantBuffer(context, m_extractCB.Get(),
			&extractData, sizeof(extractData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_extractPS.Get(),
			inputSRV, brightTarget,
			m_sampler.Get(), m_extractCB.Get(),
			screenW, screenH);

		auto* blurredTarget = blurredRtv();
		if (!blurredTarget)
		{
			debug::warnOnce("render.postprocess.bloom.rt_missing",
				"BloomPass::apply: ブラー結果RTが取得できずブルームをスキップしました");
			return;
		}

		/// ステップ 2: ガウスブラー（brightRT → blurredRT）
		m_blurPass->applyBlur(context,
			brightSrv(), blurredTarget,
			screenW, screenH);

		/// ステップ 3: 加算合成（scene + blurred → output）
		/// スロット 0 にシーン、スロット 1 にブルーム
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
			inputSRV, blurredSrv()
		};
		context->PSSetShaderResources(0, 2, srvs);
		auto* samplerPtr = m_sampler.Get();
		context->PSSetSamplers(0, 1, &samplerPtr);

		context->Draw(3, 0);

		/// SRV バインド解除
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

	/// @brief pool 経由/自前どちらでも同じ形で RTV/SRV を返す
	[[nodiscard]] ID3D11RenderTargetView* brightRtv() const noexcept
	{
		return rtvOf(m_brightHandle, m_brightRT);
	}
	[[nodiscard]] ID3D11ShaderResourceView* brightSrv() const noexcept
	{
		return srvOf(m_brightHandle, m_brightRT);
	}
	[[nodiscard]] ID3D11RenderTargetView* blurredRtv() const noexcept
	{
		return rtvOf(m_blurredHandle, m_blurredRT);
	}
	[[nodiscard]] ID3D11ShaderResourceView* blurredSrv() const noexcept
	{
		return srvOf(m_blurredHandle, m_blurredRT);
	}

	[[nodiscard]] ID3D11RenderTargetView* rtvOf(
		gfx::RtHandle handle, const PostProcessRT& fallback) const noexcept
	{
		if (m_pool && handle != gfx::RtHandle::Invalid)
		{
			auto* rt = m_pool->resolve(handle);
			return rt ? static_cast<ID3D11RenderTargetView*>(rt->nativeRtv()) : nullptr;
		}
		return fallback.rtv.Get();
	}
	[[nodiscard]] ID3D11ShaderResourceView* srvOf(
		gfx::RtHandle handle, const PostProcessRT& fallback) const noexcept
	{
		if (m_pool && handle != gfx::RtHandle::Invalid)
		{
			auto* rt = m_pool->resolve(handle);
			return rt ? static_cast<ID3D11ShaderResourceView*>(rt->nativeSrv()) : nullptr;
		}
		return fallback.srv.Get();
	}

	/// @brief 輝度抽出 RT・ブラー結果 RT を確保する
	/// @details pool が使えるときは RGBA16F で借り、失敗時（未対応バックエンド等）は
	///          従来どおり自前で PostProcessRT を生成する。
	void acquireIntermediates(std::uint32_t w, std::uint32_t h)
	{
		if (m_pool)
		{
			m_brightHandle = m_pool->acquire(
				static_cast<int>(w), static_cast<int>(h), gfx::PixelFormat::RGBA16F);
			m_blurredHandle = m_pool->acquire(
				static_cast<int>(w), static_cast<int>(h), gfx::PixelFormat::RGBA16F);
			if (m_brightHandle != gfx::RtHandle::Invalid && m_blurredHandle != gfx::RtHandle::Invalid)
			{
				return;
			}
			releaseIntermediates();
			// pool 枯渇（RenderTargetPool 側で既に warnOnce 済み）。
			// 自前 RT へフォールバックすること自体も呼び出し側で残す。
			debug::warnOnce("render.postprocess.bloom.pool_exhausted",
				"BloomPass: RenderTargetPool から中間RTを確保できず"
				"自前確保にフォールバックしました");
		}
		m_brightRT = createRenderTarget(m_device.Get(), w, h);
		m_blurredRT = createRenderTarget(m_device.Get(), w, h);
	}

	/// @brief pool 由来の中間 RT を返却する（自前生成時は何もしない）
	void releaseIntermediates() noexcept
	{
		if (!m_pool) { return; }
		if (m_brightHandle != gfx::RtHandle::Invalid)
		{
			m_pool->release(m_brightHandle);
			m_brightHandle = gfx::RtHandle::Invalid;
		}
		if (m_blurredHandle != gfx::RtHandle::Invalid)
		{
			m_pool->release(m_blurredHandle);
			m_blurredHandle = gfx::RtHandle::Invalid;
		}
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_extractPS;
	ComPtr<ID3D11PixelShader> m_combinePS;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_extractCB;
	PostProcessRT m_brightRT;                                  ///< pool 未使用時のみ実体を持つ
	PostProcessRT m_blurredRT;                                 ///< pool 未使用時のみ実体を持つ
	gfx::RenderTargetPool* m_pool = nullptr;                   ///< 中間RTの貸し出し元（nullptr可）
	gfx::RtHandle m_brightHandle = gfx::RtHandle::Invalid;      ///< pool 使用時の輝度抽出RTハンドル
	gfx::RtHandle m_blurredHandle = gfx::RtHandle::Invalid;     ///< pool 使用時のブラー結果RTハンドル
	std::unique_ptr<GaussianBlurPass> m_blurPass;
	BloomConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
