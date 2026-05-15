#pragma once

/// @file PostProcessIntegration.hpp
/// @brief ポストプロセスパイプラインとエンジン描画ループの統合レイヤー
/// @details PostProcessChainをEngine/RenderPipeline2Dに接続し、
///          オフスクリーンレンダーターゲットへの描画→ポストプロセス→バックバッファ出力を行う。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <memory>
#include <stdexcept>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/render/PostProcess.hpp>
#include <mitiru/render/FXAAShader.hpp>
#include <mitiru/render/GpuSpriteBatch.hpp>

namespace mitiru::render
{

/// @brief ポストプロセスマネージャー
/// @details エンジンの描画ループにポストプロセスパイプラインを統合する。
///          beginScene()でオフスクリーンRTにリダイレクトし、
///          endScene()でポストプロセスチェーンを実行してバックバッファに出力する。
///
/// @code
/// PostProcessManager ppManager;
/// ppManager.init(dx11Device, 1280, 720);
/// ppManager.enableBloom(0.8f, 1.0f);
///
/// // フレームごと:
/// ppManager.beginScene(context);
/// // ... 通常の描画 ...
/// ppManager.endScene(context, backbufferRTV);
/// @endcode
class PostProcessManager
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	PostProcessManager() noexcept = default;

	/// @brief ポストプロセスマネージャーを初期化する
	/// @param device D3D11デバイス
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	void init(ID3D11Device* device, int screenW, int screenH)
	{
		if (!device || screenW <= 0 || screenH <= 0)
		{
			throw std::runtime_error(
				"PostProcessManager: invalid parameters");
		}

		m_device = device;
		m_screenW = static_cast<std::uint32_t>(screenW);
		m_screenH = static_cast<std::uint32_t>(screenH);

		/// オフスクリーンシーンレンダーターゲットを生成する
		createSceneRT();

		/// 深度ステンシルビューを生成する（3D描画対応）
		createDepthStencil();

		/// 共有リソースを生成する
		m_fullscreenVS = compileFullscreenVS(device);
		m_sampler = createLinearClampSampler(device);

		/// 空のチェーンを構築する
		m_chain = std::make_unique<PostProcessChain>(
			device, m_screenW, m_screenH);

		m_initialized = true;
	}

	/// @brief ウィンドウリサイズ時にバッファを再生成する
	/// @param w 新しいスクリーン幅
	/// @param h 新しいスクリーン高さ
	void resize(int w, int h)
	{
		if (!m_initialized || w <= 0 || h <= 0)
		{
			return;
		}

		m_screenW = static_cast<std::uint32_t>(w);
		m_screenH = static_cast<std::uint32_t>(h);

		/// シーンRTを再生成する
		createSceneRT();

		/// 深度ステンシルを再生成する
		createDepthStencil();

		/// チェーンのピンポンバッファを再生成する
		if (m_chain)
		{
			m_chain->resize(m_screenW, m_screenH);
		}
	}

	/// @brief PostProcessConfigを一括適用する
	/// @param config 統合設定
	void setConfig(const PostProcessConfig& config)
	{
		if (!m_initialized)
		{
			return;
		}

		m_config = config;

		/// チェーンを再構築する（FXAAを含む）
		rebuildChain();
	}

	/// @brief シーン描画をオフスクリーンRTにリダイレクトする
	/// @param context D3D11デバイスコンテキスト
	/// @details 無効時は何もしない（描画はバックバッファに直接向かう）
	void beginScene(ID3D11DeviceContext* context)
	{
		if (!m_initialized || !m_enabled || !context)
		{
			return;
		}

		/// オフスクリーンRTをクリアしてバインドする
		constexpr float clearColor[4] = {
			0.0f, 0.0f, 0.0f, 0.0f
		};
		context->ClearRenderTargetView(
			m_sceneRT.rtv.Get(), clearColor);

		if (m_depthStencilView)
		{
			context->ClearDepthStencilView(
				m_depthStencilView.Get(),
				D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
				1.0f, 0);
		}

		auto* rtv = m_sceneRT.rtv.Get();
		context->OMSetRenderTargets(
			1, &rtv, m_depthStencilView.Get());

		/// ビューポートを設定する
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_screenW);
		vp.Height = static_cast<float>(m_screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		m_sceneActive = true;
	}

	/// @brief ポストプロセスチェーンを実行し、バックバッファに出力する
	/// @param context D3D11デバイスコンテキスト
	/// @param backbufferRTV バックバッファのレンダーターゲットビュー
	void endScene(
		ID3D11DeviceContext* context,
		ID3D11RenderTargetView* backbufferRTV)
	{
		if (!m_initialized || !m_enabled || !m_sceneActive ||
			!context || !backbufferRTV)
		{
			m_sceneActive = false;
			return;
		}

		m_sceneActive = false;

		/// SRVバインドを解除する（シーンRTを読み取る前に必要）
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);

		/// レンダーターゲットバインドを解除する
		ID3D11RenderTargetView* nullRTV = nullptr;
		context->OMSetRenderTargets(1, &nullRTV, nullptr);

		/// チェーンに有効パスがあるかチェックする
		const bool hasActivePasses =
			m_chain && m_chain->enabledPassCount() > 0;

		if (hasActivePasses)
		{
			/// ポストプロセスチェーンを実行する
			m_chain->execute(
				context,
				m_sceneRT.srv.Get(),
				backbufferRTV);
		}
		else
		{
			/// パスなし：シーンRTをバックバッファに直接コピーする
			copySceneToBackbuffer(context, backbufferRTV);
		}
	}

	/// @brief ポストプロセスが有効かどうかを取得する
	[[nodiscard]] bool isEnabled() const noexcept
	{
		return m_enabled;
	}

	/// @brief ポストプロセスの有効/無効を切り替える
	/// @param enabled 有効にする場合はtrue
	void setEnabled(bool enabled) noexcept
	{
		m_enabled = enabled;
	}

	// ================================================================
	// コンビニエンスメソッド
	// ================================================================

	/// @brief ブルームを有効化する
	/// @param threshold 輝度抽出閾値
	/// @param intensity ブルーム強度
	void enableBloom(float threshold = 0.8f, float intensity = 1.0f)
	{
		m_config.bloom.enabled = true;
		m_config.bloom.threshold = threshold;
		m_config.bloom.intensity = intensity;
		rebuildChain();
	}

	/// @brief ビネットを有効化する
	/// @param intensity 減光強度
	/// @param radius 減光開始半径
	void enableVignette(float intensity = 0.5f, float radius = 0.8f)
	{
		m_config.vignette.enabled = true;
		m_config.vignette.intensity = intensity;
		m_config.vignette.radius = radius;
		rebuildChain();
	}

	/// @brief カラーグレーディングを有効化する
	/// @param brightness 明度オフセット
	/// @param contrast コントラスト倍率
	/// @param saturation 彩度倍率
	void enableColorGrading(
		float brightness = 0.0f,
		float contrast = 1.0f,
		float saturation = 1.0f)
	{
		m_config.colorGrading.enabled = true;
		m_config.colorGrading.brightness = brightness;
		m_config.colorGrading.contrast = contrast;
		m_config.colorGrading.saturation = saturation;
		rebuildChain();
	}

	/// @brief フロストグラスを有効化する（UI背景ブラー用）
	/// @param blurAmount ブラー量
	void enableFrostGlass(float blurAmount = 2.0f)
	{
		m_config.frostGlass.enabled = true;
		m_config.frostGlass.blurAmount = blurAmount;
		rebuildChain();
	}

	/// @brief FXAAを有効化する（アンチエイリアシング）
	/// @param quality 品質プリセット（デフォルト: Medium）
	void enableFXAA(FXAAQuality quality = FXAAQuality::Medium)
	{
		const auto cfg = FXAAConfig::fromQuality(quality);
		m_config.fxaa.enabled = true;
		m_config.fxaa.subpixQuality = cfg.subpixQuality;
		m_config.fxaa.edgeThreshold = cfg.edgeThreshold;
		m_config.fxaa.edgeThresholdMin = cfg.edgeThresholdMin;
		rebuildChain();
	}

	/// @brief FXAAを無効化する
	void disableFXAA()
	{
		m_config.fxaa.enabled = false;
		rebuildChain();
	}

	/// @brief 全エフェクトを無効化する
	void disableAll()
	{
		m_config = PostProcessConfig{};
		rebuildChain();
	}

	// ================================================================
	// シーンテクスチャキャプチャ
	// ================================================================

	/// @brief 現在のシーンテクスチャをスナップショットとして取得する
	/// @details サムネイル生成やフロストグラス背景に使用する。
	///          beginScene()〜endScene()の間、またはendScene()直後に呼ぶ。
	/// @return シーンテクスチャのコピー（GpuTexture2D）
	[[nodiscard]] GpuTexture2D captureSceneTexture()
	{
		if (!m_initialized || !m_device)
		{
			throw std::runtime_error(
				"PostProcessManager: not initialized");
		}

		/// シーンRTテクスチャをステージング経由でCPUに読み出す
		ComPtr<ID3D11DeviceContext> context;
		m_device->GetImmediateContext(context.GetAddressOf());
		if (!context)
		{
			throw std::runtime_error(
				"PostProcessManager: failed to get context");
		}

		/// ステージングテクスチャを生成する
		D3D11_TEXTURE2D_DESC srcDesc = {};
		m_sceneRT.texture->GetDesc(&srcDesc);

		/// ソースと同一フォーマットのステージングテクスチャを生成する
		/// （CopyResourceはフォーマット一致が必須）
		D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
		stagingDesc.MipLevels = 1;
		stagingDesc.ArraySize = 1;
		stagingDesc.SampleDesc.Count = 1;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;

		ComPtr<ID3D11Texture2D> staging;
		HRESULT hr = m_device->CreateTexture2D(
			&stagingDesc, nullptr, staging.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"PostProcessManager: staging texture creation failed");
		}

		/// シーンRTをステージングにコピーする（同一フォーマット）
		context->CopyResource(staging.Get(), m_sceneRT.texture.Get());

		/// マップしてCPU読み出しする
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"PostProcessManager: Map failed");
		}

		const auto w = static_cast<int>(srcDesc.Width);
		const auto h = static_cast<int>(srcDesc.Height);
		std::vector<std::uint8_t> pixels(
			static_cast<std::size_t>(w) * h * 4);

		const auto* src =
			static_cast<const std::uint8_t*>(mapped.pData);

		const bool isFloat16 =
			(srcDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);

		for (int y = 0; y < h; ++y)
		{
			const auto* rowSrc = src +
				static_cast<std::size_t>(y) * mapped.RowPitch;
			auto* rowDst = pixels.data() +
				static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4;

			if (isFloat16)
			{
				/// R16G16B16A16_FLOAT → RGBA8変換
				const auto* fp16 =
					reinterpret_cast<const uint16_t*>(rowSrc);
				for (int x = 0; x < w; ++x)
				{
					for (int c = 0; c < 4; ++c)
					{
						/// half→float簡易変換後、[0,1]にクランプしてu8に
						const uint16_t hf = fp16[x * 4 + c];
						const uint32_t sign = (hf >> 15) & 1;
						const uint32_t exp = (hf >> 10) & 0x1F;
						const uint32_t mant = hf & 0x3FF;
						float val = 0.0f;
						if (exp == 0)
						{
							val = (5.96046448e-08f) *
								static_cast<float>(mant);
						}
						else if (exp < 31)
						{
							val = std::ldexp(
								1.0f + static_cast<float>(mant) / 1024.0f,
								static_cast<int>(exp) - 15);
						}
						else
						{
							val = (mant == 0) ? 1.0f : 0.0f;
						}
						if (sign) val = -val;
						val = std::clamp(val, 0.0f, 1.0f);
						rowDst[x * 4 + c] =
							static_cast<uint8_t>(val * 255.0f + 0.5f);
					}
				}
			}
			else
			{
				/// R8G8B8A8_UNORMなどはそのままコピー
				std::memcpy(rowDst, rowSrc,
					static_cast<std::size_t>(w) * 4);
			}
		}

		context->Unmap(staging.Get(), 0);

		/// GpuTexture2Dとして返す
		return GpuTexture2D::createFromPixels(
			m_device.Get(), w, h,
			std::span<const std::uint8_t>(pixels));
	}

	// ================================================================
	// アクセサ
	// ================================================================

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief シーンRTのSRVを取得する（外部パスからの参照用）
	[[nodiscard]] ID3D11ShaderResourceView* sceneTextureSRV() const noexcept
	{
		return m_sceneRT.srv.Get();
	}

	/// @brief 深度ステンシルビューを取得する
	[[nodiscard]] ID3D11DepthStencilView* depthStencilView() const noexcept
	{
		return m_depthStencilView.Get();
	}

	/// @brief 内部のPostProcessChainを取得する
	[[nodiscard]] PostProcessChain* chain() const noexcept
	{
		return m_chain.get();
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const PostProcessConfig& config() const noexcept
	{
		return m_config;
	}

private:
	/// @brief オフスクリーンシーンレンダーターゲットを生成する
	void createSceneRT()
	{
		m_sceneRT = createRenderTarget(
			m_device.Get(), m_screenW, m_screenH);
	}

	/// @brief 深度ステンシルバッファを生成する
	void createDepthStencil()
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_screenW;
		desc.Height = m_screenH;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		ComPtr<ID3D11Texture2D> depthTexture;
		HRESULT hr = m_device->CreateTexture2D(
			&desc, nullptr, depthTexture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"PostProcessManager: depth stencil texture creation failed");
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

		hr = m_device->CreateDepthStencilView(
			depthTexture.Get(), &dsvDesc,
			m_depthStencilView.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"PostProcessManager: depth stencil view creation failed");
		}

		m_depthTexture = depthTexture;
	}

	/// @brief シーンRTの内容をバックバッファに直接コピーする
	/// @details 有効パスがない場合のフォールバック処理。
	///          フルスクリーン三角形でシーンテクスチャをそのまま描画する。
	void copySceneToBackbuffer(
		ID3D11DeviceContext* context,
		ID3D11RenderTargetView* backbufferRTV)
	{
		/// パススルーシェーダーでシーンRTをバックバッファに描画する
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_screenW);
		vp.Height = static_cast<float>(m_screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		context->OMSetRenderTargets(1, &backbufferRTV, nullptr);
		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);

		/// パススルーPS: sceneTexture.Sample() をそのまま返す
		/// コンパイル済みPSがなければ遅延生成する
		if (!m_passthroughPS)
		{
			m_passthroughPS = compilePostProcessPS(
				m_device.Get(), PP_PASSTHROUGH_PS);
		}

		context->PSSetShader(m_passthroughPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto* srv = m_sceneRT.srv.Get();
		context->PSSetShaderResources(0, 1, &srv);
		auto* samplerPtr = m_sampler.Get();
		context->PSSetSamplers(0, 1, &samplerPtr);

		context->Draw(3, 0);

		/// SRVバインドを解除する
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
	}

	/// @brief 現在のconfigからチェーンを再構築する
	void rebuildChain()
	{
		if (!m_initialized)
		{
			return;
		}

		m_chain = std::make_unique<PostProcessChain>(
			PostProcessChain::createFromConfig(
				m_device.Get(), m_screenW, m_screenH, m_config));

		/// FXAAはチェーンの最終段に追加する（ブルーム・カラーグレーディング後）
		/// m_fullscreenVS/m_samplerを再利用し、再コンパイルを回避する
		if (m_config.fxaa.enabled)
		{
			auto fxaaPass = std::make_unique<FXAAPass>(
				m_device.Get(), m_fullscreenVS, m_sampler);
			FXAAConfig fxaaCfg;
			fxaaCfg.subpixQuality = m_config.fxaa.subpixQuality;
			fxaaCfg.edgeThreshold = m_config.fxaa.edgeThreshold;
			fxaaCfg.edgeThresholdMin = m_config.fxaa.edgeThresholdMin;
			fxaaPass->setConfig(fxaaCfg);
			m_chain->addPass(std::move(fxaaPass));
		}
	}

	// ── パススルーシェーダー ──────────────────────────
	/// @brief パススルーピクセルシェーダー（エフェクトなしコピー用）
	static constexpr std::string_view PP_PASSTHROUGH_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	return sceneTexture.Sample(linearSampler, input.texCoord);
}
)hlsl";

	// ── メンバ変数 ──────────────────────────────────
	ComPtr<ID3D11Device> m_device;                           ///< D3D11デバイス
	std::uint32_t m_screenW = 0;                            ///< スクリーン幅
	std::uint32_t m_screenH = 0;                            ///< スクリーン高さ

	PostProcessRT m_sceneRT;                                 ///< オフスクリーンシーンRT
	ComPtr<ID3D11Texture2D> m_depthTexture;                  ///< 深度テクスチャ
	ComPtr<ID3D11DepthStencilView> m_depthStencilView;       ///< 深度ステンシルビュー

	ComPtr<ID3D11VertexShader> m_fullscreenVS;               ///< フルスクリーンVS（共有）
	ComPtr<ID3D11SamplerState> m_sampler;                    ///< リニアサンプラー（共有）
	mutable ComPtr<ID3D11PixelShader> m_passthroughPS;       ///< パススルーPS（遅延生成）

	std::unique_ptr<PostProcessChain> m_chain;               ///< ポストプロセスチェーン
	PostProcessConfig m_config;                              ///< 現在の設定

	bool m_initialized = false;                              ///< 初期化済みフラグ
	bool m_enabled = true;                                   ///< 有効フラグ
	bool m_sceneActive = false;                              ///< beginScene()〜endScene()間フラグ
};

} // namespace mitiru::render

#endif // _WIN32
