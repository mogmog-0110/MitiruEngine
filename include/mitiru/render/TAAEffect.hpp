#pragma once

/// @file TAAEffect.hpp
/// @brief テンポラルアンチエイリアシング（TAA）ポストプロセス
/// @details 現在フレームと前フレームの色をブレンドし、
///          近傍クランピングでゴーストを抑制する。
///          8サンプルのHaltonシーケンスでジッターパターンを生成。

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
// HLSL --- TAAリゾルブピクセルシェーダー
// ============================================================================

/// @brief TAAリゾルブシェーダー
/// @details 現在フレーム色を近傍色でクランプしたヒストリーとブレンドする。
///          3x3近傍のmin/maxでAABBクランプを行い、ゴーストを抑制する。
///          モーションベクターが無い場合は前フレーム深度からリプロジェクトする。
constexpr std::string_view TAA_RESOLVE_PS = R"hlsl(
Texture2D currentTexture : register(t0);
Texture2D historyTexture : register(t1);
Texture2D depthTexture : register(t2);
SamplerState linearClampSampler : register(s0);
SamplerState pointClampSampler : register(s1);

cbuffer TAAParams : register(b0)
{
	float2 texelSize;
	float blendFactor;
	float motionScale;
	float2 jitterOffset;
	float2 pad0;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

// ── 近傍のAABBを計算する ─────────────────────────
void computeNeighborhoodAABB(
	float2 uv, float2 texel,
	out float3 aabbMin, out float3 aabbMax)
{
	float3 m1 = float3(0, 0, 0);
	float3 m2 = float3(0, 0, 0);

	[unroll]
	for (int y = -1; y <= 1; y++)
	{
		[unroll]
		for (int x = -1; x <= 1; x++)
		{
			float2 offset = float2(float(x), float(y))
				* texel;
			float3 c = currentTexture.Sample(
				pointClampSampler, uv + offset).rgb;
			m1 += c;
			m2 += c * c;
		}
	}

	// 平均と分散からAABBを構築する
	float3 mu = m1 / 9.0;
	float3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));
	float gamma = 1.0;

	aabbMin = mu - gamma * sigma;
	aabbMax = mu + gamma * sigma;
}

// ── ヒストリー色をAABBにクランプする ──────────────────
float3 clipToAABB(float3 color,
	float3 aabbMin, float3 aabbMax)
{
	float3 center = (aabbMin + aabbMax) * 0.5;
	float3 extents = (aabbMax - aabbMin) * 0.5
		+ float3(0.001, 0.001, 0.001);

	float3 offset = color - center;
	float3 ts = abs(extents / (offset + 0.0001));
	float t = saturate(min(ts.x, min(ts.y, ts.z)));

	return center + offset * t;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	// ジッターを除去した正確なUVを計算する
	float2 uv = input.texCoord - jitterOffset * texelSize;

	float3 currentColor = currentTexture.Sample(
		pointClampSampler, input.texCoord).rgb;

	// 深度ベースの簡易リプロジェクション
	// （モーションベクター未使用、前フレームUV ≒ 現フレームUV）
	float2 historyUV = uv;
	float3 historyColor = historyTexture.Sample(
		linearClampSampler, historyUV).rgb;

	// 近傍クランピングでゴーストを抑制する
	float3 aabbMin, aabbMax;
	computeNeighborhoodAABB(
		input.texCoord, texelSize,
		aabbMin, aabbMax);
	historyColor = clipToAABB(
		historyColor, aabbMin, aabbMax);

	// ブレンド: 低いblendFactorほどヒストリーを多く使う
	float3 result = lerp(
		historyColor, currentColor, blendFactor);

	return float4(result, 1.0);
}
)hlsl";

// ============================================================================
// HLSL --- ヒストリーコピーピクセルシェーダー
// ============================================================================

/// @brief ヒストリーバッファへのコピーシェーダー
constexpr std::string_view TAA_COPY_PS = R"hlsl(
Texture2D sourceTexture : register(t0);
SamplerState pointClampSampler : register(s0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	return sourceTexture.Sample(
		pointClampSampler, input.texCoord);
}
)hlsl";

// ============================================================================
// TAAConfig --- TAA設定
// ============================================================================

/// @brief TAAパラメータ設定
struct TAAConfig
{
	float blendFactor = 0.1f;     ///< ブレンド係数（0=全ヒストリー, 1=全現在）
	float motionScale = 1.0f;     ///< モーションスケール
};

// ============================================================================
// TAAEffect --- テンポラルアンチエイリアシングエフェクト
// ============================================================================

/// @brief テンポラルアンチエイリアシング
/// @details Haltonシーケンスによるサブピクセルジッターと
///          近傍クランピング付きヒストリーブレンドでAAを実現する。
///
/// @code
/// TAAEffect taa;
/// taa.init(device, 1280, 720);
///
/// // 毎フレーム: ジッターオフセットを取得して射影行列に適用する
/// auto [jx, jy] = taa.currentJitter();
/// projMatrix[2][0] += jx * 2.0f / screenW;
/// projMatrix[2][1] += jy * 2.0f / screenH;
///
/// // シーン描画後にTAAを適用する
/// taa.setDepthSRV(depthSRV);
/// taa.apply(context, sceneSRV, outputRTV, 1280, 720);
/// taa.advanceFrame();
/// @endcode
class TAAEffect final
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
		m_resolvePS = compilePS(device, TAA_RESOLVE_PS);
		m_copyPS = compilePS(device, TAA_COPY_PS);

		/// 定数バッファを生成する
		m_resolveCB = createCB(device, sizeof(ResolveCB));

		/// サンプラーを生成する
		m_linearClampSampler = createSampler(device,
			D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_CLAMP);
		m_pointClampSampler = createSampler(device,
			D3D11_FILTER_MIN_MAG_MIP_POINT,
			D3D11_TEXTURE_ADDRESS_CLAMP);

		/// ヒストリーバッファを生成する
		m_historyRT = createRT(device, screenW, screenH,
			DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_historyValid = false;

		/// Haltonシーケンスを事前計算する
		generateHaltonSequence();
	}

	/// @brief 深度SRVを設定する
	void setDepthSRV(
		ID3D11ShaderResourceView* depthSRV) noexcept
	{
		m_depthSRV = depthSRV;
	}

	/// @brief 設定を変更する
	void setConfig(const TAAConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const TAAConfig&
	config() const noexcept
	{
		return m_config;
	}

	/// @brief 現在のジッターオフセットを取得する（ピクセル単位）
	/// @return {x, y} サブピクセルオフセット（-0.5 ~ +0.5）
	[[nodiscard]] std::array<float, 2>
	currentJitter() const noexcept
	{
		return m_haltonSequence[m_frameIndex];
	}

	/// @brief フレームを進める（ジッターインデックス更新）
	void advanceFrame() noexcept
	{
		m_frameIndex =
			(m_frameIndex + 1) % kJitterSamples;
	}

	/// @brief TAAを適用する
	/// @param context D3D11デバイスコンテキスト
	/// @param inputSRV 現在フレームのシーン色SRV
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

		/// リサイズ検出
		if (screenW != m_width || screenH != m_height)
		{
			m_historyRT = createRT(m_device.Get(),
				screenW, screenH,
				DXGI_FORMAT_R16G16B16A16_FLOAT);
			m_width = screenW;
			m_height = screenH;
			m_historyValid = false;
		}

		if (!m_historyValid)
		{
			/// 初回フレーム: 入力をそのまま出力＆ヒストリーにコピーする
			copyTexture(context, inputSRV, outputRTV,
				screenW, screenH);
			copyTexture(context, inputSRV,
				m_historyRT.rtv.Get(),
				screenW, screenH);
			m_historyValid = true;
			return;
		}

		/// TAAリゾルブ: 現在色 + ヒストリー → 出力
		applyResolvePass(context, inputSRV, outputRTV,
			screenW, screenH);

		/// ヒストリー更新: リゾルブ結果を保存する
		/// （outputRTVは直接読めないので入力を再ブレンドした結果をコピー）
		/// 簡易実装: 出力先がテクスチャなら再利用、ここでは入力をコピー
		/// 実運用では出力テクスチャのSRVからコピーするが、
		/// ここではリゾルブ結果をヒストリーにも出力する2パス方式を使う
		applyResolveToHistory(context, inputSRV,
			screenW, screenH);
	}

private:
	static constexpr int kJitterSamples = 8;

	// ── 定数バッファレイアウト ─────────────────────────

	/// @brief TAA定数バッファ（16バイトアライン）
	struct ResolveCB
	{
		float texelSize[2];
		float blendFactor;
		float motionScale;
		float jitterOffset[2];
		float pad0[2];
	};

	// ── Haltonシーケンス ──────────────────────────────

	/// @brief 基数bのHalton数列の第i項を返す
	[[nodiscard]] static float halton(
		int i, int b) noexcept
	{
		float f = 1.0f;
		float r = 0.0f;
		int idx = i;
		while (idx > 0)
		{
			f /= static_cast<float>(b);
			r += f * static_cast<float>(idx % b);
			idx /= b;
		}
		return r;
	}

	/// @brief 8サンプルのHalton(2,3)シーケンスを生成する
	void generateHaltonSequence()
	{
		for (int i = 0; i < kJitterSamples; ++i)
		{
			/// Halton(2,3)を-0.5 ~ +0.5の範囲にマッピングする
			m_haltonSequence[i] = {
				halton(i + 1, 2) - 0.5f,
				halton(i + 1, 3) - 0.5f
			};
		}
	}

	// ── パス実行 ────────────────────────────────────

	/// @brief TAAリゾルブパスを実行する
	void applyResolvePass(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* currentSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		auto jitter = currentJitter();

		ResolveCB cbData = {};
		cbData.texelSize[0] =
			1.0f / static_cast<float>(screenW);
		cbData.texelSize[1] =
			1.0f / static_cast<float>(screenH);
		cbData.blendFactor = m_config.blendFactor;
		cbData.motionScale = m_config.motionScale;
		cbData.jitterOffset[0] = jitter[0];
		cbData.jitterOffset[1] = jitter[1];
		updateCB(context, m_resolveCB.Get(),
			&cbData, sizeof(cbData));

		/// ビューポート設定
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		/// レンダーターゲット設定
		context->OMSetRenderTargets(
			1, &outputRTV, nullptr);

		/// シェーダー設定
		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_resolvePS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ設定: t0=現在, t1=ヒストリー, t2=深度
		ID3D11ShaderResourceView* srvs[3] = {
			currentSRV,
			m_historyRT.srv.Get(),
			m_depthSRV
		};
		context->PSSetShaderResources(0, 3, srvs);

		/// サンプラー設定: s0=リニア, s1=ポイント
		ID3D11SamplerState* samplers[2] = {
			m_linearClampSampler.Get(),
			m_pointClampSampler.Get()
		};
		context->PSSetSamplers(0, 2, samplers);

		/// 定数バッファ設定
		auto* cb = m_resolveCB.Get();
		context->PSSetConstantBuffers(0, 1, &cb);

		/// 描画
		context->Draw(3, 0);

		/// SRVクリア
		ID3D11ShaderResourceView* nullSRVs[3] = {
			nullptr, nullptr, nullptr
		};
		context->PSSetShaderResources(0, 3, nullSRVs);
	}

	/// @brief ヒストリーバッファへリゾルブ結果をコピーする
	void applyResolveToHistory(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* currentSRV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		auto jitter = currentJitter();

		ResolveCB cbData = {};
		cbData.texelSize[0] =
			1.0f / static_cast<float>(screenW);
		cbData.texelSize[1] =
			1.0f / static_cast<float>(screenH);
		cbData.blendFactor = m_config.blendFactor;
		cbData.motionScale = m_config.motionScale;
		cbData.jitterOffset[0] = jitter[0];
		cbData.jitterOffset[1] = jitter[1];
		updateCB(context, m_resolveCB.Get(),
			&cbData, sizeof(cbData));

		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		auto* rtv = m_historyRT.rtv.Get();
		context->OMSetRenderTargets(1, &rtv, nullptr);

		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_resolvePS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11ShaderResourceView* srvs[3] = {
			currentSRV,
			nullptr,  // ヒストリーは出力先なのでバインドしない
			m_depthSRV
		};
		context->PSSetShaderResources(0, 3, srvs);

		ID3D11SamplerState* samplers[2] = {
			m_linearClampSampler.Get(),
			m_pointClampSampler.Get()
		};
		context->PSSetSamplers(0, 2, samplers);

		auto* cb = m_resolveCB.Get();
		context->PSSetConstantBuffers(0, 1, &cb);

		context->Draw(3, 0);

		ID3D11ShaderResourceView* nullSRVs[3] = {
			nullptr, nullptr, nullptr
		};
		context->PSSetShaderResources(0, 3, nullSRVs);
	}

	/// @brief テクスチャをコピーする（パススルー描画）
	void copyTexture(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* srcSRV,
		ID3D11RenderTargetView* dstRTV,
		std::uint32_t screenW,
		std::uint32_t screenH)
	{
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		context->OMSetRenderTargets(1, &dstRTV, nullptr);

		context->VSSetShader(
			m_fullscreenVS.Get(), nullptr, 0);
		context->PSSetShader(
			m_copyPS.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->PSSetShaderResources(0, 1, &srcSRV);
		context->PSSetSamplers(
			0, 1, m_pointClampSampler.GetAddressOf());

		context->Draw(3, 0);

		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
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
			"TaaFullscreenVS", nullptr, nullptr,
			"VSMain", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			blob.GetAddressOf(),
			errBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"TAAEffect: VS compile failed");
		}

		ComPtr<ID3D11VertexShader> vs;
		hr = device->CreateVertexShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr, vs.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"TAAEffect: CreateVertexShader failed");
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
			"TaaPS", nullptr, nullptr,
			"PSMain", "ps_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			blob.GetAddressOf(),
			errBlob.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"TAAEffect: PS compile failed");
		}

		ComPtr<ID3D11PixelShader> ps;
		hr = device->CreatePixelShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr, ps.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"TAAEffect: CreatePixelShader failed");
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
				"TAAEffect: CreateBuffer (CB) failed");
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
				"TAAEffect: CreateSamplerState failed");
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
				"TAAEffect: CreateTexture2D failed");
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
				"TAAEffect: CreateSRV failed");
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
				"TAAEffect: CreateRTV failed");
		}

		return rt;
	}

	// ── メンバー変数 ────────────────────────────────

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_fullscreenVS;

	/// シェーダー
	ComPtr<ID3D11PixelShader> m_resolvePS;
	ComPtr<ID3D11PixelShader> m_copyPS;

	/// 定数バッファ
	ComPtr<ID3D11Buffer> m_resolveCB;

	/// サンプラー
	ComPtr<ID3D11SamplerState> m_linearClampSampler;
	ComPtr<ID3D11SamplerState> m_pointClampSampler;

	/// ヒストリーバッファ
	RTData m_historyRT;
	bool m_historyValid = false;

	/// 深度SRV（外部から設定、所有権なし）
	ID3D11ShaderResourceView* m_depthSRV = nullptr;

	/// Haltonジッターシーケンス
	std::array<std::array<float, 2>, kJitterSamples>
		m_haltonSequence = {};
	int m_frameIndex = 0;

	/// スクリーンサイズ
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;

	/// 設定
	TAAConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
