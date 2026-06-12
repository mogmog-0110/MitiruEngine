#pragma once

/// @file SSAOEffect.hpp
/// @brief スクリーンスペースアンビエントオクルージョン（SSAO）ポストプロセス
/// @details 深度バッファから半球サンプリングでAOを計算し、ブラーで平滑化する。
///          16サンプルの高速実装。PostProcessPassとして統合可能。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/PostProcess.hpp>
#include <mitiru/render/SSAOEffect_shaders_tables.hpp>

namespace mitiru::render
{

// ============================================================================
// SSAOConfig — SSAO設定
// ============================================================================

/// @brief SSAOパラメータ設定
struct SSAOConfig
{
	float radius = 0.5f;       ///< サンプリング半径（ビュー空間単位）
	float bias = 0.025f;       ///< 深度バイアス（アクネ防止）
	float intensity = 1.0f;    ///< AOの強さ (0=無効, 1=標準, 2=強い)
	float nearPlane = 0.1f;    ///< カメラニアクリップ面
	float farPlane = 100.0f;   ///< カメラファークリップ面
};

// ============================================================================
// SSAOPass — SSAOポストプロセスパス
// ============================================================================

/// @brief SSAOポストプロセスパス
/// @details 深度バッファから半球サンプリングでAO値を計算し、ブラーで平滑化する。
///          PostProcessPassを継承し、ポストプロセスチェーンに組み込み可能。
///          applyメソッドはシーンの色テクスチャにAOを合成した結果を出力する。
///
/// @note このパスは追加で深度SRVを必要とする。setDepthSRV()で設定すること。
///
/// @code
/// SSAOPass ssao(device, fullscreenVS, sampler, 1280, 720);
/// ssao.setDepthSRV(depthSRV);
///
/// SSAOConfig cfg;
/// cfg.radius = 0.8f;
/// cfg.intensity = 1.5f;
/// ssao.setConfig(cfg);
///
/// // 射影行列を毎フレーム設定する
/// ssao.setProjection(projMatrix);
///
/// ssao.apply(context, sceneSRV, outputRTV, 1280, 720);
/// @endcode
class SSAOPass final : public PostProcessPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアクランプサンプラー（共有）
	/// @param screenW 初期スクリーン幅
	/// @param screenH 初期スクリーン高さ
	SSAOPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler,
		std::uint32_t screenW,
		std::uint32_t screenH)
		: m_device(device)
		, m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
		, m_width(screenW)
		, m_height(screenH)
	{
		/// シェーダーをコンパイルする
		m_ssaoPS = compilePostProcessPS(device, PP_SSAO_PS);
		m_blurPS = compilePostProcessPS(device, PP_SSAO_BLUR_PS);
		m_compositePS = compilePostProcessPS(
			device, PP_SSAO_COMPOSITE_PS);

		/// 定数バッファを生成する
		m_ssaoCB = createConstantBuffer(device, sizeof(SSAOCB));
		m_blurCB = createConstantBuffer(device, sizeof(SSAOBlurCB));

		/// サンプラーを生成する
		m_pointClampSampler = createPointClampSampler(device);
		m_noiseWrapSampler = createNoiseWrapSampler(device);

		/// ランダムカーネルを生成する
		generateKernel();

		/// ノイズテクスチャを生成する
		createNoiseTexture(device);

		/// 中間レンダーターゲットを生成する
		m_ssaoRT = createRenderTarget(
			device, screenW, screenH,
			DXGI_FORMAT_R8_UNORM);
		m_blurredRT = createRenderTarget(
			device, screenW, screenH,
			DXGI_FORMAT_R8_UNORM);
	}

	/// @brief 深度SRVを設定する
	/// @param depthSRV 深度バッファのシェーダーリソースビュー
	void setDepthSRV(
		ID3D11ShaderResourceView* depthSRV) noexcept
	{
		m_depthSRV = depthSRV;
	}

	/// @brief 射影行列を設定する（カラムメジャー、float[4][4]）
	/// @param proj 射影行列の先頭ポインタ（float x 16）
	void setProjection(const float* proj) noexcept
	{
		std::memcpy(m_projection, proj, sizeof(float) * 16);
		invertMatrix4x4(m_projection, m_invProjection);
	}

	/// @brief SSAO設定を変更する
	/// @param cfg SSAO設定
	void setConfig(const SSAOConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief ブラー済みAOテクスチャのSRVを取得する
	/// @return AO出力のSRV（apply後に有効）
	[[nodiscard]] ID3D11ShaderResourceView*
	aoSRV() const noexcept
	{
		return m_blurredRT.srv.Get();
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		if (screenW == 0 || screenH == 0)
		{
			return;
		}

		if (!m_depthSRV)
		{
			/// 深度SRVが未設定ならパススルーする
			drawFullscreenPass(context,
				m_fullscreenVS.Get(), m_compositePS.Get(),
				inputSRV, outputRTV,
				m_sampler.Get(), nullptr,
				screenW, screenH);
			return;
		}

		/// リサイズ検出: 中間バッファを再生成する
		if (screenW != m_width || screenH != m_height)
		{
			m_ssaoRT = createRenderTarget(
				m_device.Get(), screenW, screenH,
				DXGI_FORMAT_R8_UNORM);
			m_blurredRT = createRenderTarget(
				m_device.Get(), screenW, screenH,
				DXGI_FORMAT_R8_UNORM);
			m_width = screenW;
			m_height = screenH;
		}

		/// ステップ1: SSAO計算（深度 → AO生テクスチャ）
		applySSAOPass(context, screenW, screenH);

		/// ステップ2: ブラー（AO生 → AOブラー済み）
		applyBlurPass(context, screenW, screenH);

		/// ステップ3: 合成（シーン色 * AOブラー済み → 出力）
		applyCompositePass(context, inputSRV, outputRTV,
			screenW, screenH);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "SSAO";
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const SSAOConfig& config() const noexcept
	{
		return m_config;
	}

private:
	// ── 定数バッファレイアウト ─────────────────────────

	/// @brief SSAO定数バッファ（16バイトアライン）
	struct SSAOCB
	{
		float projection[16];          ///< 射影行列
		float invProjection[16];       ///< 逆射影行列
		float samples[16][4];          ///< カーネルサンプル (float4 x 16)
		float noiseScale[2];           ///< ノイズタイルスケール
		float radius;                  ///< サンプリング半径
		float bias;                    ///< 深度バイアス
		float intensity;               ///< AOの強さ
		float farPlane;                ///< ファークリップ
		float nearPlane;               ///< ニアクリップ
		float pad0;                    ///< パディング
	};

	/// @brief SSAOブラー定数バッファ
	struct SSAOBlurCB
	{
		float texelSize[2];            ///< 1.0 / screenSize
		float pad[2];                  ///< パディング
	};

	// ── カーネル生成 ─────────────────────────────────

	/// @brief 半球内のランダムサンプルカーネルを生成する
	void generateKernel()
	{
		std::mt19937 rng(42);
		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
		std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);

		for (int i = 0; i < 16; ++i)
		{
			/// 半球内のランダム方向を生成する
			float x = distNeg(rng);
			float y = distNeg(rng);
			float z = dist01(rng);  // 半球（z >= 0）

			/// 正規化する
			float len = std::sqrt(x * x + y * y + z * z);
			if (len < 0.001f)
			{
				len = 1.0f;
			}
			x /= len;
			y /= len;
			z /= len;

			/// 加速補間: 中心付近のサンプルを増やす
			float scale = static_cast<float>(i) / 16.0f;
			scale = lerp(0.1f, 1.0f, scale * scale);
			x *= scale;
			y *= scale;
			z *= scale;

			m_kernel[i][0] = x;
			m_kernel[i][1] = y;
			m_kernel[i][2] = z;
			m_kernel[i][3] = 0.0f;
		}
	}

	// ── ノイズテクスチャ生成 ────────────────────────────

	/// @brief 4x4ランダム回転ノイズテクスチャを生成する
	void createNoiseTexture(ID3D11Device* device)
	{
		std::mt19937 rng(12345);
		std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

		/// 4x4テクスチャ（RGBA16F）
		constexpr int kNoiseSize = 4;
		constexpr int kNoisePixels = kNoiseSize * kNoiseSize;

		/// float16用にfloat32で生成してR8G8B8A8_SNORMに変換する
		struct NoisePixel
		{
			std::int8_t r, g, b, a;
		};

		std::array<NoisePixel, kNoisePixels> noiseData{};
		for (int i = 0; i < kNoisePixels; ++i)
		{
			float x = dist(rng);
			float y = dist(rng);
			float len = std::sqrt(x * x + y * y);
			if (len < 0.001f)
			{
				len = 1.0f;
			}
			x /= len;
			y /= len;

			noiseData[i].r = static_cast<std::int8_t>(
				std::clamp(x * 127.0f, -127.0f, 127.0f));
			noiseData[i].g = static_cast<std::int8_t>(
				std::clamp(y * 127.0f, -127.0f, 127.0f));
			noiseData[i].b = 0;
			noiseData[i].a = 0;
		}

		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width = kNoiseSize;
		texDesc.Height = kNoiseSize;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_IMMUTABLE;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = noiseData.data();
		initData.SysMemPitch =
			kNoiseSize * static_cast<UINT>(sizeof(NoisePixel));

		HRESULT hr = device->CreateTexture2D(
			&texDesc, &initData,
			m_noiseTexture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"SSAOPass: CreateTexture2D (noise) failed");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
		srvDesc.ViewDimension =
			D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(
			m_noiseTexture.Get(), &srvDesc,
			m_noiseSRV.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"SSAOPass: CreateSRV (noise) failed");
		}
	}

	// ── サンプラー生成 ──────────────────────────────

	/// @brief ポイントクランプサンプラーを生成する
	[[nodiscard]] static ComPtr<ID3D11SamplerState>
	createPointClampSampler(ID3D11Device* device)
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		ComPtr<ID3D11SamplerState> sampler;
		HRESULT hr = device->CreateSamplerState(
			&desc, sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"SSAOPass: CreateSamplerState (point) failed");
		}
		return sampler;
	}

	/// @brief ノイズ用ラップサンプラーを生成する
	[[nodiscard]] static ComPtr<ID3D11SamplerState>
	createNoiseWrapSampler(ID3D11Device* device)
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		ComPtr<ID3D11SamplerState> sampler;
		HRESULT hr = device->CreateSamplerState(
			&desc, sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"SSAOPass: CreateSamplerState (wrap) failed");
		}
		return sampler;
	}

	// ── 行列ユーティリティ ─────────────────────────────

	/// @brief 線形補間
	[[nodiscard]] static float lerp(
		float a, float b, float t) noexcept
	{
		return a + (b - a) * t;
	}

	/// @brief 4x4行列の逆行列を計算する（余因子展開）
	static void invertMatrix4x4(
		const float src[16], float dst[16]) noexcept
	{
		/// 余因子を計算する
		float s0 = src[0] * src[5] - src[4] * src[1];
		float s1 = src[0] * src[6] - src[4] * src[2];
		float s2 = src[0] * src[7] - src[4] * src[3];
		float s3 = src[1] * src[6] - src[5] * src[2];
		float s4 = src[1] * src[7] - src[5] * src[3];
		float s5 = src[2] * src[7] - src[6] * src[3];

		float c5 = src[10] * src[15] - src[14] * src[11];
		float c4 = src[9] * src[15] - src[13] * src[11];
		float c3 = src[9] * src[14] - src[13] * src[10];
		float c2 = src[8] * src[15] - src[12] * src[11];
		float c1 = src[8] * src[14] - src[12] * src[10];
		float c0 = src[8] * src[13] - src[12] * src[9];

		float det = s0 * c5 - s1 * c4 + s2 * c3
			+ s3 * c2 - s4 * c1 + s5 * c0;

		if (std::abs(det) < 1e-10f)
		{
			/// 特異行列: 単位行列を返す
			for (int i = 0; i < 16; ++i)
			{
				dst[i] = (i % 5 == 0) ? 1.0f : 0.0f;
			}
			return;
		}

		float invDet = 1.0f / det;

		dst[0] = ( src[5] * c5 - src[6] * c4
			+ src[7] * c3) * invDet;
		dst[1] = (-src[1] * c5 + src[2] * c4
			- src[3] * c3) * invDet;
		dst[2] = ( src[13] * s5 - src[14] * s4
			+ src[15] * s3) * invDet;
		dst[3] = (-src[9] * s5 + src[10] * s4
			- src[11] * s3) * invDet;

		dst[4] = (-src[4] * c5 + src[6] * c2
			- src[7] * c1) * invDet;
		dst[5] = ( src[0] * c5 - src[2] * c2
			+ src[3] * c1) * invDet;
		dst[6] = (-src[12] * s5 + src[14] * s2
			- src[15] * s1) * invDet;
		dst[7] = ( src[8] * s5 - src[10] * s2
			+ src[11] * s1) * invDet;

		dst[8] = ( src[4] * c4 - src[5] * c2
			+ src[7] * c0) * invDet;
		dst[9] = (-src[0] * c4 + src[1] * c2
			- src[3] * c0) * invDet;
		dst[10] = ( src[12] * s4 - src[13] * s2
			+ src[15] * s0) * invDet;
		dst[11] = (-src[8] * s4 + src[9] * s2
			- src[11] * s0) * invDet;

		dst[12] = (-src[4] * c3 + src[5] * c1
			- src[6] * c0) * invDet;
		dst[13] = ( src[0] * c3 - src[1] * c1
			+ src[2] * c0) * invDet;
		dst[14] = (-src[12] * s3 + src[13] * s1
			- src[14] * s0) * invDet;
		dst[15] = ( src[8] * s3 - src[9] * s1
			+ src[10] * s0) * invDet;
	}

	// ── パス実行 ────────────────────────────────────

	/// @brief SSAO計算パスを実行する
	void applySSAOPass(
		ID3D11DeviceContext* context,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		/// 定数バッファを更新する
		SSAOCB cbData = {};
		std::memcpy(cbData.projection, m_projection,
			sizeof(float) * 16);
		std::memcpy(cbData.invProjection, m_invProjection,
			sizeof(float) * 16);
		std::memcpy(cbData.samples, m_kernel,
			sizeof(float) * 16 * 4);
		cbData.noiseScale[0] =
			static_cast<float>(screenW) / 4.0f;
		cbData.noiseScale[1] =
			static_cast<float>(screenH) / 4.0f;
		cbData.radius = m_config.radius;
		cbData.bias = m_config.bias;
		cbData.intensity = m_config.intensity;
		cbData.farPlane = m_config.farPlane;
		cbData.nearPlane = m_config.nearPlane;
		cbData.pad0 = 0.0f;
		updateConstantBuffer(context, m_ssaoCB.Get(),
			&cbData, sizeof(cbData));

		/// ビューポート設定
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		/// レンダーターゲット設定
		auto* rtv = m_ssaoRT.rtv.Get();
		context->OMSetRenderTargets(1, &rtv, nullptr);

		/// シェーダー設定
		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_ssaoPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ設定: t0=深度, t1=ノイズ
		ID3D11ShaderResourceView* srvs[2] = {
			m_depthSRV, m_noiseSRV.Get()
		};
		context->PSSetShaderResources(0, 2, srvs);

		/// サンプラー設定: s0=ポイントクランプ, s1=ノイズラップ
		ID3D11SamplerState* samplers[2] = {
			m_pointClampSampler.Get(),
			m_noiseWrapSampler.Get()
		};
		context->PSSetSamplers(0, 2, samplers);

		/// 定数バッファ設定
		auto* cb = m_ssaoCB.Get();
		context->PSSetConstantBuffers(0, 1, &cb);

		/// フルスクリーン三角形を描画する
		context->Draw(3, 0);

		/// SRVバインドをクリアする
		ID3D11ShaderResourceView* nullSRVs[2] = {
			nullptr, nullptr
		};
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

	/// @brief ブラーパスを実行する
	void applyBlurPass(
		ID3D11DeviceContext* context,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		/// 定数バッファを更新する
		SSAOBlurCB blurData = {};
		blurData.texelSize[0] =
			1.0f / static_cast<float>(screenW);
		blurData.texelSize[1] =
			1.0f / static_cast<float>(screenH);
		updateConstantBuffer(context, m_blurCB.Get(),
			&blurData, sizeof(blurData));

		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_blurPS.Get(),
			m_ssaoRT.srv.Get(), m_blurredRT.rtv.Get(),
			m_pointClampSampler.Get(), m_blurCB.Get(),
			screenW, screenH);
	}

	/// @brief 合成パスを実行する（シーン色 * AO）
	void applyCompositePass(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* sceneSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		/// ビューポート設定
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		/// レンダーターゲット設定
		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		/// シェーダー設定
		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_compositePS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ設定: t0=シーン, t1=AOブラー済み
		ID3D11ShaderResourceView* srvs[2] = {
			sceneSRV, m_blurredRT.srv.Get()
		};
		context->PSSetShaderResources(0, 2, srvs);

		/// サンプラー設定
		auto* samplerPtr = m_sampler.Get();
		context->PSSetSamplers(0, 1, &samplerPtr);

		/// フルスクリーン三角形を描画する
		context->Draw(3, 0);

		/// SRVバインドをクリアする
		ID3D11ShaderResourceView* nullSRVs[2] = {
			nullptr, nullptr
		};
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

	// ── メンバー変数 ────────────────────────────────

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11SamplerState> m_sampler;

	/// シェーダー
	ComPtr<ID3D11PixelShader> m_ssaoPS;
	ComPtr<ID3D11PixelShader> m_blurPS;
	ComPtr<ID3D11PixelShader> m_compositePS;

	/// 定数バッファ
	ComPtr<ID3D11Buffer> m_ssaoCB;
	ComPtr<ID3D11Buffer> m_blurCB;

	/// サンプラー
	ComPtr<ID3D11SamplerState> m_pointClampSampler;
	ComPtr<ID3D11SamplerState> m_noiseWrapSampler;

	/// ノイズテクスチャ
	ComPtr<ID3D11Texture2D> m_noiseTexture;
	ComPtr<ID3D11ShaderResourceView> m_noiseSRV;

	/// 中間レンダーターゲット
	PostProcessRT m_ssaoRT;
	PostProcessRT m_blurredRT;

	/// 深度SRV（外部から設定、所有権なし）
	ID3D11ShaderResourceView* m_depthSRV = nullptr;

	/// サンプルカーネル
	float m_kernel[16][4] = {};

	/// 射影行列
	float m_projection[16] = {};
	float m_invProjection[16] = {};

	/// スクリーンサイズ
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;

	/// 設定
	SSAOConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
