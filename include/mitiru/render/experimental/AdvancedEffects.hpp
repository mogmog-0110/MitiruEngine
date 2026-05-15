#pragma once

#ifndef MITIRU_ENABLE_EXPERIMENTAL_RENDER
#error "AdvancedEffects は experimental 段階のため、" \
       "明示的に MITIRU_ENABLE_EXPERIMENTAL_RENDER=1 を定義しないと使用不可。" \
       "詳細は docs/3D_RENDERING.md を参照"
#endif

/// @file AdvancedEffects.hpp
/// @brief SSR, Parallax, SSS, Tessellation, OIT, Compute Particles
/// @note This header is EXPERIMENTAL. APIs and HLSL implementations are incomplete
///       and may not produce correct results. Requires MITIRU_ENABLE_EXPERIMENTAL_RENDER.

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
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/PostProcess.hpp>

namespace mitiru::render
{

// ============================================================================
// 1. Screen-Space Reflections (SSR)
// ============================================================================

struct SSRConfig
{
	int maxSteps = 64;
	float stepSize = 0.05f;
	float thickness = 0.5f;
	float fadeEdge = 0.1f;
};

struct alignas(16) SSRCBData
{
	int maxSteps; float stepSize; float thickness; float fadeEdge;
};

constexpr std::string_view SSR_PS = R"hlsl(
Texture2D sceneColor : register(t0);
Texture2D sceneDepth : register(t1);
Texture2D sceneNormal : register(t2);
SamplerState samp : register(s0);
cbuffer SSRParams : register(b0) { int maxSteps; float stepSize; float thickness; float fadeEdge; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

float4 PSMain(PSIn i) : SV_TARGET {
	float4 col = sceneColor.Sample(samp, i.uv);
	float depth = sceneDepth.Sample(samp, i.uv).r;
	float3 N = sceneNormal.Sample(samp, i.uv).rgb * 2.0 - 1.0;
	if (depth >= 1.0) return col;

	float3 vp = float3(i.uv * 2.0 - 1.0, depth); vp.y = -vp.y;
	float3 rd = reflect(normalize(vp), normalize(N));
	float2 uv = i.uv; float rd_z = depth;

	for (int s = 0; s < maxSteps; ++s) {
		uv += rd.xy * stepSize; rd_z += rd.z * stepSize;
		if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) break;
		float sd = sceneDepth.Sample(samp, uv).r;
		float diff = rd_z - sd;
		if (diff > 0 && diff < thickness) {
			// Binary refinement
			float2 ru = uv; float rz = rd_z;
			for (int j = 0; j < 5; ++j) {
				float hs = stepSize * pow(0.5, float(j+1));
				if (rz - sceneDepth.Sample(samp, ru).r > 0)
					{ ru -= rd.xy * hs; rz -= rd.z * hs; }
				else { ru += rd.xy * hs; rz += rd.z * hs; }
			}
			float ef = 1.0 - smoothstep(0, fadeEdge, max(abs(ru.x-0.5)*2, abs(ru.y-0.5)*2));
			float sf = 1.0 - float(s) / float(maxSteps);
			float4 r = sceneColor.Sample(samp, ru);
			return float4(lerp(col.rgb, r.rgb, ef * sf * 0.5), col.a);
		}
	}
	return col;
}
)hlsl";

class SSRPass final : public PostProcessPass
{
public:
	explicit SSRPass(ID3D11Device* dev, const SSRConfig& cfg = {}) : m_cfg(cfg)
	{
		m_vs = compileFullscreenVS(dev);
		m_ps = compilePostProcessPS(dev, SSR_PS);
		m_cb = createConstantBuffer(dev, sizeof(SSRCBData));
		m_samp = createLinearClampSampler(dev);
	}

	void apply(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* in,
		ID3D11RenderTargetView* out, std::uint32_t w, std::uint32_t h) override
	{
		SSRCBData d{m_cfg.maxSteps, m_cfg.stepSize, m_cfg.thickness, m_cfg.fadeEdge};
		updateConstantBuffer(ctx, m_cb.Get(), &d, sizeof(d));
		drawFullscreenPass(ctx, m_vs.Get(), m_ps.Get(), in, out, m_samp.Get(), m_cb.Get(), w, h);
	}

	[[nodiscard]] std::string_view name() const noexcept override { return "SSR"; }
	void setConfig(const SSRConfig& c) noexcept { m_cfg = c; }
	[[nodiscard]] const SSRConfig& config() const noexcept { return m_cfg; }

private:
	SSRConfig m_cfg;
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11Buffer> m_cb;
	ComPtr<ID3D11SamplerState> m_samp;
};

// ============================================================================
// 2. Parallax Occlusion Mapping (standalone HLSL helper)
// ============================================================================

struct ParallaxConfig
{
	float heightScale = 0.05f;
	int minLayers = 8;
	int maxLayers = 32;
};

/// @brief Embed this function in any pixel shader that needs parallax mapping
constexpr std::string_view PARALLAX_HLSL_FUNCTION = R"hlsl(
float2 ParallaxOcclusionMapping(
	float2 uv, float3 viewDirTS, Texture2D heightMap,
	SamplerState samp, float heightScale, int minLayers, int maxLayers)
{
	float numLayers = lerp(float(maxLayers), float(minLayers), abs(dot(float3(0,0,1), viewDirTS)));
	float layerD = 1.0 / numLayers;
	float curD = 0.0;
	float2 dUV = viewDirTS.xy / viewDirTS.z * heightScale / numLayers;
	float2 curUV = uv;
	float curH = heightMap.SampleLevel(samp, curUV, 0).r;
	[loop] for (int i = 0; i < maxLayers; ++i) {
		if (curD >= curH) break;
		curUV -= dUV;
		curH = heightMap.SampleLevel(samp, curUV, 0).r;
		curD += layerD;
	}
	float2 prevUV = curUV + dUV;
	float after = curH - curD;
	float before = heightMap.SampleLevel(samp, prevUV, 0).r - curD + layerD;
	float w = (abs(after - before) > 1e-6) ? after / (after - before) : 0.5;
	return lerp(curUV, prevUV, w);
}
)hlsl";

// ============================================================================
// 3. Subsurface Scattering (SSS)
// ============================================================================

struct SSSConfig
{
	float scatterRadius = 0.01f;
	float scatterColor[3] = {1.0f, 0.4f, 0.25f};
	float thickness = 1.0f;
};

struct alignas(16) SSSCBData
{
	float scatterRadius;
	float scatterR, scatterG, scatterB;
	float thickness;
	float pad[3];
};

constexpr std::string_view SSS_PS = R"hlsl(
Texture2D sceneColor : register(t0);
Texture2D thicknessMap : register(t1);
SamplerState samp : register(s0);
cbuffer SSSParams : register(b0) {
	float scatterRadius; float scatterR; float scatterG; float scatterB;
	float matThickness; float3 sssPad;
};
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

float3 sssKernel(float off, float rad) {
	float r = abs(off);
	return float3(
		exp(-r*r / (2*rad*rad*1.0)) * scatterR,
		exp(-r*r / (2*rad*rad*0.4)) * scatterG,
		exp(-r*r / (2*rad*rad*0.15)) * scatterB);
}

float4 PSMain(PSIn i) : SV_TARGET {
	float thick = thicknessMap.Sample(samp, i.uv).r;
	float eRad = scatterRadius * thick * matThickness;
	float3 result = float3(0,0,0); float3 tw = float3(0,0,0);
	for (int s = -11; s <= 11; ++s) {
		float off = float(s) / 11.0;
		float3 w = sssKernel(off, 1.0);
		result += sceneColor.Sample(samp, i.uv + float2(off * eRad, 0)).rgb * w;
		tw += w;
	}
	return float4(result / max(tw, 0.001), 1.0);
}
)hlsl";

class SSSPass final : public PostProcessPass
{
public:
	explicit SSSPass(ID3D11Device* dev, const SSSConfig& cfg = {}) : m_cfg(cfg)
	{
		m_vs = compileFullscreenVS(dev);
		m_ps = compilePostProcessPS(dev, SSS_PS);
		m_cb = createConstantBuffer(dev, sizeof(SSSCBData));
		m_samp = createLinearClampSampler(dev);
	}

	void apply(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* in,
		ID3D11RenderTargetView* out, std::uint32_t w, std::uint32_t h) override
	{
		SSSCBData d{m_cfg.scatterRadius, m_cfg.scatterColor[0],
			m_cfg.scatterColor[1], m_cfg.scatterColor[2], m_cfg.thickness, {}};
		updateConstantBuffer(ctx, m_cb.Get(), &d, sizeof(d));
		drawFullscreenPass(ctx, m_vs.Get(), m_ps.Get(), in, out, m_samp.Get(), m_cb.Get(), w, h);
	}

	[[nodiscard]] std::string_view name() const noexcept override { return "SSS"; }
	void setConfig(const SSSConfig& c) noexcept { m_cfg = c; }
	[[nodiscard]] const SSSConfig& config() const noexcept { return m_cfg; }

private:
	SSSConfig m_cfg;
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11Buffer> m_cb;
	ComPtr<ID3D11SamplerState> m_samp;
};

// ============================================================================
// 4. Tessellation Shaders (Hull + Domain for displacement)
// ============================================================================

struct TessellationConfig
{
	float tessellationFactor = 8.0f;
	float displacementScale = 0.1f;
	std::string heightMapKey;
};

struct alignas(16) TessCBData { float tessFactor; float dispScale; float pad[2]; };

constexpr std::string_view TESSELLATION_HS = R"hlsl(
cbuffer TP : register(b0) { float tessFactor; float dispScale; float2 tp; };
struct VOut { float4 pos : SV_POSITION; float3 norm : NORMAL; float2 uv : TEXCOORD0; };
struct HOut { float4 pos : SV_POSITION; float3 norm : NORMAL; float2 uv : TEXCOORD0; };
struct HConst { float et[3] : SV_TessFactor; float it : SV_InsideTessFactor; };

HConst HSConst(InputPatch<VOut,3> p, uint pid : SV_PrimitiveID) {
	HConst o; o.et[0]=o.et[1]=o.et[2]=tessFactor; o.it=tessFactor; return o;
}

[domain("tri")][partitioning("fractional_odd")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("HSConst")]
HOut HSMain(InputPatch<VOut,3> p, uint id : SV_OutputControlPointID) {
	HOut o; o.pos=p[id].pos; o.norm=p[id].norm; o.uv=p[id].uv; return o;
}
)hlsl";

constexpr std::string_view TESSELLATION_DS = R"hlsl(
cbuffer TP : register(b0) { float tessFactor; float dispScale; float2 tp; };
cbuffer MB : register(b1) { float4x4 wvp; };
Texture2D hmap : register(t0);
SamplerState samp : register(s0);
struct HOut { float4 pos : SV_POSITION; float3 norm : NORMAL; float2 uv : TEXCOORD0; };
struct HConst { float et[3] : SV_TessFactor; float it : SV_InsideTessFactor; };
struct DOut { float4 pos : SV_POSITION; float3 norm : NORMAL; float2 uv : TEXCOORD0; };

[domain("tri")]
DOut DSMain(HConst hc, float3 b : SV_DomainLocation, const OutputPatch<HOut,3> p) {
	DOut o;
	float4 pos = p[0].pos*b.x + p[1].pos*b.y + p[2].pos*b.z;
	float3 n = normalize(p[0].norm*b.x + p[1].norm*b.y + p[2].norm*b.z);
	float2 uv = p[0].uv*b.x + p[1].uv*b.y + p[2].uv*b.z;
	pos.xyz += n * hmap.SampleLevel(samp, uv, 0).r * dispScale;
	o.pos = mul(pos, wvp); o.norm = n; o.uv = uv;
	return o;
}
)hlsl";

class TessellationShaders
{
public:
	explicit TessellationShaders(ID3D11Device* dev, const TessellationConfig& cfg = {})
		: m_cfg(cfg)
	{
		m_hs = compileShader<ID3D11HullShader>(dev, TESSELLATION_HS, "HSMain", "hs_5_0");
		m_ds = compileShader<ID3D11DomainShader>(dev, TESSELLATION_DS, "DSMain", "ds_5_0");
		m_cb = createConstantBuffer(dev, sizeof(TessCBData));
	}

	void bind(ID3D11DeviceContext* ctx) const
	{
		TessCBData d{m_cfg.tessellationFactor, m_cfg.displacementScale, {}};
		updateConstantBuffer(ctx, m_cb.Get(), &d, sizeof(d));
		ctx->HSSetShader(m_hs.Get(), nullptr, 0);
		ctx->DSSetShader(m_ds.Get(), nullptr, 0);
		ctx->HSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
		ctx->DSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	}

	static void unbind(ID3D11DeviceContext* ctx)
	{
		ctx->HSSetShader(nullptr, nullptr, 0);
		ctx->DSSetShader(nullptr, nullptr, 0);
	}

	void setConfig(const TessellationConfig& c) noexcept { m_cfg = c; }
	[[nodiscard]] const TessellationConfig& config() const noexcept { return m_cfg; }

private:
	template <typename ShaderT>
	ComPtr<ShaderT> compileShader(
		ID3D11Device* dev, std::string_view src,
		const char* entry, const char* target)
	{
		ComPtr<ID3DBlob> blob, err;
		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		HRESULT hr = D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr,
			entry, target, flags, 0, blob.GetAddressOf(), err.GetAddressOf());
		if (FAILED(hr))
		{
			std::string msg = "Tessellation compile failed";
			if (err) { msg += ": "; msg += static_cast<const char*>(err->GetBufferPointer()); }
			throw std::runtime_error(msg);
		}
		ComPtr<ShaderT> shader;
		hr = createShaderFromBlob(dev, blob.Get(), shader);
		if (FAILED(hr)) throw std::runtime_error("Tessellation: CreateShader failed");
		return shader;
	}

	static HRESULT createShaderFromBlob(
		ID3D11Device* dev, ID3DBlob* blob, ComPtr<ID3D11HullShader>& s)
	{
		return dev->CreateHullShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s.GetAddressOf());
	}

	static HRESULT createShaderFromBlob(
		ID3D11Device* dev, ID3DBlob* blob, ComPtr<ID3D11DomainShader>& s)
	{
		return dev->CreateDomainShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s.GetAddressOf());
	}

	TessellationConfig m_cfg;
	ComPtr<ID3D11HullShader> m_hs;
	ComPtr<ID3D11DomainShader> m_ds;
	ComPtr<ID3D11Buffer> m_cb;
};

// ============================================================================
// 5. Order-Independent Transparency (Weighted Blended OIT)
// ============================================================================

struct OITConfig { bool enabled = true; };

constexpr std::string_view OIT_ACCUMULATE_PS = R"hlsl(
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct PSOut { float4 accum : SV_TARGET0; float reveal : SV_TARGET1; };
PSOut PSMain(PSIn i) {
	PSOut o; float4 c = i.col;
	float w = clamp(pow(min(1.0, c.a*10.0)+0.01, 3.0) * 1e8
		* pow(1e-5 + abs(i.pos.z), -3.0), 1e-2, 3e3);
	o.accum = float4(c.rgb * c.a, c.a) * w;
	o.reveal = c.a;
	return o;
}
)hlsl";

constexpr std::string_view OIT_COMPOSITE_PS = R"hlsl(
Texture2D accumTex : register(t0);
Texture2D revealTex : register(t1);
SamplerState samp : register(s0);
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
float4 PSMain(PSIn i) : SV_TARGET {
	float4 ac = accumTex.Sample(samp, i.uv);
	float rv = revealTex.Sample(samp, i.uv).r;
	if (ac.a < 1e-5) discard;
	float3 c = ac.rgb / max(ac.a, 1e-5);
	float a = 1.0 - rv;
	return float4(c * a, a);
}
)hlsl";

class OITPass
{
public:
	explicit OITPass(ID3D11Device* dev, const OITConfig& cfg = {}) : m_cfg(cfg), m_dev(dev)
	{
		m_vs = compileFullscreenVS(dev);
		m_compositePS = compilePostProcessPS(dev, OIT_COMPOSITE_PS);
		m_samp = createLinearClampSampler(dev);
		createBlendStates(dev);
	}

	void resize(std::uint32_t w, std::uint32_t h)
	{
		m_accumRT = createRenderTarget(m_dev.Get(), w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_revealRT = createRenderTarget(m_dev.Get(), w, h, DXGI_FORMAT_R16_FLOAT);
		m_w = w; m_h = h;
	}

	void beginTransparentPass(ID3D11DeviceContext* ctx) const
	{
		const float clrA[4] = {0,0,0,0}, clrR[4] = {1,1,1,1};
		ctx->ClearRenderTargetView(m_accumRT.rtv.Get(), clrA);
		ctx->ClearRenderTargetView(m_revealRT.rtv.Get(), clrR);
		ID3D11RenderTargetView* rtvs[2] = {m_accumRT.rtv.Get(), m_revealRT.rtv.Get()};
		ctx->OMSetRenderTargets(2, rtvs, nullptr);
		ctx->OMSetBlendState(m_oitBlend.Get(), nullptr, 0xFFFFFFFF);
	}

	static void endTransparentPass(ID3D11DeviceContext* ctx)
	{
		ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	}

	void composite(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* out) const
	{
		D3D11_VIEWPORT vp = {}; vp.Width = float(m_w); vp.Height = float(m_h); vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);
		ctx->OMSetRenderTargets(1, &out, nullptr);
		ctx->OMSetBlendState(m_compBlend.Get(), nullptr, 0xFFFFFFFF);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->IASetInputLayout(nullptr);
		ctx->VSSetShader(m_vs.Get(), nullptr, 0);
		ctx->PSSetShader(m_compositePS.Get(), nullptr, 0);
		ID3D11ShaderResourceView* srvs[2] = {m_accumRT.srv.Get(), m_revealRT.srv.Get()};
		ctx->PSSetShaderResources(0, 2, srvs);
		ctx->PSSetSamplers(0, 1, m_samp.GetAddressOf());
		ctx->Draw(3, 0);
		ID3D11ShaderResourceView* null2[2] = {};
		ctx->PSSetShaderResources(0, 2, null2);
		ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	}

	void setConfig(const OITConfig& c) noexcept { m_cfg = c; }
	[[nodiscard]] const OITConfig& config() const noexcept { return m_cfg; }

private:
	void createBlendStates(ID3D11Device* dev)
	{
		// Accumulation blend: additive on RT0, multiplicative on RT1
		D3D11_BLEND_DESC bd = {};
		bd.IndependentBlendEnable = TRUE;
		auto& rt0 = bd.RenderTarget[0];
		rt0.BlendEnable = TRUE;
		rt0.SrcBlend = rt0.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt0.DestBlend = rt0.DestBlendAlpha = D3D11_BLEND_ONE;
		rt0.BlendOp = rt0.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		auto& rt1 = bd.RenderTarget[1];
		rt1.BlendEnable = TRUE;
		rt1.SrcBlend = rt1.SrcBlendAlpha = D3D11_BLEND_ZERO;
		rt1.DestBlend = D3D11_BLEND_INV_SRC_COLOR; rt1.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		rt1.BlendOp = rt1.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt1.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(dev->CreateBlendState(&bd, m_oitBlend.GetAddressOf())))
			throw std::runtime_error("OIT: CreateBlendState (accum) failed");

		// Composite blend: standard alpha blend
		D3D11_BLEND_DESC cd = {};
		auto& c0 = cd.RenderTarget[0];
		c0.BlendEnable = TRUE;
		c0.SrcBlend = D3D11_BLEND_SRC_ALPHA; c0.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		c0.BlendOp = D3D11_BLEND_OP_ADD;
		c0.SrcBlendAlpha = D3D11_BLEND_ONE; c0.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		c0.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		c0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(dev->CreateBlendState(&cd, m_compBlend.GetAddressOf())))
			throw std::runtime_error("OIT: CreateBlendState (composite) failed");
	}

	OITConfig m_cfg;
	ComPtr<ID3D11Device> m_dev;
	PostProcessRT m_accumRT, m_revealRT;
	std::uint32_t m_w = 0, m_h = 0;
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader> m_compositePS;
	ComPtr<ID3D11SamplerState> m_samp;
	ComPtr<ID3D11BlendState> m_oitBlend, m_compBlend;
};

// ============================================================================
// 6. Compute Shader Particles
// ============================================================================

struct GPUParticleConfig
{
	std::uint32_t maxParticles = 100000;
	float gravity = -9.81f;
	float drag = 0.98f;
	float emitRate = 1000.0f;
};

struct GPUParticle
{
	float pos[3]; float life;
	float vel[3]; float maxLife;
	float color[4];
	float size; float pad[3];
};

struct alignas(16) ParticleUpdateCB
{
	float dt; float gravity; float drag; float emitRate;
	std::uint32_t maxParticles; std::uint32_t frame; float pad[2];
};

constexpr std::string_view PARTICLE_CS = R"hlsl(
struct P { float3 pos; float life; float3 vel; float maxLife; float4 color; float size; float3 pad; };
cbuffer U : register(b0) {
	float dt; float gravity; float drag; float emitRate;
	uint maxP; uint frame; float2 csPad;
};
RWStructuredBuffer<P> particles : register(u0);

float hash(uint s) {
	s = s*747796405u+2891336453u;
	s = ((s>>16u)^s)*0x45d9f3bu;
	s = ((s>>16u)^s)*0x45d9f3bu;
	return float((s>>16u)^s) / 4294967295.0;
}

[numthreads(256,1,1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
	if (tid.x >= maxP) return;
	P p = particles[tid.x];
	if (p.life > 0) {
		p.vel.y += gravity * dt;
		p.vel *= drag;
		p.pos += p.vel * dt;
		p.life -= dt;
		float t = p.life / max(p.maxLife, 0.001);
		p.color.a = t;
		p.size = lerp(0.01, p.size, t);
	} else {
		uint s = tid.x * 1973u + frame * 6571u;
		p.pos = float3((hash(s)-0.5)*2, 0, (hash(s+1)-0.5)*2);
		p.vel = float3((hash(s+2)-0.5)*4, hash(s+3)*8+2, (hash(s+4)-0.5)*4);
		p.life = hash(s+5)*3+1; p.maxLife = p.life;
		p.color = float4(hash(s+6)*0.5+0.5, hash(s+7)*0.3+0.2, hash(s+8)*0.2+0.1, 1);
		p.size = hash(s+9)*0.05+0.02;
	}
	particles[tid.x] = p;
}
)hlsl";

class ComputeShaderParticles
{
public:
	explicit ComputeShaderParticles(ID3D11Device* dev, const GPUParticleConfig& cfg = {})
		: m_cfg(cfg), m_dev(dev)
	{
		compileCS(dev);
		createBuffers(dev);
		m_cb = createConstantBuffer(dev, sizeof(ParticleUpdateCB));
	}

	void update(ID3D11DeviceContext* ctx, float deltaTime)
	{
		ParticleUpdateCB d{deltaTime, m_cfg.gravity, m_cfg.drag,
			m_cfg.emitRate, m_cfg.maxParticles, m_frame++, {}};
		updateConstantBuffer(ctx, m_cb.Get(), &d, sizeof(d));
		ctx->CSSetShader(m_cs.Get(), nullptr, 0);
		ctx->CSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
		ctx->CSSetUnorderedAccessViews(0, 1, m_uav.GetAddressOf(), nullptr);
		ctx->Dispatch((m_cfg.maxParticles + 255) / 256, 1, 1);
		ID3D11UnorderedAccessView* null1 = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &null1, nullptr);
		ctx->CSSetShader(nullptr, nullptr, 0);
	}

	[[nodiscard]] ID3D11ShaderResourceView* particleSRV() const noexcept { return m_srv.Get(); }
	[[nodiscard]] std::uint32_t particleCount() const noexcept { return m_cfg.maxParticles; }
	/// @brief maxParticles は初期化時の値を維持する
	void setConfig(const GPUParticleConfig& c) noexcept
	{
		const auto originalMax = m_cfg.maxParticles;
		m_cfg = c;
		m_cfg.maxParticles = originalMax;
	}
	[[nodiscard]] const GPUParticleConfig& config() const noexcept { return m_cfg; }

private:
	void compileCS(ID3D11Device* dev)
	{
		ComPtr<ID3DBlob> blob, err;
		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		HRESULT hr = D3DCompile(PARTICLE_CS.data(), PARTICLE_CS.size(), nullptr,
			nullptr, nullptr, "CSMain", "cs_5_0", flags, 0,
			blob.GetAddressOf(), err.GetAddressOf());
		if (FAILED(hr)) {
			std::string msg = "ComputeParticles: CS compile failed";
			if (err) { msg += ": "; msg += static_cast<const char*>(err->GetBufferPointer()); }
			throw std::runtime_error(msg);
		}
		hr = dev->CreateComputeShader(blob->GetBufferPointer(),
			blob->GetBufferSize(), nullptr, m_cs.GetAddressOf());
		if (FAILED(hr)) throw std::runtime_error("ComputeParticles: CreateCS failed");
	}

	void createBuffers(ID3D11Device* dev)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = static_cast<UINT>(sizeof(GPUParticle) * m_cfg.maxParticles);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(GPUParticle);
		std::vector<GPUParticle> init(m_cfg.maxParticles, GPUParticle{});
		D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = init.data();
		if (FAILED(dev->CreateBuffer(&bd, &sd, m_buf.GetAddressOf())))
			throw std::runtime_error("ComputeParticles: CreateBuffer failed");

		D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
		ud.Format = DXGI_FORMAT_UNKNOWN;
		ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		ud.Buffer.NumElements = m_cfg.maxParticles;
		if (FAILED(dev->CreateUnorderedAccessView(m_buf.Get(), &ud, m_uav.GetAddressOf())))
			throw std::runtime_error("ComputeParticles: CreateUAV failed");

		D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
		svd.Format = DXGI_FORMAT_UNKNOWN;
		svd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		svd.Buffer.NumElements = m_cfg.maxParticles;
		if (FAILED(dev->CreateShaderResourceView(m_buf.Get(), &svd, m_srv.GetAddressOf())))
			throw std::runtime_error("ComputeParticles: CreateSRV failed");
	}

	GPUParticleConfig m_cfg;
	ComPtr<ID3D11Device> m_dev;
	std::uint32_t m_frame = 0;
	ComPtr<ID3D11ComputeShader> m_cs;
	ComPtr<ID3D11Buffer> m_buf;
	ComPtr<ID3D11UnorderedAccessView> m_uav;
	ComPtr<ID3D11ShaderResourceView> m_srv;
	ComPtr<ID3D11Buffer> m_cb;
};

} // namespace mitiru::render

#endif // _WIN32
