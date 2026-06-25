#pragma once
// DX12Live2D の createPipeline (root sig + シェーダ + PSO)。DX12Live2D.hpp 末尾から include。

namespace mitiru::render
{

inline bool Dx12Live2D::createPipeline(ID3D12Device* device)
{
	// root sig: b0 CBV, t0 table, t1 table, t2 table(dest copy), s0(wrap) s1(clamp)
	D3D12_DESCRIPTOR_RANGE r0={}; r0.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r0.NumDescriptors=1; r0.BaseShaderRegister=0;
	D3D12_DESCRIPTOR_RANGE r1={}; r1.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r1.NumDescriptors=1; r1.BaseShaderRegister=1;
	D3D12_DESCRIPTOR_RANGE r2={}; r2.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r2.NumDescriptors=1; r2.BaseShaderRegister=2;
	D3D12_ROOT_PARAMETER rp[4]={};
	rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; rp[0].Descriptor.ShaderRegister=0; rp[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
	rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[1].DescriptorTable.NumDescriptorRanges=1; rp[1].DescriptorTable.pDescriptorRanges=&r0; rp[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
	rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[2].DescriptorTable.NumDescriptorRanges=1; rp[2].DescriptorTable.pDescriptorRanges=&r1; rp[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
	rp[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[3].DescriptorTable.NumDescriptorRanges=1; rp[3].DescriptorTable.pDescriptorRanges=&r2; rp[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_STATIC_SAMPLER_DESC ss[2]={};
	for (int i=0;i<2;++i){ ss[i].Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		ss[i].AddressU=ss[i].AddressV=ss[i].AddressW=(i==0)?D3D12_TEXTURE_ADDRESS_MODE_WRAP:D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		ss[i].ShaderRegister=i; ss[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL; ss[i].MaxLOD=D3D12_FLOAT32_MAX; }
	D3D12_ROOT_SIGNATURE_DESC rsd={}; rsd.NumParameters=4; rsd.pParameters=rp; rsd.NumStaticSamplers=2; rsd.pStaticSamplers=ss;
	rsd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ComPtr<ID3DBlob> sig,err;
	if (FAILED(D3D12SerializeRootSignature(&rsd,D3D_ROOT_SIGNATURE_VERSION_1,sig.GetAddressOf(),err.GetAddressOf()))) return false;
	if (FAILED(device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(m_rootSig.GetAddressOf())))) return false;

	static const char* kHLSL = R"(
cbuffer CB:register(b0){ float4x4 projectMatrix; float4x4 clipMatrix; float4 baseColor; float4 multiplyColor; float4 screenColor; float4 channelFlag; float4 blendMode; };
Texture2D tex0:register(t0); Texture2D tex1:register(t1); Texture2D tex2:register(t2);
SamplerState samp0:register(s0); SamplerState samp1:register(s1);
struct VIN{ float2 pos:POSITION; float2 uv:TEXCOORD0; };
struct VOUT{ float4 position:SV_POSITION; float2 uv:TEXCOORD0; float4 clipPos:TEXCOORD1; };
struct FOUT{ float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
VOUT VsNormal(VIN i){ VOUT o=(VOUT)0; o.position=mul(float4(i.pos,0,1),projectMatrix); o.uv=float2(i.uv.x,1-i.uv.y); o.clipPos=0; return o; }
VOUT VsSetupMask(VIN i){ VOUT o=(VOUT)0; o.position=mul(float4(i.pos,0,1),projectMatrix); o.clipPos=o.position; o.uv=float2(i.uv.x,1-i.uv.y); return o; }
VOUT VsMasked(VIN i){ VOUT o=(VOUT)0; o.position=mul(float4(i.pos,0,1),projectMatrix); o.clipPos=mul(float4(i.pos,0,1),clipMatrix); o.uv=float2(i.uv.x,1-i.uv.y); return o; }
FOUT VsFull(uint id:SV_VertexID){ FOUT o; float2 uv=float2((id<<1)&2,id&2); o.uv=uv; o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1); return o; }
float4 PsNormal(VOUT i):SV_Target{
    float4 t=tex0.Sample(samp0,i.uv); t.rgb*=multiplyColor.rgb; t.rgb=(t.rgb+screenColor.rgb)-(t.rgb*screenColor.rgb);
    float4 c=t*baseColor; c.xyz*=c.w; return c; }
float4 PsSetupMask(VOUT i):SV_Target{
    float ins=step(baseColor.x,i.clipPos.x/i.clipPos.w)*step(baseColor.y,i.clipPos.y/i.clipPos.w)
             *step(i.clipPos.x/i.clipPos.w,baseColor.z)*step(i.clipPos.y/i.clipPos.w,baseColor.w);
    return channelFlag*tex0.Sample(samp0,i.uv).a*ins; }
float4 maskedC(VOUT i,float inv){
    float4 t=tex0.Sample(samp0,i.uv); t.rgb*=multiplyColor.rgb; t.rgb=(t.rgb+screenColor.rgb)-(t.rgb*screenColor.rgb);
    float4 c=t*baseColor; c.xyz*=c.w;
    float2 mu=i.clipPos.xy/i.clipPos.w; mu.y=1+mu.y;
    float4 cm=(1-tex1.Sample(samp1,mu))*channelFlag; float mv=cm.r+cm.g+cm.b+cm.a;
    return c*(inv>0.5?(1-mv):mv); }
float4 PsMasked(VOUT i):SV_Target{ return maskedC(i,0); }
float4 PsMaskedInv(VOUT i):SV_Target{ return maskedC(i,1); }
// ── composite (basic, premult維持) : 公式 PixelNormalPremult 相当。offscreen RT(premult) に
//    mult/screen(×a)/opacity を適用し premult のまま出力 → 固定機能 over/add/mult で親へ合成 ──
float4 PsComposite(FOUT i):SV_Target{
    float4 c=tex0.Sample(samp0,i.uv);                                  // offscreen RT (premult)
    c.rgb=c.rgb*multiplyColor.rgb;
    c.rgb=(c.rgb+screenColor.rgb*c.a)-(c.rgb*screenColor.rgb);
    return c*baseColor; }                                              // opacity、premult のまま
// ── composite (masked, basic) : オフスクリーン自体のマスクで合成をクリップ。clipMatrix=NDC→mask UV ──
VOUT VsCompMasked(uint id:SV_VertexID){ VOUT o=(VOUT)0;
    float2 uv=float2((id<<1)&2,id&2); o.uv=uv;
    o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);
    o.clipPos=mul(o.position,clipMatrix); return o; }
float4 PsCompMasked(VOUT i):SV_Target{
    float4 c=tex0.Sample(samp1,i.uv);
    c.rgb=c.rgb*multiplyColor.rgb;
    c.rgb=(c.rgb+screenColor.rgb*c.a)-(c.rgb*screenColor.rgb);
    c=c*baseColor;
    float2 mu=i.clipPos.xy/i.clipPos.w; mu.y=1+mu.y;
    float cov=1.0-tex1.Sample(samp1,mu).r;                             // 高精度マスク (channel R)
    return c*cov; }
// ── blit : model RT を backbuffer へ ──
float4 PsBlit(FOUT i):SV_Target{ return tex0.Sample(samp0,i.uv)*baseColor; }
// ── sprite : 公式 LAppSprite 相当 (背景/歯車/閉じる)。projectMatrix で [0,1]² quad を NDC 矩形へ。straight alpha ──
VOUT VsSprite(uint id:SV_VertexID){ VOUT o=(VOUT)0;
    float2 kUV[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    float2 uv=kUV[id]; o.position=mul(float4(uv,0,1),projectMatrix); o.uv=uv; o.clipPos=0; return o; }
float4 PsSprite(VOUT i):SV_Target{ return tex0.Sample(samp1,i.uv)*baseColor; }
// ── advanced blend : tex0=offscreen(src), tex1=dest copy。channelFlag.x=colorType .y=alphaType ──
float3 straightC(float4 p){ return (p.a>1e-4)?p.rgb/p.a:float3(0,0,0); }
// 公式 CubismBlendMode.fx を忠実移植 (per-channel スカラー + whole-color)。s=source, d=destination。
float cbBurn(float s,float d){ if(abs(d-1.0)<1e-6) return 1.0; if(abs(s)<1e-6) return 0.0; return 1.0-min(1.0,(1.0-d)/s); }
float cbDodge(float s,float d){ if(d<=0.0) return 0.0; if(s==1.0) return 1.0; return min(1.0,d/(1.0-s)); }
float cbOverlay(float s,float d){ return (d<0.5)?2.0*s*d:1.0-2.0*(1.0-s)*(1.0-d); }
float cbHard(float s,float d){ return (s<0.5)?2.0*s*d:1.0-2.0*(1.0-s)*(1.0-d); }
float cbSoft(float s,float d){ float v1=d-(1.0-2.0*s)*d*(1.0-d); float v2=d+(2.0*s-1.0)*d*((16.0*d-12.0)*d+3.0); float v3=d+(2.0*s-1.0)*(sqrt(d)-d); if(s<=0.5) return v1; if(d<=0.25) return v2; return v3; }
float cbLinear(float s,float d){ float bu=max(0.0,2.0*s+d-1.0); float dg=min(1.0,2.0*(s-0.5)+d); return (s<0.5)?bu:dg; }
float lLuma(float3 c){ return 0.30*c.r+0.59*c.g+0.11*c.b; }
float lMaxC(float3 c){ return max(c.r,max(c.g,c.b)); }
float lMinC(float3 c){ return min(c.r,min(c.g,c.b)); }
float3 lClip(float3 c){ float lm=lLuma(c); float mx=lMaxC(c); float mn=lMinC(c); float3 o=c;
    if(mn<0.0) o=lm+(o-lm)*(lm/(lm-mn)); if(mx>1.0) o=lm+(o-lm)*((1.0-lm)/(mx-lm)); return o; }
float3 lSetLuma(float3 c,float lm){ return lClip(c+(lm-lLuma(c))); }
float3 lSetSat(float3 c,float sat){ float mx=lMaxC(c); float mn=lMinC(c); float md=c.r+c.g+c.b-mx-mn;
    float oM=(mn<mx)?sat:0.0; float oD=(mn<mx)?((md-mn)*sat/(mx-mn)):0.0;
    if(c.r==mx) return (c.b<c.g)?float3(oM,oD,0.0):float3(oM,0.0,oD);
    if(c.g==mx) return (c.r<c.b)?float3(0.0,oM,oD):float3(oD,oM,0.0);
    return (c.g<c.r)?float3(oD,0.0,oM):float3(0.0,oD,oM); }
float3 colorBlend(int ct,float3 s,float3 d){
    if(ct==1||ct==3) return min(s+d,1.0);            // AddCompatible / Add
    if(ct==2||ct==6) return s*d;                     // MultiplyCompatible / Multiply
    if(ct==4) return s+d;                            // AddGlow
    if(ct==5) return min(s,d);                       // Darken
    if(ct==7) return float3(cbBurn(s.r,d.r),cbBurn(s.g,d.g),cbBurn(s.b,d.b));       // ColorBurn
    if(ct==8) return max(float3(0,0,0),s+d-1.0);     // LinearBurn
    if(ct==9) return max(s,d);                       // Lighten
    if(ct==10) return s+d-s*d;                       // Screen
    if(ct==11) return float3(cbDodge(s.r,d.r),cbDodge(s.g,d.g),cbDodge(s.b,d.b));   // ColorDodge
    if(ct==12) return float3(cbOverlay(s.r,d.r),cbOverlay(s.g,d.g),cbOverlay(s.b,d.b)); // Overlay
    if(ct==13) return float3(cbSoft(s.r,d.r),cbSoft(s.g,d.g),cbSoft(s.b,d.b));      // SoftLight
    if(ct==14) return float3(cbHard(s.r,d.r),cbHard(s.g,d.g),cbHard(s.b,d.b));      // HardLight
    if(ct==15) return float3(cbLinear(s.r,d.r),cbLinear(s.g,d.g),cbLinear(s.b,d.b));// LinearLight
    if(ct==16) return lSetLuma(lSetSat(s,lMaxC(d)-lMinC(d)),lLuma(d));              // Hue
    if(ct==17) return lSetLuma(s,lLuma(d));          // Color
    return s;                                        // Normal(0)
}
float4 PsAdvanced(FOUT i):SV_Target{
    int ct=(int)(channelFlag.x+0.5), at=(int)(channelFlag.y+0.5);
    // src = offscreen RT (premult)。公式 GetNormalPremultColorInfo と同順: premult のまま
    // mult/screen(×a)/opacity を適用 → その後 straight 化。
    float4 sp=tex0.Sample(samp0,i.uv);
    sp.rgb=sp.rgb*multiplyColor.rgb;
    sp.rgb=(sp.rgb+screenColor.rgb*sp.a)-(sp.rgb*screenColor.rgb);
    sp=sp*baseColor;                                 // opacity (premult)
    float sa=sp.a; float3 s=straightC(sp);           // straight 化は mult/screen/opacity の後
    float4 dp=tex1.Sample(samp1,i.uv); float3 d=straightC(dp); float da=dp.a;
    float3 cb=colorBlend(ct,s,d);
    float x,y,z;
    if(at==1){ x=sa*da; y=0; z=da*(1-sa); }                        // atop
    else if(at==2){ x=0; y=0; z=da*(1-sa); }                        // out
    else if(at==3){ x=min(sa,da); y=max(sa-da,0.0); z=max(da-sa,0.0); } // conjoint over
    else if(at==4){ x=max(sa+da-1.0,0.0); y=min(sa,1-da); z=min(da,1-sa); } // disjoint over
    else { x=sa*da; y=sa*(1-da); z=da*(1-sa); }                     // over
    float3 rgb=cb*x + s*y + d*z; float a=x+y+z;
    return float4(rgb,a); }
// ── per-drawable advanced blend (公式 ExecuteDrawForDrawable + CubismBlendMode.fx) ──
//   tex0=drawable texture(straight), tex1=mask, tex2=dest copy(premult)。blendMode.xy=color/alpha type、
//   .z=masked、.w=inverted。出力は premult、ブレンドステートは REPLACE (Src=ONE,Dst=ZERO)。
struct VOUTA{ float4 position:SV_POSITION; float2 uv:TEXCOORD0; float4 clipPos:TEXCOORD1; float2 blendUv:TEXCOORD2; };
VOUTA VsDrawAdv(VIN i){ VOUTA o=(VOUTA)0;
    o.position=mul(float4(i.pos,0,1),projectMatrix);
    o.clipPos=mul(float4(i.pos,0,1),clipMatrix);
    o.uv=float2(i.uv.x,1-i.uv.y);
    o.blendUv=(o.position.xy/o.position.w)*float2(0.5,-0.5)+0.5;   // NDC→[0,1] UV (y反転)
    return o; }
float4 PsDrawAdv(VOUTA i):SV_Target{
    int ct=(int)(blendMode.x+0.5), at=(int)(blendMode.y+0.5);
    float4 t=tex0.Sample(samp0,i.uv); t.rgb*=multiplyColor.rgb; t.rgb=(t.rgb+screenColor.rgb)-(t.rgb*screenColor.rgb);
    float4 src=t*baseColor;                                        // straight (baseColor=(1,1,1,op))
    float sa=src.a; float3 s=src.rgb;
    if(blendMode.z>0.5){ float2 mu=i.clipPos.xy/i.clipPos.w; mu.y=1+mu.y;
        float4 cm=(1-tex1.Sample(samp1,mu))*channelFlag; float mv=cm.r+cm.g+cm.b+cm.a;
        sa*=(blendMode.w>0.5)?(1-mv):mv; }
    float4 dp=tex2.Sample(samp1,i.blendUv); float da=dp.a; float3 d=straightC(dp);
    float3 cb=colorBlend(ct,s,d);
    float x,y,z;
    if(at==1){ x=sa*da; y=0; z=da*(1-sa); }
    else if(at==2){ x=0; y=0; z=da*(1-sa); }
    else if(at==3){ x=min(sa,da); y=max(sa-da,0.0); z=max(da-sa,0.0); }
    else if(at==4){ x=max(sa+da-1.0,0.0); y=min(sa,1-da); z=min(da,1-sa); }
    else { x=sa*da; y=sa*(1-da); z=da*(1-sa); }
    float3 rgb=cb*x + s*y + d*z; float a=x+y+z;
    return float4(rgb,a); }
)";
	auto comp=[&](const char* e,const char* t,ComPtr<ID3DBlob>& o)->bool{ ComPtr<ID3DBlob> ce;
		if (FAILED(D3DCompile(kHLSL,std::strlen(kHLSL),nullptr,nullptr,nullptr,e,t,0,0,o.GetAddressOf(),ce.GetAddressOf()))){
			if (ce) std::fprintf(stderr,"[Live2D] %s: %s\n",e,(const char*)ce->GetBufferPointer()); return false; } return true; };
	ComPtr<ID3DBlob> vN,vS,vM,vF,vSp,vCM,vDA, pN,pS,pM,pMi,pC,pB,pA,pSp,pCM,pDA;
	if (!comp("VsNormal","vs_5_0",vN)||!comp("VsSetupMask","vs_5_0",vS)||!comp("VsMasked","vs_5_0",vM)||!comp("VsFull","vs_5_0",vF)||!comp("VsSprite","vs_5_0",vSp)||!comp("VsCompMasked","vs_5_0",vCM)||!comp("VsDrawAdv","vs_5_0",vDA)) return false;
	if (!comp("PsNormal","ps_5_0",pN)||!comp("PsSetupMask","ps_5_0",pS)||!comp("PsMasked","ps_5_0",pM)||!comp("PsMaskedInv","ps_5_0",pMi)) return false;
	if (!comp("PsComposite","ps_5_0",pC)||!comp("PsBlit","ps_5_0",pB)||!comp("PsAdvanced","ps_5_0",pA)||!comp("PsSprite","ps_5_0",pSp)||!comp("PsCompMasked","ps_5_0",pCM)||!comp("PsDrawAdv","ps_5_0",pDA)) return false;

	D3D12_INPUT_ELEMENT_DESC il[]={
		{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0} };
	// blend: 0=Normal 1=Add 2=Mult 3=Mask 4=ColorAlphaBlend 5=Sprite(straight alpha)
	const D3D12_BLEND SC[6]={D3D12_BLEND_ONE,D3D12_BLEND_ONE,D3D12_BLEND_DEST_COLOR,D3D12_BLEND_ZERO,D3D12_BLEND_ONE,D3D12_BLEND_SRC_ALPHA};
	const D3D12_BLEND DC[6]={D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_INV_SRC_COLOR,D3D12_BLEND_ZERO,D3D12_BLEND_INV_SRC_ALPHA};
	const D3D12_BLEND SA[6]={D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_ZERO,D3D12_BLEND_ZERO,D3D12_BLEND_ONE,D3D12_BLEND_ONE};
	const D3D12_BLEND DA[6]={D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ONE,D3D12_BLEND_ONE,D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_ZERO,D3D12_BLEND_INV_SRC_ALPHA};
	auto bc=[](ComPtr<ID3DBlob>& b){ D3D12_SHADER_BYTECODE s={b->GetBufferPointer(),b->GetBufferSize()}; return s; };
	auto mk=[&](ComPtr<ID3DBlob>& vs,ComPtr<ID3DBlob>& ps,int blend,bool inLayout,ComPtr<ID3D12PipelineState>& out,D3D12_CULL_MODE cull=D3D12_CULL_MODE_NONE)->bool{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC d={}; d.pRootSignature=m_rootSig.Get(); d.VS=bc(vs); d.PS=bc(ps);
		if (inLayout) d.InputLayout={il,2};
		d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode=cull;
		d.RasterizerState.FrontCounterClockwise=TRUE; d.RasterizerState.DepthClipEnable=FALSE;
		d.DepthStencilState.DepthEnable=FALSE; d.DepthStencilState.StencilEnable=FALSE;
		auto& rt=d.BlendState.RenderTarget[0]; rt.BlendEnable=TRUE; rt.SrcBlend=SC[blend]; rt.DestBlend=DC[blend]; rt.BlendOp=D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha=SA[blend]; rt.DestBlendAlpha=DA[blend]; rt.BlendOpAlpha=D3D12_BLEND_OP_ADD; rt.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
		d.SampleMask=UINT_MAX; d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; d.NumRenderTargets=1;
		d.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
		return SUCCEEDED(device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(out.GetAddressOf()))); };

	// 通常 (両面=cull none) と片面 (cull back, 公式 Cull_Ccw + FrontCounterClockwise) の 2 系統
	for (int b=0;b<3;++b) if (!mk(vN,pN,b,true,m_pso[b])) return false;
	for (int b=0;b<3;++b) if (!mk(vN,pN,b,true,m_psoCull[b],D3D12_CULL_MODE_BACK)) return false;
	if (!mk(vS,pS,3,true,m_psoMaskSetup)) return false;
	if (!mk(vM,pM,0,true,m_psoMasked)) return false;
	if (!mk(vM,pM,0,true,m_psoMaskedCull,D3D12_CULL_MODE_BACK)) return false;
	if (!mk(vM,pMi,0,true,m_psoMaskedInv)) return false;
	if (!mk(vM,pMi,0,true,m_psoMaskedInvCull,D3D12_CULL_MODE_BACK)) return false;
	for (int b=0;b<3;++b) if (!mk(vF,pC,b,false,m_psoComp[b])) return false;
	for (int b=0;b<3;++b) if (!mk(vCM,pCM,b,false,m_psoCompMasked[b])) return false;   // オフスクリーンマスク付き合成
	// per-drawable advanced blend (blend=4 REPLACE、メッシュ描画なので input layout 有り)。両面/片面。
	if (!mk(vDA,pDA,4,true,m_psoDrawAdv)) return false;
	if (!mk(vDA,pDA,4,true,m_psoDrawAdvCull,D3D12_CULL_MODE_BACK)) return false;
	if (!mk(vF,pA,4,false,m_psoAdv)) return false;
	if (!mk(vF,pB,0,false,m_psoBlit)) return false;
	if (!mk(vSp,pSp,5,false,m_psoSprite)) return false;   // 公式 LAppView 相当のステージスプライト
	return true;
}

}  // namespace mitiru::render
