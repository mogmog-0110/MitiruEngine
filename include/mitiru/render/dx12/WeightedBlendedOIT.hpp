#pragma once

/// @file WeightedBlendedOIT.hpp
/// @brief DX12 用 Weighted-Blended Order-Independent Transparency (McGuire & Bavoil 2013)。
/// @details 半透明をソートせず正しく合成する。透明物体を 2 枚の RT に蓄積する:
///          - accum (RGBA16F): Σ (premultiplied color × weight)  ── 加算ブレンド
///          - reveal (R16F):   Π (1 - alpha)                      ── 乗算ブレンド
///          深度に応じた weight を掛けるので、描画順に依存せず近似合成できる。
///          最後に accum/reveal をフルスクリーンで composite して背景へ重ねる。
///
/// 使い方 (呼び出し側が透明ジオメトリの VS と PSO を用意する):
///   1. 透明 PSO を作る — PS = `weightPsHlsl()`、BlendState = `fillAccumulateBlend()`、
///      RTV0=accumFormat() / RTV1=revealFormat()、深度は読み取り専用 (DepthWrite=ZERO)。
///   2. oit.beginAccumulate(cl, dsv) → 透明メッシュを上記 PSO で描画 → oit.composite(cl, outRtv)。
///
/// MSAA には依存しない (single-sample 前提)。MSAA 経路は composite 前に resolve すること。

#ifdef _WIN32

#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace mitiru::render::dx12
{

/// @brief WBOIT の accum/reveal RT と composite パスを管理する。
class WeightedBlendedOIT
{
public:
	static constexpr DXGI_FORMAT kAccumFormat  = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static constexpr DXGI_FORMAT kRevealFormat = DXGI_FORMAT_R16_FLOAT;

	[[nodiscard]] static constexpr DXGI_FORMAT accumFormat()  noexcept { return kAccumFormat; }
	[[nodiscard]] static constexpr DXGI_FORMAT revealFormat() noexcept { return kRevealFormat; }

	/// @brief 透明ジオメトリ PSO に焼く PS。VS は {SV_POSITION, COLOR0(rgba)} を渡すこと。
	/// @details SV_TARGET0=accum へ premult-color×weight、SV_TARGET1=reveal へ alpha を書く。
	///          weight は McGuire eq.9 系: alpha が高く・カメラに近いほど重く効く。
	[[nodiscard]] static std::string_view weightPsHlsl() noexcept
	{
		return R"hlsl(
struct PSIn  { float4 pos : SV_POSITION; float4 col : COLOR0; };
struct PSOut { float4 accum : SV_TARGET0; float reveal : SV_TARGET1; };
PSOut PSMain(PSIn i)
{
    PSOut o;
    float4 c = i.col;                  // 直線 alpha のカラー (rgb, a)
    float z = i.pos.z;                 // NDC 深度 [0,1] (近 0 / 遠 1)
    // 深度に応じた重み: 近いほど・不透明なほど大きく (McGuire & Bavoil 2013)。
    float w = clamp(pow(min(1.0, c.a * 10.0) + 0.01, 3.0)
                    * 1e3 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
    o.accum  = float4(c.rgb * c.a, c.a) * w;   // premultiplied color × weight
    o.reveal = c.a;                            // ブレンドで Π(1-a) になる (下記)
    return o;
}
)hlsl";
	}

	/// @brief 透明ジオメトリ PSO の BlendState を WBOIT 用に埋める。
	/// @details RT0(accum)=加算 ONE/ONE、RT1(reveal)=乗算 dst*(1-src)。IndependentBlend。
	static void fillAccumulateBlend(D3D12_BLEND_DESC& bd) noexcept
	{
		bd = {};
		bd.IndependentBlendEnable = TRUE;
		auto& a = bd.RenderTarget[0];   // accum: 加算
		a.BlendEnable = TRUE;
		a.SrcBlend = a.SrcBlendAlpha = D3D12_BLEND_ONE;
		a.DestBlend = a.DestBlendAlpha = D3D12_BLEND_ONE;
		a.BlendOp = a.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		a.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		auto& r = bd.RenderTarget[1];   // reveal: dst = dst*(1-src)
		r.BlendEnable = TRUE;
		r.SrcBlend = r.SrcBlendAlpha = D3D12_BLEND_ZERO;
		r.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
		r.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		r.BlendOp = r.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		r.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}

	/// @brief accum/reveal RT + composite PSO を生成する。
	/// @param outFormat    composite 先 RTV の形式 (例: HDR の R16G16B16A16_FLOAT)。
	/// @param sampleCount  >1 で MSAA。accum/reveal を MSAA で蓄積し、composite で
	///                     single-sample へ resolve してから out へ重ねる。
	void initialize(ID3D12Device* device, UINT width, UINT height,
	                DXGI_FORMAT outFormat, UINT sampleCount = 1)
	{
		m_device = device;
		m_width  = width;
		m_height = height;
		m_sampleCount = sampleCount;
		createTargets(width, height);
		createCompositePso(outFormat);
		m_initialized = true;
	}

	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	/// @brief accum=0 / reveal=1 にクリアし、accum+reveal を RT に、dsv を読み取り専用深度に bind。
	/// @details この後、呼び出し側が透明 PSO で透明メッシュを描画する。
	///          dsv=nullptr で深度なし (不透明が無い純粋な合成テスト等)。
	void beginAccumulate(ID3D12GraphicsCommandList* cl,
	                     const D3D12_CPU_DESCRIPTOR_HANDLE* dsv = nullptr)
	{
		transition(cl, m_accum.Get(), m_accumState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_accumState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		transition(cl, m_reveal.Get(), m_revealState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_revealState = D3D12_RESOURCE_STATE_RENDER_TARGET;

		const auto rtvStart = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		const UINT rtvInc = m_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE accumRtv = rtvStart;
		D3D12_CPU_DESCRIPTOR_HANDLE revealRtv = rtvStart;
		revealRtv.ptr += rtvInc;

		const float clrAccum[4]  = {0, 0, 0, 0};
		const float clrReveal[4] = {1, 1, 1, 1};   // Π(1-a) の初期値 = 1
		cl->ClearRenderTargetView(accumRtv,  clrAccum,  0, nullptr);
		cl->ClearRenderTargetView(revealRtv, clrReveal, 0, nullptr);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {accumRtv, revealRtv};
		cl->OMSetRenderTargets(2, rtvs, FALSE, dsv);
	}

	/// @brief accum/reveal を composite して outRtv へ over ブレンドする (背景の上に重なる)。
	/// @details outRtv は呼び出し側で RENDER_TARGET 状態にしておくこと。
	void composite(ID3D12GraphicsCommandList* cl, D3D12_CPU_DESCRIPTOR_HANDLE outRtv)
	{
		constexpr auto kPSR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		if (m_sampleCount > 1)
		{
			// MSAA: accum/reveal を single-sample へ resolve してから sample する。
			transition(cl, m_accum.Get(), m_accumState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
			m_accumState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			transition(cl, m_reveal.Get(), m_revealState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
			m_revealState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			transition(cl, m_accumResolved.Get(), m_accumResolvedState, D3D12_RESOURCE_STATE_RESOLVE_DEST);
			m_accumResolvedState = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			transition(cl, m_revealResolved.Get(), m_revealResolvedState, D3D12_RESOURCE_STATE_RESOLVE_DEST);
			m_revealResolvedState = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			cl->ResolveSubresource(m_accumResolved.Get(), 0, m_accum.Get(), 0, kAccumFormat);
			cl->ResolveSubresource(m_revealResolved.Get(), 0, m_reveal.Get(), 0, kRevealFormat);
			transition(cl, m_accumResolved.Get(), m_accumResolvedState, kPSR);
			m_accumResolvedState = kPSR;
			transition(cl, m_revealResolved.Get(), m_revealResolvedState, kPSR);
			m_revealResolvedState = kPSR;
		}
		else
		{
			transition(cl, m_accum.Get(), m_accumState, kPSR);
			m_accumState = kPSR;
			transition(cl, m_reveal.Get(), m_revealState, kPSR);
			m_revealState = kPSR;
		}

		cl->OMSetRenderTargets(1, &outRtv, FALSE, nullptr);

		D3D12_VIEWPORT vp{};
		vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height);
		vp.MaxDepth = 1.0f;
		cl->RSSetViewports(1, &vp);
		D3D12_RECT sc{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
		cl->RSSetScissorRects(1, &sc);

		cl->SetGraphicsRootSignature(m_compRootSig.Get());
		cl->SetPipelineState(m_compPso.Get());
		ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
		cl->SetDescriptorHeaps(1, heaps);
		cl->SetGraphicsRootDescriptorTable(
			0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cl->IASetVertexBuffers(0, 0, nullptr);
		cl->DrawInstanced(3, 1, 0, 0);   // フルスクリーン三角形 (SV_VertexID)
	}

	[[nodiscard]] ID3D12Resource* accumResource()  const noexcept { return m_accum.Get(); }
	[[nodiscard]] ID3D12Resource* revealResource() const noexcept { return m_reveal.Get(); }

private:
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	void createTargets(UINT w, UINT h)
	{
		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		const bool msaa = m_sampleCount > 1;

		auto makeTex = [&](DXGI_FORMAT fmt, const float clr[4], UINT samples,
		                   D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState,
		                   ComPtr<ID3D12Resource>& out)
		{
			D3D12_RESOURCE_DESC rd{};
			rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width            = w;
			rd.Height           = h;
			rd.DepthOrArraySize = 1;
			rd.MipLevels        = 1;
			rd.Format           = fmt;
			rd.SampleDesc.Count = samples;
			rd.Flags            = flags;
			D3D12_CLEAR_VALUE cv{};
			cv.Format = fmt;
			if (clr) { cv.Color[0]=clr[0]; cv.Color[1]=clr[1]; cv.Color[2]=clr[2]; cv.Color[3]=clr[3]; }
			const bool wantClear = (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
			if (FAILED(m_device->CreateCommittedResource(
					&hp, D3D12_HEAP_FLAG_NONE, &rd, initState,
					wantClear ? &cv : nullptr, IID_PPV_ARGS(out.GetAddressOf()))))
			{
				throw std::runtime_error("WBOIT: CreateCommittedResource (RT) failed");
			}
		};
		const float clrA[4] = {0, 0, 0, 0};
		const float clrR[4] = {1, 1, 1, 1};
		// MSAA は resolve source として常に RT 状態スタート、SS は直接 sample されるので PSR スタート。
		const auto rtInit = msaa ? D3D12_RESOURCE_STATE_RENDER_TARGET
		                         : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		makeTex(kAccumFormat, clrA, m_sampleCount, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, rtInit, m_accum);
		makeTex(kRevealFormat, clrR, m_sampleCount, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, rtInit, m_reveal);
		m_accumState  = rtInit;
		m_revealState = rtInit;
		if (msaa)
		{
			// MSAA: composite で resolve してから sample する single-sample コピーを持つ。
			makeTex(kAccumFormat,  nullptr, 1, D3D12_RESOURCE_FLAG_NONE,
			        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, m_accumResolved);
			makeTex(kRevealFormat, nullptr, 1, D3D12_RESOURCE_FLAG_NONE,
			        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, m_revealResolved);
			m_accumResolvedState  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			m_revealResolvedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}

		// RTV ヒープ (accum, reveal)
		D3D12_DESCRIPTOR_HEAP_DESC rh{};
		rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rh.NumDescriptors = 2;
		if (FAILED(m_device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(m_rtvHeap.GetAddressOf()))))
		{
			throw std::runtime_error("WBOIT: CreateDescriptorHeap (RTV) failed");
		}
		const UINT rtvInc = m_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		m_device->CreateRenderTargetView(m_accum.Get(), nullptr, rtv);
		rtv.ptr += rtvInc;
		m_device->CreateRenderTargetView(m_reveal.Get(), nullptr, rtv);

		// SRV ヒープ (shader-visible: t0=accum, t1=reveal)
		D3D12_DESCRIPTOR_HEAP_DESC sh{};
		sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		sh.NumDescriptors = 2;
		sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(m_srvHeap.GetAddressOf()))))
		{
			throw std::runtime_error("WBOIT: CreateDescriptorHeap (SRV) failed");
		}
		const UINT srvInc = m_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		auto srv = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
		// SRV は MSAA 時 resolve 先 (single-sample) を、非 MSAA 時は RT 自身を指す。
		ID3D12Resource* srvAccum  = msaa ? m_accumResolved.Get()  : m_accum.Get();
		ID3D12Resource* srvReveal = msaa ? m_revealResolved.Get() : m_reveal.Get();
		D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sd.Texture2D.MipLevels = 1;
		sd.Format = kAccumFormat;
		m_device->CreateShaderResourceView(srvAccum, &sd, srv);
		srv.ptr += srvInc;
		sd.Format = kRevealFormat;
		m_device->CreateShaderResourceView(srvReveal, &sd, srv);
	}

	void createCompositePso(DXGI_FORMAT outFormat)
	{
		// root sig: SRV table (t0,t1) + static sampler (point clamp)
		D3D12_DESCRIPTOR_RANGE range{};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 2;
		range.BaseShaderRegister = 0;
		D3D12_ROOT_PARAMETER param{};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param.DescriptorTable.NumDescriptorRanges = 1;
		param.DescriptorTable.pDescriptorRanges = &range;
		param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC samp{};
		samp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rsd{};
		rsd.NumParameters = 1;
		rsd.pParameters = &param;
		rsd.NumStaticSamplers = 1;
		rsd.pStaticSamplers = &samp;
		rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		ComPtr<ID3DBlob> sig, err;
		if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
				sig.GetAddressOf(), err.GetAddressOf())))
		{
			throw std::runtime_error("WBOIT: composite SerializeRootSignature failed");
		}
		if (FAILED(m_device->CreateRootSignature(0, sig->GetBufferPointer(),
				sig->GetBufferSize(), IID_PPV_ARGS(m_compRootSig.GetAddressOf()))))
		{
			throw std::runtime_error("WBOIT: composite CreateRootSignature failed");
		}

		static constexpr std::string_view kCompositeHlsl = R"hlsl(
Texture2D<float4> accumTex  : register(t0);
Texture2D<float>  revealTex : register(t1);
SamplerState samp : register(s0);
struct VOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VOut VSMain(uint id : SV_VertexID)
{
    VOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PSMain(VOut i) : SV_TARGET
{
    float4 ac = accumTex.Sample(samp, i.uv);
    float  rv = revealTex.Sample(samp, i.uv).r;
    float  alpha = 1.0 - rv;                       // Π(1-a) → 透明レイヤの被覆率
    float3 col = ac.rgb / max(ac.a, 1e-5);         // 重み正規化したカラー
    return float4(col, alpha);                     // over ブレンドで背景へ重ねる
}
)hlsl";

		ComPtr<ID3DBlob> vs, ps, cerr;
		D3DCompile(kCompositeHlsl.data(), kCompositeHlsl.size(), nullptr, nullptr, nullptr,
			"VSMain", "vs_5_0", 0, 0, vs.GetAddressOf(), cerr.GetAddressOf());
		D3DCompile(kCompositeHlsl.data(), kCompositeHlsl.size(), nullptr, nullptr, nullptr,
			"PSMain", "ps_5_0", 0, 0, ps.GetAddressOf(), cerr.GetAddressOf());
		if (!vs || !ps)
		{
			throw std::runtime_error("WBOIT: composite D3DCompile failed");
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
		pd.pRootSignature = m_compRootSig.Get();
		pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
		pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
		auto& rt = pd.BlendState.RenderTarget[0];   // over: src_alpha / inv_src_alpha
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		pd.SampleMask = UINT_MAX;
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.DepthStencilState.DepthEnable = FALSE;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = outFormat;
		pd.SampleDesc.Count = m_sampleCount;   // out が MSAA color のとき一致させる
		if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_compPso.GetAddressOf()))))
		{
			throw std::runtime_error("WBOIT: composite CreateGraphicsPipelineState failed");
		}
	}

	static void transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
	                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		if (before == after) { return; }
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = res;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter = after;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cl->ResourceBarrier(1, &b);
	}

	ID3D12Device* m_device = nullptr;
	UINT m_width = 0, m_height = 0, m_sampleCount = 1;
	bool m_initialized = false;
	ComPtr<ID3D12Resource>       m_accum, m_reveal;                  ///< 蓄積 RT (MSAA 可)
	ComPtr<ID3D12Resource>       m_accumResolved, m_revealResolved;  ///< MSAA 時の resolve 先 (SS)。SRV はこちら
	D3D12_RESOURCE_STATES        m_accumState{}, m_revealState{};
	D3D12_RESOURCE_STATES        m_accumResolvedState{}, m_revealResolvedState{};
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_srvHeap;
	ComPtr<ID3D12RootSignature>  m_compRootSig;
	ComPtr<ID3D12PipelineState>  m_compPso;
};

}  // namespace mitiru::render::dx12

#endif  // _WIN32
