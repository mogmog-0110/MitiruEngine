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

/// @brief root signature と PSO を 1 つずつ持つだけの compute パス
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
};

} // namespace mitiru::render::dx12

#endif // _WIN32
