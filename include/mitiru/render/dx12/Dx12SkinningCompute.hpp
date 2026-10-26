#pragma once

/// @file Dx12SkinningCompute.hpp
/// @brief 線形ブレンドスキニングの compute パス。頂点は GPU 上で変形され CPU へ戻らない
/// @details 式は Skinning.hpp::skinVertices と同じ (重みは和で割る・範囲外 joint は無視・
///          位置は w で割る・法線は 3x3 を掛けて長さ 1)。出力は Vertex3D と同じ 48B 並びで、
///          そのまま頂点バッファとして描ける。
///          出力バッファは「このコマンドリストでまだ読まれていない」状態で渡すこと。
///          バッファは ExecuteCommandLists の終わりに COMMON へ戻り、UAV へ暗黙に昇格できるので
///          書く前の遷移は張らない。読んだ後に同じリストで書くと、その昇格が使えない。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>

namespace mitiru::render::dx12
{

inline constexpr const char* kSkinningCsHlsl = R"HLSL(
cbuffer Params : register(b0)
{
    uint gVertexCount;
    uint gJointCount;
};
ByteAddressBuffer gPalette : register(t0);
ByteAddressBuffer gBase : register(t1);
ByteAddressBuffer gBinding : register(t2);
RWByteAddressBuffer gOut : register(u0);

float4 paletteRow(uint joint, uint row)
{
    return asfloat(gPalette.Load4((joint * 4 + row) * 16));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= gVertexCount) { return; }
    const uint ofs = i * 48;
    float4 v0 = asfloat(gBase.Load4(ofs));
    float4 v1 = asfloat(gBase.Load4(ofs + 16));
    const float4 v2 = asfloat(gBase.Load4(ofs + 32));
    const uint4 joints = gBinding.Load4(i * 32);
    const float4 weights = asfloat(gBinding.Load4(i * 32 + 16));
    const float wsum = weights.x + weights.y + weights.z + weights.w;
    if (wsum > 1e-8)
    {
        float4 r0 = 0, r1 = 0, r2 = 0, r3 = 0;
        [unroll] for (uint k = 0; k < 4; ++k)
        {
            if (weights[k] == 0.0 || joints[k] >= gJointCount) { continue; }
            const float s = weights[k] / wsum;
            r0 += paletteRow(joints[k], 0) * s;
            r1 += paletteRow(joints[k], 1) * s;
            r2 += paletteRow(joints[k], 2) * s;
            r3 += paletteRow(joints[k], 3) * s;
        }
        const float4 p = float4(v0.xyz, 1.0);
        const float w = dot(r3, p);
        const float invW = (w == 0.0) ? 0.0 : 1.0 / w;
        const float3 pos = float3(dot(r0, p), dot(r1, p), dot(r2, p)) * invW;
        const float3 nIn = float3(v0.w, v1.x, v1.y);
        float3 n = float3(dot(r0.xyz, nIn), dot(r1.xyz, nIn), dot(r2.xyz, nIn));
        const float len = length(n);
        if (len > 1e-7) { n /= len; }
        v0 = float4(pos, n.x);
        v1.xy = n.yz;
    }
    gOut.Store4(ofs, asuint(v0));
    gOut.Store4(ofs + 16, asuint(v1));
    gOut.Store4(ofs + 32, asuint(v2));
}
)HLSL";

/// モーフを足してからスキンする版。モーフのある prim で重みが 0 でないときだけ使い、ほかの描画は上の kSkinningCsHlsl のまま
/// (モーフなしの絵と性能が変わらないことを構造で決めるため、同じ shader に分岐を足さない)。
/// 頂点ごとに自分の行 (gMorphRows) の差分だけを target の昇順に足す gather で、atomic が要らず、結果は実行ごとに同じ。
/// 行の 1 要素は 32 byte: target、位置の差分 xyz、法線の差分 xyz、パディング。重みは gWeights[gSlotBase + target]。
inline constexpr const char* kSkinningMorphCsHlsl = R"HLSL(
cbuffer Params : register(b0)
{
    uint gVertexCount;
    uint gJointCount;
    uint gSlotBase;
    uint gWeightCount;
};
ByteAddressBuffer gPalette : register(t0);
ByteAddressBuffer gBase : register(t1);
ByteAddressBuffer gBinding : register(t2);
ByteAddressBuffer gMorphRows : register(t3);
ByteAddressBuffer gMorphEntries : register(t4);
ByteAddressBuffer gWeights : register(t5);
RWByteAddressBuffer gOut : register(u0);

float4 paletteRow(uint joint, uint row)
{
    return asfloat(gPalette.Load4((joint * 4 + row) * 16));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= gVertexCount) { return; }
    const uint ofs = i * 48;
    float4 v0 = asfloat(gBase.Load4(ofs));
    float4 v1 = asfloat(gBase.Load4(ofs + 16));
    const float4 v2 = asfloat(gBase.Load4(ofs + 32));
    const uint4 joints = gBinding.Load4(i * 32);
    const float4 weights = asfloat(gBinding.Load4(i * 32 + 16));

    float3 mpos = v0.xyz;
    float3 mnrm = float3(v0.w, v1.x, v1.y);
    bool touched = false;
    const uint rowBegin = gMorphRows.Load(i * 4);
    const uint rowEnd = gMorphRows.Load(i * 4 + 4);
    for (uint e = rowBegin; e < rowEnd; ++e)
    {
        const uint4 a = gMorphEntries.Load4(e * 32);
        const uint4 b = gMorphEntries.Load4(e * 32 + 16);
        const uint slot = gSlotBase + a.x;
        if (slot >= gWeightCount) { continue; }
        const float mw = asfloat(gWeights.Load(slot * 4));
        if (mw == 0.0) { continue; }
        mpos += asfloat(a.yzw) * mw;
        mnrm += asfloat(b.xyz) * mw;
        touched = true;
    }
    v0 = float4(mpos, mnrm.x);
    v1.xy = mnrm.yz;

    const float wsum = weights.x + weights.y + weights.z + weights.w;
    if (wsum > 1e-8)
    {
        float4 r0 = 0, r1 = 0, r2 = 0, r3 = 0;
        [unroll] for (uint k = 0; k < 4; ++k)
        {
            if (weights[k] == 0.0 || joints[k] >= gJointCount) { continue; }
            const float s = weights[k] / wsum;
            r0 += paletteRow(joints[k], 0) * s;
            r1 += paletteRow(joints[k], 1) * s;
            r2 += paletteRow(joints[k], 2) * s;
            r3 += paletteRow(joints[k], 3) * s;
        }
        const float4 p = float4(v0.xyz, 1.0);
        const float w = dot(r3, p);
        const float invW = (w == 0.0) ? 0.0 : 1.0 / w;
        const float3 pos = float3(dot(r0, p), dot(r1, p), dot(r2, p)) * invW;
        const float3 nIn = float3(v0.w, v1.x, v1.y);
        float3 n = float3(dot(r0.xyz, nIn), dot(r1.xyz, nIn), dot(r2.xyz, nIn));
        const float len = length(n);
        if (len > 1e-7) { n /= len; }
        v0 = float4(pos, n.x);
        v1.xy = n.yz;
    }
    else if (touched)
    {
        const float len = length(mnrm);
        if (len > 1e-7) { mnrm /= len; }
        v0.w = mnrm.x;
        v1.xy = mnrm.yz;
    }
    gOut.Store4(ofs, asuint(v0));
    gOut.Store4(ofs + 16, asuint(v1));
    gOut.Store4(ofs + 32, asuint(v2));
}
)HLSL";

/// @brief 1 回の変形の入出力。palette は joint ごとに行優先 4x4 (sgc::Mat4f と同じ並び)
struct SkinningDispatch
{
	D3D12_GPU_VIRTUAL_ADDRESS palette = 0;
	D3D12_GPU_VIRTUAL_ADDRESS baseVertices = 0;   ///< Vertex3D の並び
	D3D12_GPU_VIRTUAL_ADDRESS bindings = 0;       ///< SkinVertexBinding の並び
	ID3D12Resource* out = nullptr;                ///< UAV 可・Vertex3D の並び
	std::uint32_t vertexCount = 0;
	std::uint32_t jointCount = 0;
};

/// @brief モーフつきの変形の入出力 (kSkinningMorphCsHlsl)。skin の入力に、差分の行・要素と重みの配列が付く
struct MorphSkinningDispatch
{
	SkinningDispatch skin;
	D3D12_GPU_VIRTUAL_ADDRESS morphRows = 0;      ///< uint32 × (頂点数 + 1)
	D3D12_GPU_VIRTUAL_ADDRESS morphEntries = 0;   ///< 32 byte × 要素数
	D3D12_GPU_VIRTUAL_ADDRESS weights = 0;        ///< float × weightCount
	std::uint32_t slotBase = 0;                   ///< prim のモーフ 0 番のスロット
	std::uint32_t weightCount = 0;
};

/// @brief root signature と PSO を持つだけの compute パス。モーフ用の PSO はモーフのあるモデルを読むときに別に作る
class Dx12SkinningCompute
{
public:
	[[nodiscard]] bool init(ID3D12Device* device)
	{
		if (!device) { return false; }
		D3D12_ROOT_PARAMETER p[5] = {};
		p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		p[0].Constants.Num32BitValues = 2;
		for (UINT i = 0; i < 3; ++i)
		{
			p[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			p[1 + i].Descriptor.ShaderRegister = i;
		}
		p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		D3D12_ROOT_SIGNATURE_DESC rsd = {};
		rsd.NumParameters = 5;
		rsd.pParameters = p;
		Microsoft::WRL::ComPtr<ID3DBlob> sig, cs, err;
		if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
		    FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
		                                       IID_PPV_ARGS(&m_rootSig))) ||
		    FAILED(D3DCompile(kSkinningCsHlsl, std::strlen(kSkinningCsHlsl), "Dx12SkinningCompute", nullptr,
		                      nullptr, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &err)))
		{
			return false;
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = m_rootSig.Get();
		pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
		return SUCCEEDED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso)));
	}

	[[nodiscard]] bool ready() const noexcept { return m_pso != nullptr; }

	/// @brief モーフ用の root signature と PSO を作る。モーフを持つモデルを読むときに呼ぶ (描くときには作らない)
	[[nodiscard]] bool initMorph(ID3D12Device* device)
	{
		if (!device) { return false; }
		if (m_morphPso) { return true; }
		D3D12_ROOT_PARAMETER p[8] = {};
		p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		p[0].Constants.Num32BitValues = 4;
		for (UINT i = 0; i < 6; ++i)
		{
			p[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			p[1 + i].Descriptor.ShaderRegister = i;
		}
		p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		D3D12_ROOT_SIGNATURE_DESC rsd = {};
		rsd.NumParameters = 8;
		rsd.pParameters = p;
		Microsoft::WRL::ComPtr<ID3DBlob> sig, cs, err;
		if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
		    FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
		                                       IID_PPV_ARGS(&m_morphRootSig))) ||
		    FAILED(D3DCompile(kSkinningMorphCsHlsl, std::strlen(kSkinningMorphCsHlsl), "Dx12SkinningMorphCompute", nullptr,
		                      nullptr, "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &err)))
		{
			return false;
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
		pd.pRootSignature = m_morphRootSig.Get();
		pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
		return SUCCEEDED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_morphPso)));
	}

	[[nodiscard]] bool readyMorph() const noexcept { return m_morphPso != nullptr; }

	/// @brief モーフ用の root signature と PSO を設定する。通常の変形へ戻すときは bind を呼び直す
	void bindMorph(ID3D12GraphicsCommandList* cl) const
	{
		if (!readyMorph() || !cl) { return; }
		cl->SetComputeRootSignature(m_morphRootSig.Get());
		cl->SetPipelineState(m_morphPso.Get());
	}

	/// @brief モーフつきの変形を 1 つ記録する (bindMorph の後)
	void dispatchMorph(ID3D12GraphicsCommandList* cl, const MorphSkinningDispatch& d) const
	{
		if (!readyMorph() || !cl || !d.skin.out || d.skin.vertexCount == 0) { return; }
		const std::uint32_t consts[4] = {d.skin.vertexCount, d.skin.jointCount, d.slotBase, d.weightCount};
		cl->SetComputeRoot32BitConstants(0, 4, consts, 0);
		cl->SetComputeRootShaderResourceView(1, d.skin.palette);
		cl->SetComputeRootShaderResourceView(2, d.skin.baseVertices);
		cl->SetComputeRootShaderResourceView(3, d.skin.bindings);
		cl->SetComputeRootShaderResourceView(4, d.morphRows);
		cl->SetComputeRootShaderResourceView(5, d.morphEntries);
		cl->SetComputeRootShaderResourceView(6, d.weights);
		cl->SetComputeRootUnorderedAccessView(7, d.skin.out->GetGPUVirtualAddress());
		cl->Dispatch((d.skin.vertexCount + 63) / 64, 1, 1);
	}

	/// @brief root signature と PSO を設定する。同じリストで続ける dispatch の前に 1 回だけ呼ぶ
	void bind(ID3D12GraphicsCommandList* cl) const
	{
		if (!ready() || !cl) { return; }
		cl->SetComputeRootSignature(m_rootSig.Get());
		cl->SetPipelineState(m_pso.Get());
	}

	/// @brief 変形を 1 つ記録する。出力ごとにバッファが違えば、続けて積んだ dispatch の間に barrier は要らない
	void dispatch(ID3D12GraphicsCommandList* cl, const SkinningDispatch& d) const
	{
		if (!ready() || !cl || !d.out || d.vertexCount == 0) { return; }
		const std::uint32_t consts[2] = {d.vertexCount, d.jointCount};
		cl->SetComputeRoot32BitConstants(0, 2, consts, 0);
		cl->SetComputeRootShaderResourceView(1, d.palette);
		cl->SetComputeRootShaderResourceView(2, d.baseVertices);
		cl->SetComputeRootShaderResourceView(3, d.bindings);
		cl->SetComputeRootUnorderedAccessView(4, d.out->GetGPUVirtualAddress());
		cl->Dispatch((d.vertexCount + 63) / 64, 1, 1);
	}

	/// @brief 書き終えた out を頂点バッファとして読める状態にする遷移 (まとめて 1 回の ResourceBarrier に渡す)
	[[nodiscard]] static D3D12_RESOURCE_BARRIER vertexReadBarrier(ID3D12Resource* out) noexcept
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = out;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		return b;
	}

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_morphRootSig;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_morphPso;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
