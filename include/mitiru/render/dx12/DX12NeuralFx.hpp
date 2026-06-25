#pragma once

/// @file DX12NeuralFx.hpp
/// @brief **DirectML 推論を D3D12 レンダリングパイプライン内で回す** in-pipeline ニューラル後処理。
/// @details backbuffer を CPU 往復なしでテンソル化し、自前の DirectML グラフを同一 D3D12 device 上で
///          実行して結果を合成する。DX12Neural.hpp (ORT+DML EP・readPixels で CPU 往復) と違い、
///          こちらは raw DirectML で **readback ゼロ**。
///            backbuffer → copy → src tex → [pack CS] → input tensor → [DirectML] → output tensor
///                       → [unpack CS] → result tex → [blit] → backbuffer
///          v1 の演算は element-wise scale (経路実証)。conv/SR/AA/style へ差し替え可能。
///          MITIRU_HAS_DIRECTML 未定義時はクラスごと無効。

#ifdef MITIRU_HAS_DIRECTML

#include <DirectML.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace mitiru::render
{

class Dx12NeuralPostFx
{
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	using Res = ComPtr<ID3D12Resource>;

public:
	void setEnabled(bool e) { m_enabled = e; }
	[[nodiscard]] bool enabled() const { return m_enabled; }
	void setStrength(float s) { m_strength = (s < 0.0f) ? 0.0f : (s > 2.0f ? 2.0f : s); }   ///< アンシャープ強度 (0..2)
	[[nodiscard]] float strength() const { return m_strength; }

	/// @brief 1 度 (+サイズ変更時) 構築。dml は ensureDirectMLDx12 が作った IDMLDevice。
	bool ensure(ID3D12Device* dev, IDMLDevice* dml, ID3D12GraphicsCommandList* cl, int w, int h)
	{
		if (m_dml && m_w == w && m_h == h) return m_built;
		if (!dev || !dml || !cl || w <= 0 || h <= 0) return false;
		m_dev = dev; m_dml = dml; m_w = w; m_h = h; m_built = false;

		if (!buildResources(dev, w, h)) return false;
		if (!buildPipelines(dev)) return false;
		if (!buildDml(dev, dml, cl, w, h)) return false;
		m_built = true;
		std::fprintf(stderr, "[NeuralFx] in-pipeline DirectML post-FX ready (%dx%d, zero readback)\n", w, h);
		return true;
	}

	/// @brief endFrame (Live2D 後) に backbuffer へニューラル後処理を適用する。
	void apply(ID3D12GraphicsCommandList* cl, ID3D12Resource* backbuffer, D3D12_CPU_DESCRIPTOR_HANDLE backRtv, int w, int h)
	{
		if (!m_enabled || !m_built || !backbuffer) return;

		// 1) backbuffer → src tex
		tr(cl, backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
		tr(cl, m_srcTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		cl->CopyResource(m_srcTex.Get(), backbuffer);
		tr(cl, m_srcTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		tr(cl, backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// 2) pack: src → input tensor (NCHW fp32)
		ID3D12DescriptorHeap* ch[] = {m_computeHeap.Get()};
		cl->SetDescriptorHeaps(1, ch);
		cl->SetComputeRootSignature(m_packRS.Get());
		cl->SetPipelineState(m_packPSO.Get());
		cl->SetComputeRootDescriptorTable(0, gpu(0));                         // t0 = src SRV
		cl->SetComputeRootUnorderedAccessView(1, m_inputBuf->GetGPUVirtualAddress());  // u0 = input tensor
		const UINT wh[2] = {(UINT)w, (UINT)h};
		cl->SetComputeRoot32BitConstants(2, 2, wh, 0);
		cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
		uav(cl, m_inputBuf.Get());

		// 3) DirectML: input → output
		ID3D12DescriptorHeap* dh[] = {m_dmlHeap.Get()};
		cl->SetDescriptorHeaps(1, dh);
		bindExecute();
		m_recorder->RecordDispatch(cl, m_compiledOp.Get(), m_execTable.Get());
		uav(cl, m_outputBuf.Get());

		// 4) unpack: output → result tex
		cl->SetDescriptorHeaps(1, ch);
		cl->SetComputeRootSignature(m_unpackRS.Get());
		cl->SetPipelineState(m_unpackPSO.Get());
		cl->SetComputeRootShaderResourceView(0, m_outputBuf->GetGPUVirtualAddress());  // t0 = output tensor (high-pass)
		cl->SetComputeRootDescriptorTable(1, gpu(0));                                  // t1=src SRV(slot0) + u0=result UAV(slot1)
		UINT uc[3] = {(UINT)w, (UINT)h, 0}; std::memcpy(&uc[2], &m_strength, 4);       // W, H, Strength(float bits)
		cl->SetComputeRoot32BitConstants(2, 3, uc, 0);
		tr(cl, m_resultTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
		tr(cl, m_resultTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// 5) blit result → backbuffer
		cl->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
		D3D12_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1}; D3D12_RECT sc = {0, 0, w, h};
		cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
		cl->SetGraphicsRootSignature(m_blitRS.Get());
		cl->SetPipelineState(m_blitPSO.Get());
		cl->SetDescriptorHeaps(1, ch);
		cl->SetGraphicsRootDescriptorTable(0, gpu(2));                       // result SRV
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cl->DrawInstanced(3, 1, 0, 0);
	}

private:
	static void tr(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
	{ D3D12_RESOURCE_BARRIER x={}; x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource=r;
	  x.Transition.StateBefore=a; x.Transition.StateAfter=b; x.Transition.Subresource=0; cl->ResourceBarrier(1,&x); }
	static void uav(ID3D12GraphicsCommandList* cl, ID3D12Resource* r)
	{ D3D12_RESOURCE_BARRIER x={}; x.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; x.UAV.pResource=r; cl->ResourceBarrier(1,&x); }
	D3D12_GPU_DESCRIPTOR_HANDLE gpu(int i){ auto h=m_computeHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr+=(UINT64)i*m_inc; return h; }

	bool buildResources(ID3D12Device* dev, int w, int h)
	{
		const UINT64 tensorBytes = (UINT64)1 * 3 * h * w * sizeof(float);
		auto buf=[&](UINT64 bytes, Res& out, D3D12_RESOURCE_STATES st)->bool{
			D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes; d.Height=1;
			d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			return SUCCEEDED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,st,nullptr,IID_PPV_ARGS(out.ReleaseAndGetAddressOf()))); };
		if (!buf(tensorBytes, m_inputBuf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) return false;
		if (!buf(tensorBytes, m_outputBuf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) return false;

		auto tex=[&](Res& out, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st)->bool{
			D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=(UINT64)w; d.Height=(UINT)h;
			d.DepthOrArraySize=1; d.MipLevels=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.Flags=fl;
			return SUCCEEDED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,st,nullptr,IID_PPV_ARGS(out.ReleaseAndGetAddressOf()))); };
		if (!tex(m_srcTex, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;
		if (!tex(m_resultTex, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;

		// compute heap: [0]=src SRV [1]=result UAV [2]=result SRV
		D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=3;
		hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(m_computeHeap.GetAddressOf())))) return false;
		m_inc=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		auto cpu=[&](int i){ auto x=m_computeHeap->GetCPUDescriptorHandleForHeapStart(); x.ptr+=(UINT64)i*m_inc; return x; };
		D3D12_SHADER_RESOURCE_VIEW_DESC srv={}; srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels=1;
		dev->CreateShaderResourceView(m_srcTex.Get(), &srv, cpu(0));
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavd={}; uavd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; uavd.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
		dev->CreateUnorderedAccessView(m_resultTex.Get(), nullptr, &uavd, cpu(1));
		dev->CreateShaderResourceView(m_resultTex.Get(), &srv, cpu(2));
		return true;
	}

	bool buildPipelines(ID3D12Device* dev)
	{
		static const char* kPack = R"(
Texture2D<float4> Src : register(t0);
RWStructuredBuffer<float> InT : register(u0);
cbuffer P : register(b0) { uint W; uint H; };
[numthreads(8,8,1)] void CSPack(uint3 id:SV_DispatchThreadID){
    if(id.x>=W||id.y>=H) return;
    float4 c = Src.Load(int3(id.xy,0));
    uint hw=H*W, i=id.y*W+id.x;
    InT[0*hw+i]=c.r; InT[1*hw+i]=c.g; InT[2*hw+i]=c.b;
})";
		// 輝度ベースのアンシャープマスク: DirectML conv は per-channel ハイパス (Laplacian) を出すが、
		// その「輝度成分のみ」を元画像へ等量加算する。→ チャンネル独立シャープン由来の色フリンジを排除し、
		// 強度を一括制御。Src(t1)=元画像、OutT(t0)=ハイパス、Dst(u0)=結果。
		static const char* kUnpack = R"(
StructuredBuffer<float> OutT : register(t0);
Texture2D<float4> Src : register(t1);
RWTexture2D<float4> Dst : register(u0);
cbuffer P : register(b0) { uint W; uint H; float Strength; };   // Strength = アンシャープ強度 (HUD スライダーで可変)
[numthreads(8,8,1)] void CSUnpack(uint3 id:SV_DispatchThreadID){
    if(id.x>=W||id.y>=H) return;
    uint hw=H*W, i=id.y*W+id.x;
    float3 hp = float3(OutT[0*hw+i], OutT[1*hw+i], OutT[2*hw+i]);   // per-channel ハイパス
    float hpY = dot(hp, float3(0.299,0.587,0.114));                 // 輝度のみ (色は動かさない)
    float3 orig = Src.Load(int3(id.xy,0)).rgb;
    Dst[id.xy] = float4(saturate(orig + Strength*hpY), 1.0);
})";
		ComPtr<ID3DBlob> packCS, unpackCS, e;
		if (FAILED(D3DCompile(kPack,std::strlen(kPack),nullptr,nullptr,nullptr,"CSPack","cs_5_0",0,0,packCS.GetAddressOf(),e.GetAddressOf())))
		{ if(e) std::fprintf(stderr,"[NeuralFx] CSPack: %s\n",(const char*)e->GetBufferPointer()); return false; }
		if (FAILED(D3DCompile(kUnpack,std::strlen(kUnpack),nullptr,nullptr,nullptr,"CSUnpack","cs_5_0",0,0,unpackCS.GetAddressOf(),e.GetAddressOf())))
		{ if(e) std::fprintf(stderr,"[NeuralFx] CSUnpack: %s\n",(const char*)e->GetBufferPointer()); return false; }

		// pack RS: t0(table) + u0(root UAV) + b0(2 const)
		D3D12_DESCRIPTOR_RANGE rs={}; rs.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rs.NumDescriptors=1; rs.BaseShaderRegister=0;
		D3D12_ROOT_PARAMETER pp[3]={};
		pp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; pp[0].DescriptorTable.NumDescriptorRanges=1; pp[0].DescriptorTable.pDescriptorRanges=&rs;
		pp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; pp[1].Descriptor.ShaderRegister=0;
		pp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; pp[2].Constants.Num32BitValues=2; pp[2].Constants.ShaderRegister=0;
		if (!makeRS(dev, pp, 3, m_packRS)) return false;
		if (!makeCSPSO(dev, m_packRS, packCS, m_packPSO)) return false;

		// unpack RS: t0(root SRV=OutT) + table{t1=src SRV @slot0, u0=result UAV @slot1} + b0
		D3D12_DESCRIPTOR_RANGE ru[2]={};
		ru[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ru[0].NumDescriptors=1; ru[0].BaseShaderRegister=1; ru[0].OffsetInDescriptorsFromTableStart=0;
		ru[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ru[1].NumDescriptors=1; ru[1].BaseShaderRegister=0; ru[1].OffsetInDescriptorsFromTableStart=1;
		D3D12_ROOT_PARAMETER up[3]={};
		up[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; up[0].Descriptor.ShaderRegister=0;
		up[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; up[1].DescriptorTable.NumDescriptorRanges=2; up[1].DescriptorTable.pDescriptorRanges=ru;
		up[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; up[2].Constants.Num32BitValues=3; up[2].Constants.ShaderRegister=0;   // W,H,Strength
		if (!makeRS(dev, up, 3, m_unpackRS)) return false;
		if (!makeCSPSO(dev, m_unpackRS, unpackCS, m_unpackPSO)) return false;

		// blit RS/PSO (fullscreen SRV → RTV)
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
	bool makeRS(ID3D12Device* dev, D3D12_ROOT_PARAMETER* p, UINT n, ComPtr<ID3D12RootSignature>& out){
		D3D12_ROOT_SIGNATURE_DESC d={}; d.NumParameters=n; d.pParameters=p; ComPtr<ID3DBlob> s,e;
		if (FAILED(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,s.GetAddressOf(),e.GetAddressOf()))) return false;
		return SUCCEEDED(dev->CreateRootSignature(0,s->GetBufferPointer(),s->GetBufferSize(),IID_PPV_ARGS(out.GetAddressOf()))); }
	bool makeCSPSO(ID3D12Device* dev, ComPtr<ID3D12RootSignature>& rs, ComPtr<ID3DBlob>& cs, ComPtr<ID3D12PipelineState>& out){
		D3D12_COMPUTE_PIPELINE_STATE_DESC d={}; d.pRootSignature=rs.Get(); d.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
		return SUCCEEDED(dev->CreateComputePipelineState(&d,IID_PPV_ARGS(out.GetAddressOf()))); }

	// ── DirectML: per-channel 3x3 Laplacian ハイパス畳み込み (輝度アンシャープの高周波成分を生成) ──
	bool buildDml(ID3D12Device* dev, IDMLDevice* dml, ID3D12GraphicsCommandList* cl, int w, int h)
	{
		// 入出力 {1,3,H,W}・フィルタ {3,3,3,3}=per-channel 3x3 ハイパスの畳み込み
		const UINT ioSizes[4] = {1, 3, (UINT)h, (UINT)w};
		DML_BUFFER_TENSOR_DESC ioBt={}; ioBt.DataType=DML_TENSOR_DATA_TYPE_FLOAT32; ioBt.DimensionCount=4; ioBt.Sizes=ioSizes;
		ioBt.TotalTensorSizeInBytes=(UINT64)1*3*h*w*sizeof(float);
		DML_TENSOR_DESC ioTd={DML_TENSOR_TYPE_BUFFER, &ioBt};
		const UINT fSizes[4]={3,3,3,3};
		DML_BUFFER_TENSOR_DESC fBt={}; fBt.DataType=DML_TENSOR_DATA_TYPE_FLOAT32; fBt.DimensionCount=4; fBt.Sizes=fSizes;
		fBt.TotalTensorSizeInBytes=81*sizeof(float);
		DML_TENSOR_DESC fTd={DML_TENSOR_TYPE_BUFFER, &fBt};
		if (!makeFilter(dev, cl)) return false;
		const UINT strides[2]={1,1}, dil[2]={1,1}, padS[2]={1,1}, padE[2]={1,1}, padO[2]={0,0};
		DML_CONVOLUTION_OPERATOR_DESC cv={};
		cv.InputTensor=&ioTd; cv.FilterTensor=&fTd; cv.BiasTensor=nullptr; cv.OutputTensor=&ioTd;
		cv.Mode=DML_CONVOLUTION_MODE_CROSS_CORRELATION; cv.Direction=DML_CONVOLUTION_DIRECTION_FORWARD;
		cv.DimensionCount=2; cv.Strides=strides; cv.Dilations=dil; cv.StartPadding=padS; cv.EndPadding=padE; cv.OutputPadding=padO;
		cv.GroupCount=1; cv.FusedActivation=nullptr;
		DML_OPERATOR_DESC od={DML_OPERATOR_CONVOLUTION, &cv};
		ComPtr<IDMLOperator> op;
		if (FAILED(dml->CreateOperator(&od, IID_PPV_ARGS(op.GetAddressOf())))) { std::fprintf(stderr,"[NeuralFx] CreateOperator(conv) failed\n"); return false; }
		if (FAILED(dml->CompileOperator(op.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(m_compiledOp.GetAddressOf())))) { std::fprintf(stderr,"[NeuralFx] CompileOperator failed\n"); return false; }

		ComPtr<IDMLOperatorInitializer> init;
		IDMLCompiledOperator* ops[]={m_compiledOp.Get()};
		if (FAILED(dml->CreateOperatorInitializer(1, ops, IID_PPV_ARGS(init.GetAddressOf())))) return false;
		DML_BINDING_PROPERTIES ip=init->GetBindingProperties(), ep=m_compiledOp->GetBindingProperties();
		const UINT descCount = (ip.RequiredDescriptorCount>ep.RequiredDescriptorCount)?ip.RequiredDescriptorCount:ep.RequiredDescriptorCount;
		const UINT64 tempSize = (ip.TemporaryResourceSize>ep.TemporaryResourceSize)?ip.TemporaryResourceSize:ep.TemporaryResourceSize;
		const UINT64 persSize = ep.PersistentResourceSize;

		D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=descCount?descCount:1;
		hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_dmlHeap.GetAddressOf())))) return false;
		if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_dmlInitHeap.GetAddressOf())))) return false;  // init 用 (execute と分離)
		auto dmlBuf=[&](UINT64 bytes, Res& out)->bool{ if(!bytes){return true;}
			D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes; d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			return SUCCEEDED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(out.ReleaseAndGetAddressOf()))); };
		if (!dmlBuf(tempSize, m_temp)) return false;
		if (!dmlBuf(persSize, m_pers)) return false;

		if (FAILED(dml->CreateCommandRecorder(IID_PPV_ARGS(m_recorder.GetAddressOf())))) return false;

		// init を 1 回実行 (init 専用 heap を使う = execute の bind で上書きされない)
		DML_BINDING_TABLE_DESC btd={}; btd.Dispatchable=init.Get();
		btd.CPUDescriptorHandle=m_dmlInitHeap->GetCPUDescriptorHandleForHeapStart();
		btd.GPUDescriptorHandle=m_dmlInitHeap->GetGPUDescriptorHandleForHeapStart();
		btd.SizeInDescriptors=descCount?descCount:1;
		ComPtr<IDMLBindingTable> initTable;
		if (FAILED(dml->CreateBindingTable(&btd, IID_PPV_ARGS(initTable.GetAddressOf())))) return false;
		if (tempSize){ DML_BUFFER_BINDING bb={m_temp.Get(),0,tempSize}; DML_BINDING_DESC bd={DML_BINDING_TYPE_BUFFER,&bb}; initTable->BindTemporaryResource(&bd); }
		if (persSize){ DML_BUFFER_BINDING bb={m_pers.Get(),0,persSize}; DML_BINDING_DESC bd={DML_BINDING_TYPE_BUFFER,&bb}; initTable->BindOutputs(1,&bd); }
		ID3D12DescriptorHeap* dh[]={m_dmlInitHeap.Get()}; cl->SetDescriptorHeaps(1,dh);
		m_recorder->RecordDispatch(cl, init.Get(), initTable.Get());
		uav(cl, m_pers ? m_pers.Get() : m_inputBuf.Get());

		// execute 用 binding table (execute 専用 heap・毎フレーム bind し直す)
		btd.Dispatchable=m_compiledOp.Get();
		btd.CPUDescriptorHandle=m_dmlHeap->GetCPUDescriptorHandleForHeapStart();
		btd.GPUDescriptorHandle=m_dmlHeap->GetGPUDescriptorHandleForHeapStart();
		if (FAILED(dml->CreateBindingTable(&btd, IID_PPV_ARGS(m_execTable.GetAddressOf())))) return false;
		m_tempSize=tempSize; m_persSize=persSize;
		return true;
	}

	bool makeFilter(ID3D12Device* dev, ID3D12GraphicsCommandList* cl)
	{
		float w[81]; std::memset(w,0,sizeof(w));
		const float k[9]={0,-1,0, -1,4,-1, 0,-1,0};   // 3x3 Laplacian ハイパス (sum=0)。輝度成分のみ unpack で加算
		for (int o=0;o<3;++o) for (int j=0;j<9;++j) w[o*27 + o*9 + j] = k[j];   // o==i (per-channel)
		D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=sizeof(w); d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		if (FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(m_filter.GetAddressOf())))) return false;
		D3D12_HEAP_PROPERTIES uh={}; uh.Type=D3D12_HEAP_TYPE_UPLOAD; D3D12_RESOURCE_DESC bd=d; bd.Flags=D3D12_RESOURCE_FLAG_NONE;
		if (FAILED(dev->CreateCommittedResource(&uh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(m_filterUp.GetAddressOf())))) return false;
		void* p=nullptr; D3D12_RANGE none={0,0}; m_filterUp->Map(0,&none,&p); std::memcpy(p,w,sizeof(w)); m_filterUp->Unmap(0,nullptr);
		cl->CopyResource(m_filter.Get(), m_filterUp.Get());
		tr(cl, m_filter.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		return true;
	}
	void bindExecute()
	{
		DML_BUFFER_BINDING in ={m_inputBuf.Get(), 0,(UINT64)1*3*m_h*m_w*sizeof(float)};
		DML_BUFFER_BINDING fil={m_filter.Get(),   0, 81*sizeof(float)};
		DML_BUFFER_BINDING out={m_outputBuf.Get(),0,(UINT64)1*3*m_h*m_w*sizeof(float)};
		// conv は入力3スロット (Input/Filter/Bias)。Bias は null → NONE を渡す。
		DML_BINDING_DESC ib[3]={{DML_BINDING_TYPE_BUFFER,&in},{DML_BINDING_TYPE_BUFFER,&fil},{DML_BINDING_TYPE_NONE,nullptr}};
		DML_BINDING_DESC ob={DML_BINDING_TYPE_BUFFER,&out};
		m_execTable->BindInputs(3,ib);
		m_execTable->BindOutputs(1,&ob);
		if (m_tempSize){ DML_BUFFER_BINDING bb={m_temp.Get(),0,m_tempSize}; DML_BINDING_DESC bd={DML_BINDING_TYPE_BUFFER,&bb}; m_execTable->BindTemporaryResource(&bd); }
		if (m_persSize){ DML_BUFFER_BINDING bb={m_pers.Get(),0,m_persSize}; DML_BINDING_DESC bd={DML_BINDING_TYPE_BUFFER,&bb}; m_execTable->BindPersistentResource(&bd); }
	}

	bool m_enabled=false, m_built=false;
	float m_strength=0.5f;
	int m_w=0, m_h=0;
	ID3D12Device* m_dev=nullptr; IDMLDevice* m_dml=nullptr;
	Res m_inputBuf, m_outputBuf, m_srcTex, m_resultTex, m_temp, m_pers, m_filter, m_filterUp;
	ComPtr<ID3D12DescriptorHeap> m_computeHeap, m_dmlHeap, m_dmlInitHeap; UINT m_inc=0;
	ComPtr<ID3D12RootSignature> m_packRS, m_unpackRS, m_blitRS;
	ComPtr<ID3D12PipelineState> m_packPSO, m_unpackPSO, m_blitPSO;
	ComPtr<IDMLCompiledOperator> m_compiledOp; ComPtr<IDMLCommandRecorder> m_recorder; ComPtr<IDMLBindingTable> m_execTable;
	UINT64 m_tempSize=0, m_persSize=0;
};

}  // namespace mitiru::render
#endif
