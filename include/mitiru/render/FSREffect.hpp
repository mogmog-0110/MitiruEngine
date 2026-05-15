#pragma once

/// @file FSREffect.hpp
/// @brief AMD FidelityFX Super Resolution 1.0（空間アップスケーリング）
/// @details FSR1の2パス構成を簡易実装する。
///          EASU: エッジ適応空間アップサンプリング（Lanczos風フィルタ）
///          RCAS: ロバストコントラスト適応シャープニング
///          入力は低解像度テクスチャ、出力は高解像度テクスチャ。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::render
{

// ============================================================================
// HLSL --- EASUピクセルシェーダー（簡易Lanczos風アップスケール）
// ============================================================================

/// @brief EASU（Edge-Adaptive Spatial Upsampling）シェーダー
/// @details 低解像度テクスチャからエッジを検出し、
///          Lanczos風の重み付きサンプリングでアップスケールする。
///          12サンプルの十字パターンで勾配を推定する。
constexpr std::string_view FSR_EASU_PS = R"hlsl(
Texture2D inputTexture : register(t0);
SamplerState linearClampSampler : register(s0);
SamplerState pointClampSampler : register(s1);

cbuffer EASUParams : register(b0)
{
	float2 inputSize;
	float2 outputSize;
	float2 inputTexelSize;
	float2 outputTexelSize;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

// ── Lanczos2重み関数 ───────────────────────────
float lanczos2(float x)
{
	if (abs(x) < 0.001)
	{
		return 1.0;
	}
	if (abs(x) >= 2.0)
	{
		return 0.0;
	}
	float pi_x = x * 3.14159265;
	return (sin(pi_x) / pi_x)
		* (sin(pi_x * 0.5) / (pi_x * 0.5));
}

// ── 輝度を計算する ─────────────────────────────
float luminance(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	// 出力ピクセルの入力テクスチャ上での位置を計算する
	float2 srcPos = input.texCoord * inputSize;
	float2 srcCenter = floor(srcPos - 0.5) + 0.5;

	// 4x4のLanczos2サンプリング
	float3 colorSum = float3(0, 0, 0);
	float weightSum = 0.0;

	[unroll]
	for (int y = -1; y <= 2; y++)
	{
		[unroll]
		for (int x = -1; x <= 2; x++)
		{
			float2 samplePos = srcCenter
				+ float2(float(x), float(y));
			float2 sampleUV = samplePos
				* inputTexelSize;

			float2 delta = srcPos - samplePos;
			float w = lanczos2(delta.x)
				* lanczos2(delta.y);

			float3 c = inputTexture.SampleLevel(
				linearClampSampler, sampleUV, 0).rgb;

			// エッジ適応: 輝度差が大きいサンプルの重みを下げる
			float3 centerColor = inputTexture.SampleLevel(
				linearClampSampler,
				srcCenter * inputTexelSize, 0).rgb;
			float lumDiff = abs(
				luminance(c) - luminance(centerColor));
			float edgeWeight = exp(-lumDiff * 4.0);

			w *= edgeWeight;
			colorSum += c * w;
			weightSum += w;
		}
	}

	float3 result = colorSum
		/ max(weightSum, 0.001);
	return float4(result, 1.0);
}
)hlsl";

// ============================================================================
// HLSL --- RCASピクセルシェーダー（コントラスト適応シャープニング）
// ============================================================================

/// @brief RCAS（Robust Contrast-Adaptive Sharpening）シェーダー
/// @details 十字パターンの5サンプルでローカルコントラストを推定し、
///          適応的にシャープニングを適用する。
///          リンギングアーティファクトを近傍min/maxでクランプする。
constexpr std::string_view FSR_RCAS_PS = R"hlsl(
Texture2D inputTexture : register(t0);
SamplerState pointClampSampler : register(s0);

cbuffer RCASParams : register(b0)
{
	float2 texelSize;
	float sharpness;
	float pad0;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float luminance(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = input.texCoord;

	// 十字パターンで5サンプル取得する
	float3 center = inputTexture.Sample(
		pointClampSampler, uv).rgb;
	float3 north = inputTexture.Sample(
		pointClampSampler,
		uv + float2(0, -texelSize.y)).rgb;
	float3 south = inputTexture.Sample(
		pointClampSampler,
		uv + float2(0, texelSize.y)).rgb;
	float3 east = inputTexture.Sample(
		pointClampSampler,
		uv + float2(texelSize.x, 0)).rgb;
	float3 west = inputTexture.Sample(
		pointClampSampler,
		uv + float2(-texelSize.x, 0)).rgb;

	// 近傍のmin/maxを計算する
	float3 nMin = min(min(north, south),
		min(east, west));
	float3 nMax = max(max(north, south),
		max(east, west));
	nMin = min(nMin, center);
	nMax = max(nMax, center);

	// ローカルコントラストからシャープニング強度を決定する
	float lumCenter = luminance(center);
	float lumN = luminance(north);
	float lumS = luminance(south);
	float lumE = luminance(east);
	float lumW = luminance(west);

	float lumMin = min(min(lumN, lumS),
		min(lumE, lumW));
	float lumMax = max(max(lumN, lumS),
		max(lumE, lumW));
	lumMin = min(lumMin, lumCenter);
	lumMax = max(lumMax, lumCenter);

	// コントラストが低い領域ほどシャープニングを強くする
	float contrast = lumMax - lumMin;
	float peakAmount = -1.0 / lerp(
		8.0, 5.0, saturate(sharpness));

	// 近傍平均との差分にシャープ係数を掛ける
	float3 neighbors = north + south + east + west;
	float w = max(peakAmount,
		-(1.0 / (max(contrast, 0.05) * 4.0 + 1.0)));

	float3 result = (center
		+ neighbors * w) / (1.0 + 4.0 * w);

	// リンギング防止: 近傍min/maxにクランプする
	result = clamp(result, nMin, nMax);

	return float4(result, 1.0);
}
)hlsl";

// ============================================================================
// FSRConfig --- FSR設定
// ============================================================================

/// @brief FSR1パラメータ設定
struct FSRConfig
{
	std::uint32_t inputWidth = 960;     ///< 入力解像度（幅）
	std::uint32_t inputHeight = 540;    ///< 入力解像度（高さ）
	std::uint32_t outputWidth = 1920;   ///< 出力解像度（幅）
	std::uint32_t outputHeight = 1080;  ///< 出力解像度（高さ）
	float sharpness = 0.5f;             ///< RCASシャープネス (0=弱, 1=強)
};

// ============================================================================
// FSREffect --- FSR1エフェクト
// ============================================================================

/// @brief AMD FidelityFX Super Resolution 1.0（空間アップスケーリング）
/// @details 2パス構成で低解像度テクスチャを高解像度にアップスケールする。
///          パス1（EASU）: エッジ適応Lanczos風アップサンプリング
///          パス2（RCAS）: コントラスト適応シャープニング
///
/// @code
/// FSREffect fsr;
/// FSRConfig cfg;
/// cfg.inputWidth = 960;
/// cfg.inputHeight = 540;
/// cfg.outputWidth = 1920;
/// cfg.outputHeight = 1080;
/// cfg.sharpness = 0.5f;
/// fsr.init(device, cfg);
///
/// fsr.apply(context, lowResSRV, outputRTV);
/// @endcode
class FSREffect final
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief 初期化
	/// @param device D3D11デバイス
	/// @param cfg FSR設定（入出力解像度とシャープネス）
	void init(ID3D11Device* device,
		const FSRConfig& cfg)
	{
		m_device = device;
		m_config = cfg;

		/// フルスクリーン頂点シェーダーをコンパイルする
		m_fullscreenVS = compileFullscreenVS(device);

		/// ピクセルシェーダーをコンパイルする
		m_easuPS = compilePS(device, FSR_EASU_PS);
		m_rcasPS = compilePS(device, FSR_RCAS_PS);

		/// 定数バッファを生成する
		m_easuCB = createCB(device, sizeof(EASUCB));
		m_rcasCB = createCB(device, sizeof(RCASCB));

		/// サンプラーを生成する
		m_linearClampSampler = createSampler(device,
			D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_CLAMP);
		m_pointClampSampler = createSampler(device,
			D3D11_FILTER_MIN_MAG_MIP_POINT,
			D3D11_TEXTURE_ADDRESS_CLAMP);

		/// EASU出力用中間バッファを生成する（出力解像度）
		m_easuRT = createRT(device,
			cfg.outputWidth, cfg.outputHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT);
	}

	/// @brief 設定を変更する（解像度変更時は再初期化推奨）
	void setConfig(const FSRConfig& cfg)
	{
		/// 解像度が変わった場合は中間バッファを再生成する
		if (cfg.outputWidth != m_config.outputWidth
			|| cfg.outputHeight != m_config.outputHeight)
		{
			m_easuRT = createRT(m_device.Get(),
				cfg.outputWidth, cfg.outputHeight,
				DXGI_FORMAT_R16G16B16A16_FLOAT);
		}
		m_config = cfg;
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const FSRConfig&
	config() const noexcept
	{
		return m_config;
	}

	/// @brief FSRを適用する
	/// @param context D3D11デバイスコンテキスト
	/// @param inputSRV 低解像度入力テクスチャのSRV
	/// @param outputRTV 高解像度出力先レンダーターゲット
	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV)
	{
		if (m_config.outputWidth == 0
			|| m_config.outputHeight == 0)
		{
			return;
		}

		/// パス1: EASU（低解像度 → 高解像度アップスケール）
		applyEASUPass(context, inputSRV);

		/// パス2: RCAS（シャープニング → 最終出力）
		applyRCASPass(context, outputRTV);
	}

private:
	// ── 定数バッファレイアウト ─────────────────────────

	/// @brief EASU定数バッファ（16バイトアライン）
	struct EASUCB
	{
		float inputSize[2];
		float outputSize[2];
		float inputTexelSize[2];
		float outputTexelSize[2];
	};

	/// @brief RCAS定数バッファ
	struct RCASCB
	{
		float texelSize[2];
		float sharpness;
		float pad0;
	};

	// ── パス実行 ────────────────────────────────────

	/// @brief EASUパスを実行する
	void applyEASUPass(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV)
	{
		EASUCB cbData = {};
		cbData.inputSize[0] =
			static_cast<float>(m_config.inputWidth);
		cbData.inputSize[1] =
			static_cast<float>(m_config.inputHeight);
		cbData.outputSize[0] =
			static_cast<float>(m_config.outputWidth);
		cbData.outputSize[1] =
			static_cast<float>(m_config.outputHeight);
		cbData.inputTexelSize[0] =
			1.0f / static_cast<float>(m_config.inputWidth);
		cbData.inputTexelSize[1] =
			1.0f / static_cast<float>(m_config.inputHeight);
		cbData.outputTexelSize[0] =
			1.0f / static_cast<float>(m_config.outputWidth);
		cbData.outputTexelSize[1] =
			1.0f / static_cast<float>(m_config.outputHeight);
		updateCB(context, m_easuCB.Get(),
			&cbData, sizeof(cbData));

		/// ビューポート: 出力解像度
		D3D11_VIEWPORT vp = {};
		vp.Width =
			static_cast<float>(m_config.outputWidth);
		vp.Height =
			static_cast<float>(m_config.outputHeight);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		/// レンダーターゲット設定
		auto* rtv = m_easuRT.rtv.Get();
		context->OMSetRenderTargets(1, &rtv, nullptr);

		/// シェーダー設定
		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_easuPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ設定
		context->PSSetShaderResources(
			0, 1, &inputSRV);

		/// サンプラー設定: s0=リニア, s1=ポイント
		ID3D11SamplerState* samplers[2] = {
			m_linearClampSampler.Get(),
			m_pointClampSampler.Get()
		};
		context->PSSetSamplers(0, 2, samplers);

		/// 定数バッファ設定
		auto* cb = m_easuCB.Get();
		context->PSSetConstantBuffers(0, 1, &cb);

		/// 描画
		context->Draw(3, 0);

		/// SRVクリア
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(
			0, 1, &nullSRV);
	}

	/// @brief RCASパスを実行する
	void applyRCASPass(
		ID3D11DeviceContext* context,
		ID3D11RenderTargetView* outputRTV)
	{
		RCASCB cbData = {};
		cbData.texelSize[0] =
			1.0f / static_cast<float>(m_config.outputWidth);
		cbData.texelSize[1] =
			1.0f / static_cast<float>(m_config.outputHeight);
		cbData.sharpness = m_config.sharpness;
		updateCB(context, m_rcasCB.Get(),
			&cbData, sizeof(cbData));

		/// ビューポート: 出力解像度
		D3D11_VIEWPORT vp = {};
		vp.Width =
			static_cast<float>(m_config.outputWidth);
		vp.Height =
			static_cast<float>(m_config.outputHeight);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		/// レンダーターゲット設定
		context->OMSetRenderTargets(
			1, &outputRTV, nullptr);

		/// シェーダー設定
		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_rcasPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ設定: EASU出力
		auto* easuSRV = m_easuRT.srv.Get();
		context->PSSetShaderResources(
			0, 1, &easuSRV);

		/// サンプラー設定
		context->PSSetSamplers(
			0, 1, m_pointClampSampler.GetAddressOf());

		/// 定数バッファ設定
		auto* cb = m_rcasCB.Get();
		context->PSSetConstantBuffers(0, 1, &cb);

		/// 描画
		context->Draw(3, 0);

		/// SRVクリア
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(
			0, 1, &nullSRV);
	}

	// ── 内部ユーティリティ ──────────────────────────────

	/// @brief フルスクリーン頂点シェーダーをコンパイルする
	[[nodiscard]] static ComPtr<ID3D11VertexShader>
	compileFullscreenVS(ID3D11Device* device)
	{
		constexpr std::string_view vsSource = R"hlsl(
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
	VSOutput output;
	output.texCoord = float2(
		(vertexID << 1) & 2, vertexID & 2);
	output.position = float4(
		output.texCoord * float2(2, -2)
		+ float2(-1, 1), 0, 1);
	return output;
}
)hlsl";

		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> errBlob;
		HRESULT hr = D3DCompile(
			vsSource.data(), vsSource.size(),
			"FsrFullscreenVS", nullptr, nullptr,
			"VSMain", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			blob.GetAddressOf(),
			errBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: VS compile failed");
		}

		ComPtr<ID3D11VertexShader> vs;
		hr = device->CreateVertexShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr, vs.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreateVertexShader failed");
		}
		return vs;
	}

	/// @brief ピクセルシェーダーをコンパイルする
	[[nodiscard]] static ComPtr<ID3D11PixelShader>
	compilePS(ID3D11Device* device,
		std::string_view source)
	{
		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> errBlob;
		HRESULT hr = D3DCompile(
			source.data(), source.size(),
			"FsrPS", nullptr, nullptr,
			"PSMain", "ps_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			blob.GetAddressOf(),
			errBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: PS compile failed");
		}

		ComPtr<ID3D11PixelShader> ps;
		hr = device->CreatePixelShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr, ps.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreatePixelShader failed");
		}
		return ps;
	}

	/// @brief 定数バッファを生成する
	[[nodiscard]] static ComPtr<ID3D11Buffer>
	createCB(ID3D11Device* device,
		std::uint32_t sizeBytes)
	{
		const auto aligned =
			(sizeBytes + 15u) & ~15u;

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = aligned;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		ComPtr<ID3D11Buffer> buffer;
		HRESULT hr = device->CreateBuffer(
			&desc, nullptr, buffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreateBuffer (CB) failed");
		}
		return buffer;
	}

	/// @brief 定数バッファを更新する
	static void updateCB(
		ID3D11DeviceContext* context,
		ID3D11Buffer* buffer,
		const void* data,
		std::uint32_t sizeBytes)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(buffer, 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, data, sizeBytes);
			context->Unmap(buffer, 0);
		}
	}

	/// @brief サンプラーを生成する
	[[nodiscard]] static ComPtr<ID3D11SamplerState>
	createSampler(ID3D11Device* device,
		D3D11_FILTER filter,
		D3D11_TEXTURE_ADDRESS_MODE addressMode)
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = filter;
		desc.AddressU = addressMode;
		desc.AddressV = addressMode;
		desc.AddressW = addressMode;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		ComPtr<ID3D11SamplerState> sampler;
		HRESULT hr = device->CreateSamplerState(
			&desc, sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreateSamplerState failed");
		}
		return sampler;
	}

	/// @brief レンダーターゲットを生成する
	struct RTData
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
	};

	[[nodiscard]] static RTData createRT(
		ID3D11Device* device,
		std::uint32_t w, std::uint32_t h,
		DXGI_FORMAT format)
	{
		RTData rt;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags =
			D3D11_BIND_SHADER_RESOURCE
			| D3D11_BIND_RENDER_TARGET;

		HRESULT hr = device->CreateTexture2D(
			&desc, nullptr,
			rt.texture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreateTexture2D failed");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.ViewDimension =
			D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(
			rt.texture.Get(), &srvDesc,
			rt.srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreateSRV failed");
		}

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = format;
		rtvDesc.ViewDimension =
			D3D11_RTV_DIMENSION_TEXTURE2D;

		hr = device->CreateRenderTargetView(
			rt.texture.Get(), &rtvDesc,
			rt.rtv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"FSREffect: CreateRTV failed");
		}

		return rt;
	}

	// ── メンバー変数 ────────────────────────────────

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;

	/// シェーダー
	ComPtr<ID3D11PixelShader> m_easuPS;
	ComPtr<ID3D11PixelShader> m_rcasPS;

	/// 定数バッファ
	ComPtr<ID3D11Buffer> m_easuCB;
	ComPtr<ID3D11Buffer> m_rcasCB;

	/// サンプラー
	ComPtr<ID3D11SamplerState> m_linearClampSampler;
	ComPtr<ID3D11SamplerState> m_pointClampSampler;

	/// EASU出力用中間バッファ
	RTData m_easuRT;

	/// 設定
	FSRConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
