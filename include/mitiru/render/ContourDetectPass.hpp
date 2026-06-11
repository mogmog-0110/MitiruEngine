#pragma once

/// @file ContourDetectPass.hpp
/// @brief CPU `ContourDetect`（深度段差 + 法線折れ）の GPU fullscreen-PS 移植（NPR #20 b）
/// @details `GeometryPass3D` が出す depth(R32_FLOAT) / world-normal(RGBA16F) RT を入力に、
///          各画素で 4 近傍（左右上下）の深度差・法線差の最大をしきい値で正規化し、その大きい方を
///          raw 輪郭強度 [0,1] として R16F RT に書く。アルゴリズムは CPU `contourAt`
///          （`ContourDetect.hpp`）と等価で、しきい値の意味も同一（depthThreshold=0.02 /
///          normalThreshold=0.35 既定）。出力は `TemporalContourPass` の rawContour 入力になり、
///          GeometryPass3D → ContourDetectPass → MotionVectorPass → TemporalContourPass で
///          GPU 完結の輪郭線パイプラインを構成する。
///
/// 近傍の境界クランプは Clamp サンプラに任せる（CPU の `clampCoord` 相当）。背景画素は
/// normal=(0,1,0) / depth=1（CPU `GBufferPixel` 既定値と同一規約）のため、シルエット
/// （物体 vs 背景）は主に深度差で立ち、背景同士では輪郭が立たない。
/// 前面の線細化（front-side thinning）は CPU 版にも無いため未実装（パリティ優先、将来課題）。
/// @note GPU バックエンド有り環境での smoke 前提（headless では shader 文字列 + API の compile-check のみ）。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::render
{

/// @brief 輪郭抽出シェーダ（CPU `contourAt` と等価）
constexpr std::string_view CONTOUR_DETECT_PS = R"hlsl(
Texture2D<float>  Depth  : register(t0);   // [0,1] デバイス深度
Texture2D<float4> Normal : register(t1);   // ワールド法線（生値）
SamplerState PointClamp  : register(s0);

cbuffer CDParams : register(b0)
{
    float DepthThreshold;
    float NormalThreshold;
    float2 TexelSize;        // 1/幅, 1/高さ
};

struct PSInput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

float PSMain(PSInput i) : SV_TARGET
{
    float  cDepth = Depth.SampleLevel(PointClamp, i.uv, 0);
    float3 cN     = Normal.SampleLevel(PointClamp, i.uv, 0).xyz;

    const float2 off[4] = {
        float2(-TexelSize.x, 0), float2(TexelSize.x, 0),
        float2(0, -TexelSize.y), float2(0, TexelSize.y)
    };

    float maxDepthDiff = 0.0;
    float maxNormalDiff = 0.0;
    [unroll] for (int k = 0; k < 4; ++k)
    {
        float2 nuv = i.uv + off[k];
        float  nD  = Depth.SampleLevel(PointClamp, nuv, 0);
        float3 nN  = Normal.SampleLevel(PointClamp, nuv, 0).xyz;
        maxDepthDiff  = max(maxDepthDiff, abs(cDepth - nD));
        maxNormalDiff = max(maxNormalDiff, 1.0 - dot(cN, nN));
    }

    float depthE  = (DepthThreshold  > 0.0) ? min(1.0, maxDepthDiff  / DepthThreshold)  : 0.0;
    float normalE = (NormalThreshold > 0.0) ? min(1.0, maxNormalDiff / NormalThreshold) : 0.0;
    return max(depthE, normalE);
}
)hlsl";

constexpr std::string_view CONTOUR_DETECT_VS = R"hlsl(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);             // フルスクリーン三角形
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)hlsl";

/// @brief 輪郭抽出パラメータ（CPU `ContourParams` と対応）
struct ContourDetectPassParams
{
	float depthThreshold = 0.02f;
	float normalThreshold = 0.35f;
};

class ContourDetectPass
{
public:
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	void init(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
	{
		if (device == nullptr) { throw std::runtime_error("ContourDetectPass: null device"); }
		m_device = device; m_width = width; m_height = height;
		m_vs = compileVS(device, CONTOUR_DETECT_VS);
		m_ps = compilePS(device, CONTOUR_DETECT_PS);
		m_cb = createCB(device, sizeof(CbCD));
		m_outRT = createRT(device, width, height, DXGI_FORMAT_R16_FLOAT);
		m_point = createSampler(device);
	}

	/// @brief 入力 RT を設定する（`GeometryPass3D::depthSRV()` / `normalSRV()`）。所有しない。
	void setInputs(ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* normal) noexcept
	{
		m_depth = depth; m_normal = normal;
	}

	void setParams(const ContourDetectPassParams& p) noexcept { m_params = p; }

	/// @brief 輪郭抽出を実行し、結果を出力 RT（`contourSRV()`）へ書く。
	void apply(ID3D11DeviceContext* ctx)
	{
		CbCD cb{};
		cb.depthThreshold = m_params.depthThreshold;
		cb.normalThreshold = m_params.normalThreshold;
		cb.texelX = 1.0f / static_cast<float>(m_width);
		cb.texelY = 1.0f / static_cast<float>(m_height);

		D3D11_MAPPED_SUBRESOURCE m{};
		if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			std::memcpy(m.pData, &cb, sizeof(cb));
			ctx->Unmap(m_cb.Get(), 0);
		}

		ID3D11RenderTargetView* rtv = m_outRT.rtv.Get();
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT vp{}; vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height); vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		ID3D11ShaderResourceView* srvs[2] = {m_depth, m_normal};
		ctx->PSSetShaderResources(0, 2, srvs);
		ID3D11SamplerState* samp = m_point.Get();
		ctx->PSSetSamplers(0, 1, &samp);
		ID3D11Buffer* cbuf = m_cb.Get();
		ctx->PSSetConstantBuffers(0, 1, &cbuf);

		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_vs.Get(), nullptr, 0);
		ctx->PSSetShader(m_ps.Get(), nullptr, 0);
		ctx->Draw(3, 0);

		ID3D11ShaderResourceView* nul[2] = {nullptr, nullptr};
		ctx->PSSetShaderResources(0, 2, nul);
		ID3D11RenderTargetView* nrt = nullptr;
		ctx->OMSetRenderTargets(1, &nrt, nullptr);
	}

	/// @brief raw 輪郭 RT の SRV（R16F、`TemporalContourPass` の rawContour 入力に渡す）。
	[[nodiscard]] ID3D11ShaderResourceView* contourSRV() const noexcept { return m_outRT.srv.Get(); }
	/// @brief readback テスト用の生テクスチャ。
	[[nodiscard]] ID3D11Texture2D* contourTexture() const noexcept { return m_outRT.texture.Get(); }

private:
	struct alignas(16) CbCD
	{
		float depthThreshold, normalThreshold, texelX, texelY;
	};
	struct RTData
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
	};

	[[nodiscard]] static ComPtr<ID3D11VertexShader> compileVS(ID3D11Device* d, std::string_view src)
	{
		ComPtr<ID3DBlob> b, e;
		if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, b.GetAddressOf(), e.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: VS compile failed");
		ComPtr<ID3D11VertexShader> vs;
		if (FAILED(d->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, vs.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreateVS failed");
		return vs;
	}

	[[nodiscard]] static ComPtr<ID3D11PixelShader> compilePS(ID3D11Device* d, std::string_view src)
	{
		ComPtr<ID3DBlob> b, e;
		if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, b.GetAddressOf(), e.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: PS compile failed");
		ComPtr<ID3D11PixelShader> ps;
		if (FAILED(d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, ps.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreatePS failed");
		return ps;
	}

	[[nodiscard]] static ComPtr<ID3D11Buffer> createCB(ID3D11Device* d, std::uint32_t bytes)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ComPtr<ID3D11Buffer> cb;
		if (FAILED(d->CreateBuffer(&desc, nullptr, cb.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreateBuffer(CB) failed");
		return cb;
	}

	[[nodiscard]] static RTData createRT(ID3D11Device* d, std::uint32_t w, std::uint32_t h, DXGI_FORMAT fmt)
	{
		RTData rt;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
		desc.Format = fmt; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		if (FAILED(d->CreateTexture2D(&desc, nullptr, rt.texture.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreateTexture2D failed");
		if (FAILED(d->CreateShaderResourceView(rt.texture.Get(), nullptr, rt.srv.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreateSRV failed");
		if (FAILED(d->CreateRenderTargetView(rt.texture.Get(), nullptr, rt.rtv.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreateRTV failed");
		return rt;
	}

	[[nodiscard]] static ComPtr<ID3D11SamplerState> createSampler(ID3D11Device* d)
	{
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> s;
		if (FAILED(d->CreateSamplerState(&sd, s.GetAddressOf())))
			throw std::runtime_error("ContourDetectPass: CreateSamplerState failed");
		return s;
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11Buffer> m_cb;
	RTData m_outRT;
	ComPtr<ID3D11SamplerState> m_point;
	ID3D11ShaderResourceView* m_depth = nullptr;
	ID3D11ShaderResourceView* m_normal = nullptr;
	ContourDetectPassParams m_params;
	std::uint32_t m_width = 0, m_height = 0;
};

} // namespace mitiru::render

#endif // _WIN32
