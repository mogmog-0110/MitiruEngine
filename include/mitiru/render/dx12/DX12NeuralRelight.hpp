#pragma once

/// @file DX12NeuralRelight.hpp
/// @brief **ニューラル・リライティング** — 平面 Live2D を「光を持つ立体的存在」に変える後処理。
/// @details 従来 Live2D は照明がテクスチャに焼き込み済みで動かせない。ここでは DirectML 単眼深度
///          (NeuralDepth, Depth-Anything-V2) でキャラの**立体形状を推定**し、可動光源で動的に
///          再ライティングする。**加算のみ** (元の絵は一切暗くせず光を“足す”だけ) なので、
///          Strength=0 で素の描画に完全一致 = 品質を下げない。深度はキャラ領域クロップで推定し
///          (全画面だと部屋の奥行きを拾い平面に潰れる)、深度が無い間は輝度プロキシにフォールバック。
///            backbuffer → copy → src → [relight CS: 深度→法線→可動光源, 加算] → result → blit
///          MITIRU_HAS_DIRECTML 連動。

#ifdef MITIRU_HAS_DIRECTML

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace mitiru::render
{

class Dx12NeuralRelight
{
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Res = ComPtr<ID3D12Resource>;

public:
	void setEnabled(bool e) { m_enabled = e; }
	[[nodiscard]] bool enabled() const { return m_enabled; }
	void setLight(float nx, float ny) { m_lx = nx; m_ly = ny; }
	void setParams(float strength, float rim, float relief) { m_strength = strength; m_rim = rim; m_relief = relief; }

	/// @brief NeuralDepth が出した深度マップ (0..1, 大=手前) を受け取り、次フレームのアップロードに回す。
	void setDepth(const float* data, int dW, int dH, float cx0, float cy0, float cx1, float cy1)
	{
		if (!data || dW <= 0 || dH <= 0) return;
		m_dW = dW; m_dH = dH; m_cx0 = cx0; m_cy0 = cy0; m_cx1 = cx1; m_cy1 = cy1;
		m_depthCPU.assign(data, data + (size_t)dW * dH);
		m_depthDirty = true; m_hasDepth = true;
	}

	bool ensure(ID3D12Device* dev, ID3D12GraphicsCommandList* /*cl*/, int w, int h)
	{
		if (m_built && m_w == w && m_h == h) return true;
		if (!dev || w <= 0 || h <= 0) return false;
		m_dev = dev; m_w = w; m_h = h; m_built = false;
		if (!buildResources(dev, w, h)) return false;
		if (!buildPipelines(dev)) return false;
		m_built = true;
		std::fprintf(stderr, "[Relight] neural relighting ready (%dx%d): flat 2D -> DirectML depth -> dynamic light (additive)\n", w, h);
		return true;
	}

	void apply(ID3D12GraphicsCommandList* cl, ID3D12Resource* backbuffer, D3D12_CPU_DESCRIPTOR_HANDLE backRtv, int w, int h)
	{
		if (!m_enabled || !m_built || !backbuffer) return;

		// 0) 深度マップが更新されていれば depth tex を(再)生成しアップロード
		if (m_depthDirty) { uploadDepth(cl); m_depthDirty = false; }

		// 1) backbuffer → src tex
		tr(cl, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
		tr(cl, m_srcTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		cl->CopyResource(m_srcTex.Get(), backbuffer);
		tr(cl, m_srcTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		tr(cl, backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// 2) relight compute: src(+depth) → result
		ID3D12DescriptorHeap* ch[] = {m_heap.Get()};
		cl->SetDescriptorHeaps(1, ch);
		cl->SetComputeRootSignature(m_rs.Get());
		cl->SetPipelineState(m_pso.Get());
		cl->SetComputeRootDescriptorTable(0, gpu(0));   // t0 = src SRV
		cl->SetComputeRootDescriptorTable(1, gpu(2));   // u0 = result UAV
		cl->SetComputeRootDescriptorTable(3, gpu(1));   // t1 = depth SRV (無ければ src を指す = 安全)
		struct CB { UINT W, H; float Lx, Ly, Lz, Strength, Rim, Relief;
		            float Cx0, Cy0, Cx1, Cy1; UINT UseDepth, _p0, _p1, _p2; } cb{
			(UINT)w, (UINT)h, m_lx, m_ly, m_lz, m_strength, m_rim, m_relief,
			m_cx0, m_cy0, m_cx1, m_cy1, (m_hasDepth && m_depthTex) ? 1u : 0u, 0, 0, 0 };
		cl->SetComputeRoot32BitConstants(2, 16, &cb, 0);
		tr(cl, m_resultTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
		tr(cl, m_resultTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// 3) blit result → backbuffer
		cl->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
		D3D12_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1}; D3D12_RECT sc = {0, 0, w, h};
		cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
		cl->SetGraphicsRootSignature(m_blitRS.Get());
		cl->SetPipelineState(m_blitPSO.Get());
		cl->SetDescriptorHeaps(1, ch);
		cl->SetGraphicsRootDescriptorTable(0, gpu(3));   // result SRV
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cl->DrawInstanced(3, 1, 0, 0);
	}

private:
	static void tr(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
	{ D3D12_RESOURCE_BARRIER x={}; x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource=r;
	  x.Transition.StateBefore=a; x.Transition.StateAfter=b; x.Transition.Subresource=0; cl->ResourceBarrier(1,&x); }
	D3D12_GPU_DESCRIPTOR_HANDLE gpu(int i){ auto h=m_heap->GetGPUDescriptorHandleForHeapStart(); h.ptr+=(UINT64)i*m_inc; return h; }
	D3D12_CPU_DESCRIPTOR_HANDLE cpu(int i){ auto h=m_heap->GetCPUDescriptorHandleForHeapStart(); h.ptr+=(UINT64)i*m_inc; return h; }

	bool buildResources(ID3D12Device* dev, int w, int h)
	{
		auto tex=[&](Res& out, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st)->bool{
			D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=(UINT64)w; d.Height=(UINT)h;
			d.DepthOrArraySize=1; d.MipLevels=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.Flags=fl;
			return SUCCEEDED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,st,nullptr,IID_PPV_ARGS(out.ReleaseAndGetAddressOf()))); };
		if (!tex(m_srcTex, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;
		if (!tex(m_resultTex, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;

		// heap: [0]=src SRV [1]=depth SRV [2]=result UAV [3]=result SRV
		D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=4;
		hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(m_heap.GetAddressOf())))) return false;
		m_inc=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv={}; srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
		dev->CreateShaderResourceView(m_srcTex.Get(), &srv, cpu(0));
		dev->CreateShaderResourceView(m_srcTex.Get(), &srv, cpu(1));   // [1] 仮: depth 未生成時は src を指す (安全な dummy)
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavd={}; uavd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; uavd.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
		dev->CreateUnorderedAccessView(m_resultTex.Get(), nullptr, &uavd, cpu(2));
		dev->CreateShaderResourceView(m_resultTex.Get(), &srv, cpu(3));
		return true;
	}

	/// 深度 CPU マップを depth tex (R32_FLOAT) へアップロード。サイズ変更時に(再)生成。
	void uploadDepth(ID3D12GraphicsCommandList* cl)
	{
		if (m_depthCPU.empty() || !m_dev) return;
		if (!m_depthTex || m_depthTexW != m_dW || m_depthTexH != m_dH)
		{
			D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=(UINT64)m_dW; d.Height=(UINT)m_dH;
			d.DepthOrArraySize=1; d.MipLevels=1; d.Format=DXGI_FORMAT_R32_FLOAT; d.SampleDesc.Count=1;
			if (FAILED(m_dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(m_depthTex.ReleaseAndGetAddressOf())))) return;
			// 配置フットプリント + アップロードバッファ
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 total=0;
			m_dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&total);
			m_depthFp=fp; m_depthUploadBytes=total;
			D3D12_HEAP_PROPERTIES uh={}; uh.Type=D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC bd={}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=total; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			if (FAILED(m_dev->CreateCommittedResource(&uh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(m_depthUpload.ReleaseAndGetAddressOf())))) return;
			// depth SRV を slot[1] に張り替え
			D3D12_SHADER_RESOURCE_VIEW_DESC ds={}; ds.Format=DXGI_FORMAT_R32_FLOAT; ds.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
			ds.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; ds.Texture2D.MipLevels=1;
			m_dev->CreateShaderResourceView(m_depthTex.Get(), &ds, cpu(1));
			m_depthTexW=m_dW; m_depthTexH=m_dH;
		}
		// CPU 深度 → upload バッファ (行ピッチ整列)
		void* p=nullptr; D3D12_RANGE none={0,0};
		if (FAILED(m_depthUpload->Map(0,&none,&p))) return;
		const UINT rowBytes=(UINT)m_dW*sizeof(float);
		for (int y=0;y<m_dH;++y)
			std::memcpy((std::uint8_t*)p + m_depthFp.Offset + (size_t)y*m_depthFp.Footprint.RowPitch,
			            m_depthCPU.data() + (size_t)y*m_dW, rowBytes);
		m_depthUpload->Unmap(0,nullptr);
		// upload → depth tex
		tr(cl, m_depthTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=m_depthTex.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex=0;
		D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=m_depthUpload.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=m_depthFp;
		cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
		tr(cl, m_depthTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	bool buildPipelines(ID3D12Device* dev)
	{
		static const char* kRelight = R"(
Texture2D<float4> Src : register(t0);
Texture2D<float>  Depth : register(t1);
RWTexture2D<float4> Dst : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint W; uint H; float Lx; float Ly; float Lz; float Strength; float Rim; float Relief;
                           float Cx0; float Cy0; float Cx1; float Cy1; uint UseDepth; uint p0; uint p1; uint p2; };
float luma(float3 c){ return dot(c, float3(0.299,0.587,0.114)); }
float lumaAt(int2 p){ p=clamp(p,int2(0,0),int2(W-1,H-1)); return luma(Src.Load(int3(p,0)).rgb); }
[numthreads(8,8,1)] void CSRelight(uint3 id:SV_DispatchThreadID){
    if(id.x>=W||id.y>=H) return;
    int2 p=int2(id.xy);
    float3 c=Src.Load(int3(p,0)).rgb;
    // 画面座標 → クロップ UV
    float2 uv=float2((id.x/(float)W - Cx0)/max(Cx1-Cx0,1e-4), (id.y/(float)H - Cy0)/max(Cy1-Cy0,1e-4));
    float3 N; float mask=1.0;
    if(UseDepth!=0 && uv.x>0.0 && uv.x<1.0 && uv.y>0.0 && uv.y<1.0){
        // ★DirectML 深度から実法線★ クロップ領域内のみ。近傍深度の勾配で面の向きを出す。
        float e=1.5/512.0;
        float dC=Depth.SampleLevel(samp,uv,0);
        float dR=Depth.SampleLevel(samp,uv+float2(e,0),0), dL=Depth.SampleLevel(samp,uv-float2(e,0),0);
        float dU=Depth.SampleLevel(samp,uv+float2(0,e),0), dD=Depth.SampleLevel(samp,uv-float2(0,e),0);
        N=normalize(float3(-(dR-dL)*Relief, (dU-dD)*Relief, 1.0));
        // 深度が背景(遠 ~ 0)の所はリライト弱める (人物前景に集中)
        mask=smoothstep(0.15,0.4,dC);
    } else {
        // フォールバック: 輝度プロキシ (深度未取得時)
        float gx=lumaAt(p+int2(1,0))-lumaAt(p+int2(-1,0));
        float gy=lumaAt(p+int2(0,1))-lumaAt(p+int2(0,-1));
        N=normalize(float3(-gx*Relief, -gy*Relief, 1.0));
    }
    float3 L=normalize(float3(Lx, Ly, max(Lz,0.2)));
    float3 V=float3(0,0,1);
    float key=saturate(dot(N,L));
    float rim=pow(1.0-saturate(dot(N,V)), 3.0) * (0.5+0.5*key);
    // ★加算のみ★ Strength がマスター: 0 で素の絵。元の色は暗くしない。
    float3 lightCol=float3(1.0, 0.96, 0.88);
    float3 added = lightCol * Strength * mask * (key*key*0.6 + rim*Rim);
    Dst[p]=float4(saturate(c + added), 1.0);
})";
		ComPtr<ID3DBlob> cs, e;
		if (FAILED(D3DCompile(kRelight,std::strlen(kRelight),nullptr,nullptr,nullptr,"CSRelight","cs_5_0",0,0,cs.GetAddressOf(),e.GetAddressOf())))
		{ if(e) std::fprintf(stderr,"[Relight] CSRelight: %s\n",(const char*)e->GetBufferPointer()); return false; }
		// RS: param0 t0(table) + param1 u0(table) + param2 b0(16 const) + param3 t1(table) + static sampler s0
		D3D12_DESCRIPTOR_RANGE rs0={}; rs0.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rs0.NumDescriptors=1; rs0.BaseShaderRegister=0;
		D3D12_DESCRIPTOR_RANGE ru0={}; ru0.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ru0.NumDescriptors=1; ru0.BaseShaderRegister=0;
		D3D12_DESCRIPTOR_RANGE rt1={}; rt1.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rt1.NumDescriptors=1; rt1.BaseShaderRegister=1;
		D3D12_ROOT_PARAMETER pp[4]={};
		pp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; pp[0].DescriptorTable.NumDescriptorRanges=1; pp[0].DescriptorTable.pDescriptorRanges=&rs0;
		pp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; pp[1].DescriptorTable.NumDescriptorRanges=1; pp[1].DescriptorTable.pDescriptorRanges=&ru0;
		pp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; pp[2].Constants.Num32BitValues=16; pp[2].Constants.ShaderRegister=0;
		pp[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; pp[3].DescriptorTable.NumDescriptorRanges=1; pp[3].DescriptorTable.pDescriptorRanges=&rt1;
		D3D12_STATIC_SAMPLER_DESC ss={}; ss.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR; ss.AddressU=ss.AddressV=ss.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP; ss.ShaderRegister=0; ss.MaxLOD=D3D12_FLOAT32_MAX;
		{ D3D12_ROOT_SIGNATURE_DESC d={}; d.NumParameters=4; d.pParameters=pp; d.NumStaticSamplers=1; d.pStaticSamplers=&ss; ComPtr<ID3DBlob> s,er;
		  if (FAILED(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,s.GetAddressOf(),er.GetAddressOf()))) return false;
		  if (FAILED(dev->CreateRootSignature(0,s->GetBufferPointer(),s->GetBufferSize(),IID_PPV_ARGS(m_rs.GetAddressOf())))) return false; }
		{ D3D12_COMPUTE_PIPELINE_STATE_DESC d={}; d.pRootSignature=m_rs.Get(); d.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
		  if (FAILED(dev->CreateComputePipelineState(&d,IID_PPV_ARGS(m_pso.GetAddressOf())))) return false; }

		static const char* kBlit = R"(
Texture2D t0:register(t0); SamplerState s0:register(s0);
struct O{float4 p:SV_POSITION; float2 uv:TEXCOORD0;};
O VS(uint id:SV_VertexID){ O o; float2 uv=float2((id<<1)&2,id&2); o.uv=uv; o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1); return o; }
float4 PS(O i):SV_Target{ return float4(t0.Sample(s0,i.uv).rgb,1.0); })";
		ComPtr<ID3DBlob> vs, ps;
		if (FAILED(D3DCompile(kBlit,std::strlen(kBlit),nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,vs.GetAddressOf(),e.GetAddressOf()))) return false;
		if (FAILED(D3DCompile(kBlit,std::strlen(kBlit),nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,ps.GetAddressOf(),e.GetAddressOf()))) return false;
		D3D12_DESCRIPTOR_RANGE rb={}; rb.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rb.NumDescriptors=1; rb.BaseShaderRegister=0;
		D3D12_ROOT_PARAMETER bp={}; bp.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; bp.DescriptorTable.NumDescriptorRanges=1; bp.DescriptorTable.pDescriptorRanges=&rb; bp.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC sm={}; sm.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR; sm.AddressU=sm.AddressV=sm.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sm.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL; sm.MaxLOD=D3D12_FLOAT32_MAX;
		D3D12_ROOT_SIGNATURE_DESC rd={}; rd.NumParameters=1; rd.pParameters=&bp; rd.NumStaticSamplers=1; rd.pStaticSamplers=&sm; rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> sig, err;
		if (FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,sig.GetAddressOf(),err.GetAddressOf()))) return false;
		if (FAILED(dev->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(m_blitRS.GetAddressOf())))) return false;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC g={}; g.pRootSignature=m_blitRS.Get(); g.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; g.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
		g.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; g.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; g.DepthStencilState.DepthEnable=FALSE;
		g.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL; g.SampleMask=UINT_MAX;
		g.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; g.NumRenderTargets=1; g.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM; g.SampleDesc.Count=1;
		if (FAILED(dev->CreateGraphicsPipelineState(&g,IID_PPV_ARGS(m_blitPSO.GetAddressOf())))) return false;
		return true;
	}

	bool m_enabled=false, m_built=false;
	int m_w=0, m_h=0;
	float m_lx=0.4f, m_ly=0.4f, m_lz=0.7f;
	float m_strength=0.6f, m_rim=0.5f, m_relief=6.0f;
	// 深度
	std::vector<float> m_depthCPU; bool m_depthDirty=false, m_hasDepth=false;
	int m_dW=0, m_dH=0, m_depthTexW=0, m_depthTexH=0;
	float m_cx0=0, m_cy0=0, m_cx1=1, m_cy1=1;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_depthFp{}; UINT64 m_depthUploadBytes=0;
	ID3D12Device* m_dev=nullptr;
	Res m_srcTex, m_resultTex, m_depthTex, m_depthUpload;
	ComPtr<ID3D12DescriptorHeap> m_heap; UINT m_inc=0;
	ComPtr<ID3D12RootSignature> m_rs, m_blitRS;
	ComPtr<ID3D12PipelineState> m_pso, m_blitPSO;
};

}  // namespace mitiru::render
#endif
