#pragma once

/// @file VolumetricLighting.hpp
/// @brief ボリュメトリックライティング（ゴッドレイ + フォグ）
/// @details DX11ポストプロセスによるボリュメトリック光散乱とハイトフォグを実装する。
///          ラジアルブラーベースの高速ゴッドレイと高さ・距離ベースのフォグパスを提供する。

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
#include <stdexcept>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/PostProcess.hpp>

namespace mitiru::render
{

// ============================================================================
// 設定構造体
// ============================================================================

/// @brief ボリュメトリックライト設定
/// @details ゴッドレイの密度・散乱パラメータを制御する。
struct VolumetricConfig
{
	float density = 0.5f;          ///< 媒質の密度（光の減衰速度）
	float scattering = 0.3f;       ///< 散乱係数（0.0=なし, 1.0=最大）
	int numSteps = 32;             ///< レイマーチのステップ数（16-64）
	float lightColor[3]{1.0f, 0.95f, 0.8f}; ///< ライト色 (RGB)
	float lightPos[3]{0.5f, 0.0f, 0.0f};    ///< ライトのスクリーン空間位置 (NDC xy, z未使用)
	float decay = 0.97f;           ///< 各サンプルの減衰率
	float exposure = 0.25f;        ///< 最終的な露出スケール
	float weight = 0.5f;           ///< 散乱光の重み
};

/// @brief フォグ設定
/// @details 高さベース + 距離ベースのフォグパラメータを制御する。
struct FogConfig
{
	float color[4]{0.7f, 0.75f, 0.8f, 1.0f}; ///< フォグ色 (RGBA)
	float density = 0.02f;         ///< フォグ密度
	float startDistance = 10.0f;   ///< フォグ開始距離
	float heightFalloff = 0.1f;    ///< 高さ方向のフォグ減衰
	float maxFogFactor = 0.95f;    ///< フォグファクター最大値（完全に霧で覆わない）
};

// ============================================================================
// HLSL定数 — ゴッドレイ（ラジアルブラー方式）
// ============================================================================

/// @brief ゴッドレイ用ピクセルシェーダー
/// @details ライトのスクリーン位置に向かってラジアルブラーをかけ、
///          散乱光を蓄積する。深度バッファでオクルージョンをサンプルする。
constexpr std::string_view VOLUMETRIC_GODRAYS_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer VolumetricParams : register(b0)
{
	float density;
	float scattering;
	int numSteps;
	float _pad0;
	float3 lightColor;
	float decay;
	float2 lightScreenPos;
	float exposure;
	float weight;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 sceneColor = sceneTexture.Sample(linearSampler, input.texCoord);

	// ライトのスクリーン位置からピクセルへの方向ベクトルを計算する
	float2 deltaTexCoord = input.texCoord - lightScreenPos;
	deltaTexCoord *= (1.0 / float(numSteps)) * density;

	float2 currentCoord = input.texCoord;
	float illuminationDecay = 1.0;
	float3 accumulated = float3(0, 0, 0);

	// ラジアル方向にサンプルして散乱光を蓄積する
	for (int i = 0; i < numSteps; ++i)
	{
		currentCoord -= deltaTexCoord;

		// テクスチャ範囲外に出たらマーチングを終了する
		if (currentCoord.x < 0 || currentCoord.x > 1 || currentCoord.y < 0 || currentCoord.y > 1)
			break;

		// テクスチャ範囲外をクランプする
		float2 sampleCoord = saturate(currentCoord);

		// 深度バッファでオクルージョンを判定する
		float depthSample = depthTexture.Sample(linearSampler, sampleCoord).r;
		float occlusionMask = (depthSample > 0.999) ? 1.0 : 0.0;

		// シーンの輝度をサンプルする
		float3 sampleColor = sceneTexture.Sample(linearSampler, sampleCoord).rgb;
		float luminance = dot(sampleColor, float3(0.299, 0.587, 0.114));

		// 散乱光を蓄積する
		accumulated += (sampleColor * luminance + lightColor * occlusionMask * scattering)
			* illuminationDecay * weight;

		illuminationDecay *= decay;
	}

	// 散乱光をシーンに加算合成する
	float3 finalColor = sceneColor.rgb + accumulated * exposure;
	return float4(finalColor, sceneColor.a);
}
)hlsl";

// ============================================================================
// HLSL定数 — ボリュメトリックフォグ
// ============================================================================

/// @brief フォグ用ピクセルシェーダー
/// @details 高さベースのフォグファクターと距離ベースのフォグを組み合わせる。
///          深度バッファからワールド距離を再構成する。
constexpr std::string_view VOLUMETRIC_FOG_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer FogParams : register(b0)
{
	float4 fogColor;
	float fogDensity;
	float fogStartDistance;
	float fogHeightFalloff;
	float fogMaxFactor;
	float4 cameraPos;
	float nearPlane;
	float farPlane;
	float2 _fogPad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float linearizeDepth(float d, float near, float far)
{
	return near * far / (far - d * (far - near));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 sceneColor = sceneTexture.Sample(linearSampler, input.texCoord);
	float rawDepth = depthTexture.Sample(linearSampler, input.texCoord).r;

	// スカイボックス（depth >= 1.0）はフォグをスキップする
	if (rawDepth >= 0.999)
	{
		return sceneColor;
	}

	// 線形深度を再構成する
	float linearDepth = linearizeDepth(rawDepth, nearPlane, farPlane);

	// 距離ベースのフォグファクターを計算する
	float distanceFactor = max(0.0, linearDepth - fogStartDistance);
	float distanceFog = 1.0 - exp(-distanceFactor * fogDensity);

	// 高さベースのフォグファクターを計算する
	// 近似: 深度に基づいてワールドY座標を推定する
	float heightEstimate = cameraPos.y - linearDepth * 0.1;
	float heightFog = exp(-max(0.0, heightEstimate) * fogHeightFalloff);

	// 2つのフォグファクターを合成する
	float fogFactor = min(fogMaxFactor, distanceFog * heightFog);

	float3 finalColor = lerp(sceneColor.rgb, fogColor.rgb, fogFactor);
	return float4(finalColor, sceneColor.a);
}
)hlsl";

// ============================================================================
// ゴッドレイパスクラス
// ============================================================================

/// @brief ゴッドレイ（ラジアルブラー方式）パス
/// @details シーンテクスチャと深度バッファからボリュメトリック光散乱を計算する。
///
/// @code
/// VolumetricLightPass godRays;
/// godRays.init(device, 1280, 720);
///
/// VolumetricConfig config;
/// config.lightPos[0] = 0.5f; // スクリーン中央上部
/// config.lightPos[1] = 0.1f;
/// godRays.setConfig(config);
///
/// godRays.apply(context, sceneSRV, depthSRV, outputRTV);
/// @endcode
class VolumetricLightPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	VolumetricLightPass() noexcept = default;

	/// @brief パスを初期化する
	/// @param device D3D11デバイス
	/// @param width 出力幅
	/// @param height 出力高さ
	void init(ID3D11Device* device, int width, int height)
	{
		if (!device || width <= 0 || height <= 0)
		{
			throw std::runtime_error(
				"VolumetricLightPass: invalid parameters");
		}

		m_device = device;
		m_width = static_cast<std::uint32_t>(width);
		m_height = static_cast<std::uint32_t>(height);

		compileShaders();
		createConstantBuffer();
		createSampler();

		m_initialized = true;
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 設定を更新する
	/// @param config ボリュメトリックライト設定
	void setConfig(const VolumetricConfig& config) noexcept
	{
		m_config = config;
		m_config.numSteps = std::clamp(m_config.numSteps, 16, 64);
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const VolumetricConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief ビューポートサイズを更新する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(std::uint32_t width, std::uint32_t height) noexcept
	{
		if (width > 0 && height > 0)
		{
			m_width = width;
			m_height = height;
		}
	}

	/// @brief ゴッドレイを適用する
	/// @param context D3D11デバイスコンテキスト
	/// @param sceneSRV シーンテクスチャのSRV
	/// @param depthSRV 深度バッファのSRV
	/// @param outputRTV 出力先のRTV
	void apply(ID3D11DeviceContext* context,
	           ID3D11ShaderResourceView* sceneSRV,
	           ID3D11ShaderResourceView* depthSRV,
	           ID3D11RenderTargetView* outputRTV)
	{
		if (!m_initialized || !context || !sceneSRV || !depthSRV || !outputRTV)
		{
			return;
		}

		// ビューポートサイズをRTVから検出して更新する
		{
			ComPtr<ID3D11Resource> rtvRes;
			outputRTV->GetResource(rtvRes.GetAddressOf());
			ComPtr<ID3D11Texture2D> rtvTex;
			rtvRes.As(&rtvTex);
			if (rtvTex)
			{
				D3D11_TEXTURE2D_DESC texDesc = {};
				rtvTex->GetDesc(&texDesc);
				m_width = texDesc.Width;
				m_height = texDesc.Height;
			}
		}

		updateConstantBuffer(context);

		// レンダーターゲットを設定する
		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		// シェーダーリソースを設定する
		ID3D11ShaderResourceView* srvs[] = { sceneSRV, depthSRV };
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		context->PSSetConstantBuffers(0, 1, m_cbBuffer.GetAddressOf());

		// フルスクリーン描画する
		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_godraysPS.Get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->Draw(3, 0);

		// SRVをアンバインドする
		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

private:
	/// @brief ゴッドレイ定数バッファ（GPU転送用）
	struct alignas(16) CbVolumetric
	{
		float density;
		float scattering;
		int numSteps;
		float _pad0;
		float lightColor[3];
		float decay;
		float lightScreenPos[2];
		float exposure;
		float weight;
	};

	/// @brief シェーダーをコンパイルする
	void compileShaders()
	{
		// フルスクリーン頂点シェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string vsSource(PP_FULLSCREEN_VS);
			HRESULT hr = D3DCompile(
				vsSource.data(), vsSource.size(),
				"VolumetricFullscreenVS", nullptr, nullptr,
				"VSMain", "vs_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricLightPass: VS compile failed");
			}
			hr = m_device->CreateVertexShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_fullscreenVS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricLightPass: CreateVertexShader failed");
			}
		}

		// ゴッドレイピクセルシェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string psSource(VOLUMETRIC_GODRAYS_PS);
			HRESULT hr = D3DCompile(
				psSource.data(), psSource.size(),
				"VolumetricGodraysPS", nullptr, nullptr,
				"PSMain", "ps_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricLightPass: PS compile failed");
			}
			hr = m_device->CreatePixelShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_godraysPS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricLightPass: CreatePixelShader failed");
			}
		}
	}

	/// @brief 定数バッファを作成する
	void createConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbVolumetric);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"VolumetricLightPass: CreateBuffer failed");
		}
	}

	/// @brief リニアクランプサンプラーを作成する
	void createSampler()
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

		HRESULT hr = m_device->CreateSamplerState(
			&desc, m_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"VolumetricLightPass: CreateSamplerState failed");
		}
	}

	/// @brief 定数バッファを更新する
	/// @param context D3D11デバイスコンテキスト
	void updateConstantBuffer(ID3D11DeviceContext* context)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_cbBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		CbVolumetric cb = {};
		cb.density = m_config.density;
		cb.scattering = m_config.scattering;
		cb.numSteps = m_config.numSteps;
		cb.lightColor[0] = m_config.lightColor[0];
		cb.lightColor[1] = m_config.lightColor[1];
		cb.lightColor[2] = m_config.lightColor[2];
		cb.decay = m_config.decay;
		cb.lightScreenPos[0] = m_config.lightPos[0];
		cb.lightScreenPos[1] = m_config.lightPos[1];
		cb.exposure = m_config.exposure;
		cb.weight = m_config.weight;

		std::memcpy(mapped.pData, &cb, sizeof(cb));
		context->Unmap(m_cbBuffer.Get(), 0);
	}

	ComPtr<ID3D11Device> m_device;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
	bool m_initialized = false;

	VolumetricConfig m_config;

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_godraysPS;
	ComPtr<ID3D11Buffer> m_cbBuffer;
	ComPtr<ID3D11SamplerState> m_sampler;
};

// ============================================================================
// フォグパスクラス
// ============================================================================

/// @brief ボリュメトリックフォグパス
/// @details 高さベース + 距離ベースのフォグ効果をポストプロセスで適用する。
///
/// @code
/// VolumetricFogPass fogPass;
/// fogPass.init(device, 1280, 720);
///
/// FogConfig fogCfg;
/// fogCfg.density = 0.03f;
/// fogCfg.startDistance = 20.0f;
/// fogPass.setConfig(fogCfg);
///
/// fogPass.apply(context, sceneSRV, depthSRV, outputRTV, cameraPos, 0.1f, 1000.0f);
/// @endcode
class VolumetricFogPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	VolumetricFogPass() noexcept = default;

	/// @brief パスを初期化する
	/// @param device D3D11デバイス
	/// @param width 出力幅
	/// @param height 出力高さ
	void init(ID3D11Device* device, int width, int height)
	{
		if (!device || width <= 0 || height <= 0)
		{
			throw std::runtime_error(
				"VolumetricFogPass: invalid parameters");
		}

		m_device = device;
		m_width = static_cast<std::uint32_t>(width);
		m_height = static_cast<std::uint32_t>(height);

		compileShaders();
		createConstantBuffer();
		createSampler();

		m_initialized = true;
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 設定を更新する
	/// @param config フォグ設定
	void setConfig(const FogConfig& config) noexcept
	{
		m_config = config;
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const FogConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief ビューポートサイズを更新する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(std::uint32_t width, std::uint32_t height) noexcept
	{
		if (width > 0 && height > 0)
		{
			m_width = width;
			m_height = height;
		}
	}

	/// @brief フォグを適用する
	/// @param context D3D11デバイスコンテキスト
	/// @param sceneSRV シーンテクスチャのSRV
	/// @param depthSRV 深度バッファのSRV
	/// @param outputRTV 出力先のRTV
	/// @param cameraPos カメラのワールド位置
	/// @param nearPlane ニアクリップ面
	/// @param farPlane ファークリップ面
	void apply(ID3D11DeviceContext* context,
	           ID3D11ShaderResourceView* sceneSRV,
	           ID3D11ShaderResourceView* depthSRV,
	           ID3D11RenderTargetView* outputRTV,
	           const float cameraPos[3],
	           float nearPlane,
	           float farPlane)
	{
		if (!m_initialized || !context || !sceneSRV || !depthSRV || !outputRTV)
		{
			return;
		}

		// ビューポートサイズをRTVから検出して更新する
		{
			ComPtr<ID3D11Resource> rtvRes;
			outputRTV->GetResource(rtvRes.GetAddressOf());
			ComPtr<ID3D11Texture2D> rtvTex;
			rtvRes.As(&rtvTex);
			if (rtvTex)
			{
				D3D11_TEXTURE2D_DESC texDesc = {};
				rtvTex->GetDesc(&texDesc);
				m_width = texDesc.Width;
				m_height = texDesc.Height;
			}
		}

		updateConstantBuffer(context, cameraPos, nearPlane, farPlane);

		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		ID3D11ShaderResourceView* srvs[] = { sceneSRV, depthSRV };
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		context->PSSetConstantBuffers(0, 1, m_cbBuffer.GetAddressOf());

		context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(m_fogPS.Get(), nullptr, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->Draw(3, 0);

		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

private:
	/// @brief フォグ定数バッファ（GPU転送用）
	struct alignas(16) CbFog
	{
		float fogColor[4];
		float fogDensity;
		float fogStartDistance;
		float fogHeightFalloff;
		float fogMaxFactor;
		float cameraPos[4];
		float nearPlane;
		float farPlane;
		float _pad[2];
	};

	/// @brief シェーダーをコンパイルする
	void compileShaders()
	{
		// フルスクリーン頂点シェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string vsSource(PP_FULLSCREEN_VS);
			HRESULT hr = D3DCompile(
				vsSource.data(), vsSource.size(),
				"FogFullscreenVS", nullptr, nullptr,
				"VSMain", "vs_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricFogPass: VS compile failed");
			}
			hr = m_device->CreateVertexShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_fullscreenVS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricFogPass: CreateVertexShader failed");
			}
		}

		// フォグピクセルシェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string psSource(VOLUMETRIC_FOG_PS);
			HRESULT hr = D3DCompile(
				psSource.data(), psSource.size(),
				"VolumetricFogPS", nullptr, nullptr,
				"PSMain", "ps_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricFogPass: PS compile failed");
			}
			hr = m_device->CreatePixelShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_fogPS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"VolumetricFogPass: CreatePixelShader failed");
			}
		}
	}

	/// @brief 定数バッファを作成する
	void createConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbFog);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"VolumetricFogPass: CreateBuffer failed");
		}
	}

	/// @brief リニアクランプサンプラーを作成する
	void createSampler()
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

		HRESULT hr = m_device->CreateSamplerState(
			&desc, m_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"VolumetricFogPass: CreateSamplerState failed");
		}
	}

	/// @brief 定数バッファを更新する
	/// @param context D3D11デバイスコンテキスト
	/// @param cameraPos カメラ位置
	/// @param nearPlane ニアクリップ面
	/// @param farPlane ファークリップ面
	void updateConstantBuffer(ID3D11DeviceContext* context,
	                          const float cameraPos[3],
	                          float nearPlane,
	                          float farPlane)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_cbBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		CbFog cb = {};
		std::memcpy(cb.fogColor, m_config.color, sizeof(float) * 4);
		cb.fogDensity = m_config.density;
		cb.fogStartDistance = m_config.startDistance;
		cb.fogHeightFalloff = m_config.heightFalloff;
		cb.fogMaxFactor = m_config.maxFogFactor;
		cb.cameraPos[0] = cameraPos[0];
		cb.cameraPos[1] = cameraPos[1];
		cb.cameraPos[2] = cameraPos[2];
		cb.cameraPos[3] = 0.0f;
		cb.nearPlane = nearPlane;
		cb.farPlane = farPlane;

		std::memcpy(mapped.pData, &cb, sizeof(cb));
		context->Unmap(m_cbBuffer.Get(), 0);
	}

	ComPtr<ID3D11Device> m_device;
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
	bool m_initialized = false;

	FogConfig m_config;

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_fogPS;
	ComPtr<ID3D11Buffer> m_cbBuffer;
	ComPtr<ID3D11SamplerState> m_sampler;
};

} // namespace mitiru::render

#endif // _WIN32
