#pragma once

/// @file GaussianBlurPass.hpp
/// @brief 分離型ガウシアンブラーパス

#ifdef _WIN32

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <d3d11.h>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/RenderTargetPool.hpp>
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
/// @details 水平→垂直の 2 パスでブラーを適用する。
///          ブルーム・フロストグラス・被写界深度等で再利用可能。
class GaussianBlurPass final : public PostProcessPass
{
public:
	/// @brief コンストラクタ
	/// @param device D3D11 デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアサンプラー（共有）
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	/// @param pool 中間 RT の貸し出し元（nullptr なら従来どおり自前で RT を生成する）
	GaussianBlurPass(
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
		, m_width(screenW)
		, m_height(screenH)
	{
		m_blurPS = compilePostProcessPS(device, PP_GAUSSIAN_BLUR_PS);
		m_cb = createConstantBuffer(device, sizeof(BlurCB));
		acquireIntermediate(screenW, screenH);
	}

	/// @brief デストラクタ。プール由来の中間 RT を返却する
	~GaussianBlurPass() override
	{
		releaseIntermediate();
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
			acquireIntermediate(screenW, screenH);
			m_width = screenW;
			m_height = screenH;
		}

		auto* midRtv = intermediateRtv();
		if (!midRtv)
		{
			// pool から借りたはずの中間 RT が resolve できない
			// (プールが handle を握ったまま entry を失った等)。
			// 知らせずに null RTV へ描くとブラーだけ消えて気づきにくいので明示する。
			debug::verboseOnce("render.postprocess.gaussianblur.rt_missing",
				"GaussianBlurPass::applyBlur で途中の描画先を引けなかったので、ぼかしを掛けません。");
			return;
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
			inputSRV, midRtv,
			m_sampler.Get(), m_cb.Get(),
			screenW, screenH);

		/// 垂直パス: intermediate → output
		cbData.texelDir[0] = 0.0f;
		cbData.texelDir[1] = 1.0f / static_cast<float>(screenH);
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_blurPS.Get(),
			intermediateSRV(), outputRTV,
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

	/// @brief 中間バッファの SRV を取得する（外部パスからの参照用）
	[[nodiscard]] ID3D11ShaderResourceView* intermediateSRV() const noexcept
	{
		if (m_pool && m_pooledHandle != gfx::RtHandle::Invalid)
		{
			auto* rt = m_pool->resolve(m_pooledHandle);
			return rt ? static_cast<ID3D11ShaderResourceView*>(rt->nativeSrv()) : nullptr;
		}
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

	/// @brief 中間バッファの RTV を取得する（pool 経由/自前どちらでも同じ形で返す）
	[[nodiscard]] ID3D11RenderTargetView* intermediateRtv() const noexcept
	{
		if (m_pool && m_pooledHandle != gfx::RtHandle::Invalid)
		{
			auto* rt = m_pool->resolve(m_pooledHandle);
			return rt ? static_cast<ID3D11RenderTargetView*>(rt->nativeRtv()) : nullptr;
		}
		return m_intermediate.rtv.Get();
	}

	/// @brief 中間バッファを確保する
	/// @details pool が使えるときは RGBA16F で借り、失敗時（未対応バックエンド等）は
	///          従来どおり自前で PostProcessRT を生成する。
	void acquireIntermediate(std::uint32_t w, std::uint32_t h)
	{
		releaseIntermediate();
		if (m_pool)
		{
			m_pooledHandle = m_pool->acquire(
				static_cast<int>(w), static_cast<int>(h), gfx::PixelFormat::RGBA16F);
			if (m_pooledHandle != gfx::RtHandle::Invalid)
			{
				return;
			}
			// pool 枯渇（RenderTargetPool 側で既に warnOnce 済み）。
			// 自前 RT へフォールバックすること自体も呼び出し側で残す。
			debug::verboseOnce("render.postprocess.gaussianblur.pool_exhausted",
				"GaussianBlurPass は RenderTargetPool から途中の描画先を借りられなかったので、自分で作ります。");
		}
		m_intermediate = createRenderTarget(m_device.Get(), w, h);
	}

	/// @brief pool 由来の中間バッファを返却する（自前生成時は何もしない）
	void releaseIntermediate() noexcept
	{
		if (m_pool && m_pooledHandle != gfx::RtHandle::Invalid)
		{
			m_pool->release(m_pooledHandle);
			m_pooledHandle = gfx::RtHandle::Invalid;
		}
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_blurPS;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	PostProcessRT m_intermediate;                              ///< pool 未使用時のみ実体を持つ
	gfx::RenderTargetPool* m_pool = nullptr;                   ///< 中間RTの貸し出し元（nullptr可）
	gfx::RtHandle m_pooledHandle = gfx::RtHandle::Invalid;      ///< pool 使用時の貸し出しハンドル
	GaussianBlurConfig m_config;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
};

} // namespace mitiru::render

#endif // _WIN32
