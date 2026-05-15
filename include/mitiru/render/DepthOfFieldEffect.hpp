#pragma once

/// @file DepthOfFieldEffect.hpp
/// @brief ボケ被写界深度（Depth of Field）ポストプロセス
/// @details 深度バッファとシーン色から錯乱円（CoC）を計算し、
///          ディスクブラーでボケ効果を適用する。
///          2パス構成: CoC計算 → CoC重み付きブラー。

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

namespace mitiru::render
{

// ============================================================================
// HLSL --- CoC計算ピクセルシェーダー
// ============================================================================

/// @brief 錯乱円（Circle of Confusion）を計算するシェーダー
/// @details 深度バッファからリニア深度を求め、薄レンズモデルで
///          CoCの直径を算出する。結果はR16F形式で出力。
constexpr std::string_view DOF_COC_PS = R"hlsl(
Texture2D depthTexture : register(t0);
SamplerState pointClampSampler : register(s0);

cbuffer DofCoCParams : register(b0)
{
	float focusDistance;
	float aperture;
	float focalLength;
	float nearPlane;
	float farPlane;
	float maxCoCRadius;
	float2 pad0;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float linearizeDepth(float d)
{
	return (nearPlane * farPlane)
		/ (farPlane - d * (farPlane - nearPlane));
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float depthVal = depthTexture.Sample(
		pointClampSampler, input.texCoord).r;

	// 遠方ピクセルはフォーカス外として最大CoCを返す
	if (depthVal >= 1.0)
	{
		return float4(maxCoCRadius, 0, 0, 1);
	}

	float z = linearizeDepth(depthVal);

	// 薄レンズモデルによるCoC計算
	// CoC = |aperture * focalLength * (focusDist - z)|
	//       / (z * (focusDist - focalLength))
	float denom = z * (focusDistance - focalLength);
	float coc = 0.0;
	if (abs(denom) > 0.0001)
	{
		coc = aperture * focalLength
			* (focusDistance - z) / denom;
	}

	// ピクセル単位にスケールし、最大半径でクランプする
	coc = clamp(coc, -maxCoCRadius, maxCoCRadius);

	return float4(coc, 0, 0, 1);
}
)hlsl";

// ============================================================================
// HLSL --- DoFブラーピクセルシェーダー
// ============================================================================

/// @brief CoC重み付きディスクブラーシェーダー
/// @details CoCテクスチャを参照し、各ピクセルのボケ半径に応じた
///          ディスクサンプリングでブラーを適用する。
///          16サンプルのPoisson Diskパターンを使用。
constexpr std::string_view DOF_BLUR_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D cocTexture : register(t1);
SamplerState linearClampSampler : register(s0);
SamplerState pointClampSampler : register(s1);

cbuffer DofBlurParams : register(b0)
{
	float2 texelSize;
	float maxCoCRadius;
	float blurScale;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

// 16サンプルのPoissonディスクパターン
static const float2 kPoissonDisk[16] = {
	float2(-0.94201624, -0.39906216),
	float2( 0.94558609, -0.76890725),
	float2(-0.09418410, -0.92938870),
	float2( 0.34495938,  0.29387760),
	float2(-0.91588581,  0.45771432),
	float2(-0.81544232, -0.87912464),
	float2(-0.38277543,  0.27676845),
	float2( 0.97484398,  0.75648379),
	float2( 0.44323325, -0.97511554),
	float2( 0.53742981, -0.47373420),
	float2(-0.26496911, -0.41893023),
	float2( 0.79197514,  0.19090188),
	float2(-0.24188840,  0.99706507),
	float2(-0.81409955,  0.91437590),
	float2( 0.19984126,  0.78641367),
	float2( 0.14383161, -0.14100790)
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float centerCoC = cocTexture.Sample(
		pointClampSampler, input.texCoord).r;
	float absCoC = abs(centerCoC);

	// CoCがゼロに近い場合はブラーなし
	if (absCoC < 0.5)
	{
		return sceneTexture.Sample(
			linearClampSampler, input.texCoord);
	}

	float radius = absCoC * blurScale;
	float4 colorSum = float4(0, 0, 0, 0);
	float weightSum = 0.0;

	[unroll]
	for (int i = 0; i < 16; i++)
	{
		float2 offset = kPoissonDisk[i] * radius * texelSize;
		float2 sampleUV = input.texCoord + offset;

		float sampleCoC = cocTexture.Sample(
			pointClampSampler, sampleUV).r;
		float4 sampleColor = sceneTexture.Sample(
			linearClampSampler, sampleUV);

		// 前景ボケ: サンプルのCoCが負ならリーク防止
		float weight = (abs(sampleCoC) >= absCoC * 0.3)
			? 1.0 : 0.2;

		colorSum += sampleColor * weight;
		weightSum += weight;
	}

	return colorSum / max(weightSum, 0.001);
}
)hlsl";

// ============================================================================
// DepthOfFieldConfig --- 被写界深度設定
// ============================================================================

/// @brief DoFパラメータ設定
struct DepthOfFieldConfig
{
	float focusDistance = 5.0f;     ///< フォーカス距離（ワールド単位）
	float aperture = 2.8f;         ///< 絞り値（F値）
	float focalLength = 0.05f;     ///< 焦点距離（メートル単位、50mm=0.05）
	float nearPlane = 0.1f;        ///< カメラニアクリップ面
	float farPlane = 100.0f;       ///< カメラファークリップ面
	float maxCoCRadius = 10.0f;    ///< 最大CoC半径（ピクセル単位）
	float blurScale = 1.0f;        ///< ブラーの強さスケール
};

// ============================================================================
// DepthOfFieldEffect --- 被写界深度エフェクト
// ============================================================================

/// @brief ボケ被写界深度ポストプロセス
/// @details 2パス構成で深度ベースのボケ効果を適用する。
///          パス1: 深度バッファからCoCテクスチャを生成
///          パス2: CoCに基づくディスクブラーでシーンにボケを適用
///
/// @code
/// DepthOfFieldEffect dof;
/// dof.init(device, 1280, 720);
///
/// DepthOfFieldConfig cfg;
/// cfg.focusDistance = 3.0f;
/// cfg.aperture = 1.4f;
/// dof.setConfig(cfg);
///
/// dof.setDepthSRV(depthSRV);
/// dof.apply(context, sceneSRV, outputRTV, 1280, 720);
/// @endcode
class DepthOfFieldEffect final
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief 初期化
	/// @param device D3D11デバイス
	/// @param screenW 初期スクリーン幅
	/// @param screenH 初期スクリーン高さ
	void init(ID3D11Device* device,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		m_device = device;
		m_width = screenW;
		m_height = screenH;

		/// フルスクリーン頂点シェーダーをコンパイルする
		m_fullscreenVS = compileFullscreenVS(device);

		/// ピクセルシェーダーをコンパイルする
		m_cocPS = compilePS(device, DOF_COC_PS);
		m_blurPS = compilePS(device, DOF_BLUR_PS);

		/// 定数バッファを生成する
		m_cocCB = createCB(device, sizeof(CoCCB));
		m_blurCB = createCB(device, sizeof(BlurCB));

		/// サンプラーを生成する
		m_pointClampSampler = createSampler(device,
			D3D11_FILTER_MIN_MAG_MIP_POINT,
			D3D11_TEXTURE_ADDRESS_CLAMP);
		m_linearClampSampler = createSampler(device,
			D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_CLAMP);

		/// 中間レンダーターゲットを生成する
		m_cocRT = createRT(device, screenW, screenH,
			DXGI_FORMAT_R16_FLOAT);
	}

	/// @brief 深度SRVを設定する
	void setDepthSRV(
		ID3D11ShaderResourceView* depthSRV) noexcept
	{
		m_depthSRV = depthSRV;
	}

	/// @brief 設定を変更する
	void setConfig(
		const DepthOfFieldConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const DepthOfFieldConfig&
	config() const noexcept
	{
		return m_config;
	}

	/// @brief DoFを適用する
	/// @param context D3D11デバイスコンテキスト
	/// @param inputSRV シーン色テクスチャのSRV
	/// @param outputRTV 出力先レンダーターゲット
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		if (screenW == 0 || screenH == 0)
		{
			return;
		}

		if (!m_depthSRV)
		{
			/// 深度SRV未設定ならパススルーする
			drawFullscreen(context, inputSRV, outputRTV,
				nullptr, nullptr, 1,
				m_linearClampSampler.Get(), nullptr,
				screenW, screenH);
			return;
		}

		/// リサイズ検出
		if (screenW != m_width || screenH != m_height)
		{
			m_cocRT = createRT(m_device.Get(),
				screenW, screenH,
				DXGI_FORMAT_R16_FLOAT);
			m_width = screenW;
			m_height = screenH;
		}

		/// パス1: CoC計算（深度 → CoCテクスチャ）
		applyCoCPass(context, screenW, screenH);

		/// パス2: ディスクブラー（シーン色 + CoC → 出力）
		applyBlurPass(context, inputSRV, outputRTV,
			screenW, screenH);
	}

private:
	// ── 定数バッファレイアウト ─────────────────────────

	/// @brief CoC定数バッファ（16バイトアライン）
	struct CoCCB
	{
		float focusDistance;
		float aperture;
		float focalLength;
		float nearPlane;
		float farPlane;
		float maxCoCRadius;
		float pad0[2];
	};

	/// @brief ブラー定数バッファ
	struct BlurCB
	{
		float texelSize[2];
		float maxCoCRadius;
		float blurScale;
	};

	// ── パス実行 ────────────────────────────────────

	/// @brief CoC計算パスを実行する
	void applyCoCPass(
		ID3D11DeviceContext* context,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		CoCCB cbData = {};
		cbData.focusDistance = m_config.focusDistance;
		cbData.aperture = m_config.aperture;
		cbData.focalLength = m_config.focalLength;
		cbData.nearPlane = m_config.nearPlane;
		cbData.farPlane = m_config.farPlane;
		cbData.maxCoCRadius = m_config.maxCoCRadius;
		updateCB(context, m_cocCB.Get(),
			&cbData, sizeof(cbData));

		ID3D11ShaderResourceView* srvs[1] = {
			m_depthSRV
		};
		drawFullscreen(context, nullptr,
			m_cocRT.rtv.Get(),
			srvs, nullptr, 1,
			m_pointClampSampler.Get(), m_cocCB.Get(),
			screenW, screenH);
	}

	/// @brief ディスクブラーパスを実行する
	void applyBlurPass(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* sceneSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		BlurCB cbData = {};
		cbData.texelSize[0] =
			1.0f / static_cast<float>(screenW);
		cbData.texelSize[1] =
			1.0f / static_cast<float>(screenH);
		cbData.maxCoCRadius = m_config.maxCoCRadius;
		cbData.blurScale = m_config.blurScale;
		updateCB(context, m_blurCB.Get(),
			&cbData, sizeof(cbData));

		ID3D11ShaderResourceView* srvs[2] = {
			sceneSRV, m_cocRT.srv.Get()
		};
		ID3D11SamplerState* samplers[2] = {
			m_linearClampSampler.Get(),
			m_pointClampSampler.Get()
		};

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
		context->PSSetShader(m_blurPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ設定
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 2, samplers);

		/// 定数バッファ設定
		auto* cb = m_blurCB.Get();
		context->PSSetConstantBuffers(0, 1, &cb);

		/// 描画
		context->Draw(3, 0);

		/// SRVクリア
		ID3D11ShaderResourceView* nullSRVs[2] = {
			nullptr, nullptr
		};
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

	// ── 内部ユーティリティ ──────────────────────────────

	/// @brief フルスクリーン三角形頂点シェーダーをコンパイルする
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
			"DofFullscreenVS", nullptr, nullptr,
			"VSMain", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			blob.GetAddressOf(),
			errBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"DepthOfFieldEffect: VS compile failed");
		}

		ComPtr<ID3D11VertexShader> vs;
		hr = device->CreateVertexShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr, vs.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"DepthOfFieldEffect: CreateVertexShader failed");
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
			"DofPS", nullptr, nullptr,
			"PSMain", "ps_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			blob.GetAddressOf(),
			errBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"DepthOfFieldEffect: PS compile failed");
		}

		ComPtr<ID3D11PixelShader> ps;
		hr = device->CreatePixelShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr, ps.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"DepthOfFieldEffect: CreatePixelShader failed");
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
				"DepthOfFieldEffect: CreateBuffer (CB) failed");
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
				"DepthOfFieldEffect: CreateSamplerState failed");
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
				"DepthOfFieldEffect: CreateTexture2D failed");
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
				"DepthOfFieldEffect: CreateSRV failed");
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
				"DepthOfFieldEffect: CreateRTV failed");
		}

		return rt;
	}

	/// @brief フルスクリーンパスを描画する（汎用）
	void drawFullscreen(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		ID3D11ShaderResourceView* const* extraSRVs,
		ID3D11SamplerState* const* extraSamplers,
		std::uint32_t srvCount,
		ID3D11SamplerState* mainSampler,
		ID3D11Buffer* cb,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		context->OMSetRenderTargets(
			1, &outputRTV, nullptr);

		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_cocPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (extraSRVs != nullptr)
		{
			context->PSSetShaderResources(
				0, srvCount, extraSRVs);
		}
		else if (inputSRV != nullptr)
		{
			context->PSSetShaderResources(
				0, 1, &inputSRV);
		}

		if (mainSampler != nullptr)
		{
			context->PSSetSamplers(0, 1, &mainSampler);
		}

		if (cb != nullptr)
		{
			context->PSSetConstantBuffers(0, 1, &cb);
		}

		context->Draw(3, 0);

		/// SRVクリア
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(
			0, 1, &nullSRV);
	}

	// ── メンバー変数 ────────────────────────────────

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;

	/// シェーダー
	ComPtr<ID3D11PixelShader> m_cocPS;
	ComPtr<ID3D11PixelShader> m_blurPS;

	/// 定数バッファ
	ComPtr<ID3D11Buffer> m_cocCB;
	ComPtr<ID3D11Buffer> m_blurCB;

	/// サンプラー
	ComPtr<ID3D11SamplerState> m_pointClampSampler;
	ComPtr<ID3D11SamplerState> m_linearClampSampler;

	/// 中間レンダーターゲット（CoCテクスチャ）
	RTData m_cocRT;

	/// 深度SRV（外部から設定、所有権なし）
	ID3D11ShaderResourceView* m_depthSRV = nullptr;

	/// スクリーンサイズ
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;

	/// 設定
	DepthOfFieldConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
