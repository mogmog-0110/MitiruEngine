#pragma once

/// @file DX12SplatSort.hpp
/// @brief GPU 側 splat 深度ソート (compute)。CPU/PCIe を使わず毎フレーム order を生成。
/// @details 2048-bin single-pass バケット/radix ソート。reset→range→hist→scan→scatter の
///          5 compute pass を graphics command list へ記録する (direct queue は compute 可)。
///          order は DEFAULT-heap UAV に生成され、描画の頂点シェーダが読む前に
///          UAV → NON_PIXEL_SHADER_RESOURCE へ遷移する。
///          位置は呼び出し側の ByteAddressBuffer から stride/offset 指定で読むため、
///          SplatGPU レイアウトに依存しない (テスト時は tight float 配列でも可)。

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

#include <mitiru/render/dx12/DX12SplatSortShaders.hpp>

namespace mitiru::render {

/// @brief GPU 深度バケットソート。init() で pipeline、setCount() で作業バッファ、
///        encode() で 5 pass を記録。order は toShaderResource() 後に描画が読む。
class SplatDepthSortGpu
{
public:
    static constexpr UINT kNumBuckets = 2048;

    /// @brief root sig + 5 compute PSO を一度だけ構築する。
    bool init(ID3D12Device* dev)
    {
        if (!dev) { return false; }

        // b0 = 8×32bit const / t0 = 位置(SRV root descriptor) / u0,u1,u2 = range,hist,order
        D3D12_ROOT_PARAMETER p[5] = {};
        p[0].ParameterType              = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[0].Constants.ShaderRegister   = 0;
        p[0].Constants.Num32BitValues   = 8;
        p[0].ShaderVisibility           = D3D12_SHADER_VISIBILITY_ALL;
        p[1].ParameterType              = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[1].Descriptor.ShaderRegister  = 0;
        p[2].ParameterType              = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[2].Descriptor.ShaderRegister  = 0;
        p[3].ParameterType              = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[3].Descriptor.ShaderRegister  = 1;
        p[4].ParameterType              = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[4].Descriptor.ShaderRegister  = 2;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 5;
        rsd.pParameters   = p;
        rsd.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                sig.GetAddressOf(), err.GetAddressOf()))) { return false; }
        if (FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                IID_PPV_ARGS(m_rootSig.GetAddressOf())))) { return false; }

        if (!buildPso(dev, "CSReset",   m_psoReset))   { return false; }
        if (!buildPso(dev, "CSRange",   m_psoRange))   { return false; }
        if (!buildPso(dev, "CSHist",    m_psoHist))    { return false; }
        if (!buildPso(dev, "CSScan",    m_psoScan))    { return false; }
        if (!buildPso(dev, "CSScatter", m_psoScatter)) { return false; }

        m_ready = true;
        return true;
    }

    /// @brief splat 数に合わせて order/range/hist の DEFAULT-heap UAV を確保する。
    bool setCount(ID3D12Device* dev, UINT count)
    {
        if (!m_ready || !dev || count == 0) { return false; }
        m_count = count;
        if (!makeUav(dev, static_cast<UINT64>(count) * sizeof(std::uint32_t), m_order)) { return false; }
        if (!makeUav(dev, 2ull * sizeof(std::uint32_t), m_range)) { return false; }
        if (!makeUav(dev, static_cast<UINT64>(kNumBuckets) * sizeof(std::uint32_t), m_hist)) { return false; }
        m_orderState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    }

    /// @brief 5 compute pass を記録する。posVA = 位置 ByteAddressBuffer の GPU VA。
    void encode(ID3D12GraphicsCommandList* cl, D3D12_GPU_VIRTUAL_ADDRESS posVA,
                UINT posStrideBytes, UINT posOffsetBytes, float cx, float cy, float cz)
    {
        if (!m_ready || !m_order || m_count == 0 || !cl) { return; }
        toUav(cl);   // 前フレームで SRV にしていたら UAV へ戻す

        cl->SetComputeRootSignature(m_rootSig.Get());
        std::uint32_t c[8];
        c[0] = m_count; c[1] = posStrideBytes; c[2] = posOffsetBytes;
        std::memcpy(&c[3], &cx, sizeof(float));
        std::memcpy(&c[4], &cy, sizeof(float));
        std::memcpy(&c[5], &cz, sizeof(float));
        c[6] = 0; c[7] = 0;
        cl->SetComputeRoot32BitConstants(0, 8, c, 0);
        cl->SetComputeRootShaderResourceView(1, posVA);
        cl->SetComputeRootUnorderedAccessView(2, m_range->GetGPUVirtualAddress());
        cl->SetComputeRootUnorderedAccessView(3, m_hist->GetGPUVirtualAddress());
        cl->SetComputeRootUnorderedAccessView(4, m_order->GetGPUVirtualAddress());

        const UINT gElems = (m_count + 255u) / 256u;
        const UINT gBins  = (kNumBuckets + 255u) / 256u;
        cl->SetPipelineState(m_psoReset.Get());   cl->Dispatch(gBins,  1, 1); uavBarrier(cl);
        cl->SetPipelineState(m_psoRange.Get());   cl->Dispatch(gElems, 1, 1); uavBarrier(cl);
        cl->SetPipelineState(m_psoHist.Get());    cl->Dispatch(gElems, 1, 1); uavBarrier(cl);
        cl->SetPipelineState(m_psoScan.Get());    cl->Dispatch(1,      1, 1); uavBarrier(cl);
        cl->SetPipelineState(m_psoScatter.Get()); cl->Dispatch(gElems, 1, 1); uavBarrier(cl);
    }

    /// @brief order を描画 (頂点シェーダ) が読める SRV 状態へ遷移する。
    void toShaderResource(ID3D12GraphicsCommandList* cl)
    {
        transition(cl, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    /// @brief order を compute が書ける UAV 状態へ遷移する。
    void toUav(ID3D12GraphicsCommandList* cl)
    {
        transition(cl, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ID3D12Resource* orderBuffer() const { return m_order.Get(); }
    UINT            count() const { return m_count; }
    bool            ready() const { return m_ready; }

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool buildPso(ID3D12Device* dev, const char* entry, ComPtr<ID3D12PipelineState>& out)
    {
        ComPtr<ID3DBlob> cs, err;
        if (FAILED(D3DCompile(SPLAT_SORT_CS_HLSL, std::strlen(SPLAT_SORT_CS_HLSL), nullptr,
                nullptr, nullptr, entry, "cs_5_0", 0, 0, cs.GetAddressOf(), err.GetAddressOf())))
        {
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = m_rootSig.Get();
        pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        return SUCCEEDED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(out.GetAddressOf())));
    }

    bool makeUav(ID3D12Device* dev, UINT64 bytes, ComPtr<ID3D12Resource>& out)
    {
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = bytes;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        out.Reset();
        return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(out.GetAddressOf())));
    }

    void uavBarrier(ID3D12GraphicsCommandList* cl)
    {
        D3D12_RESOURCE_BARRIER b[3] = {};
        for (int i = 0; i < 3; ++i) { b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; }
        b[0].UAV.pResource = m_range.Get();
        b[1].UAV.pResource = m_hist.Get();
        b[2].UAV.pResource = m_order.Get();
        cl->ResourceBarrier(3, b);
    }

    void transition(ID3D12GraphicsCommandList* cl, D3D12_RESOURCE_STATES to)
    {
        if (!m_order || !cl || m_orderState == to) { return; }
        D3D12_RESOURCE_BARRIER b = {};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_order.Get();
        b.Transition.StateBefore = m_orderState;
        b.Transition.StateAfter  = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(1, &b);
        m_orderState = to;
    }

    bool                        m_ready = false;
    UINT                        m_count = 0;
    D3D12_RESOURCE_STATES       m_orderState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_psoReset, m_psoRange, m_psoHist, m_psoScan, m_psoScatter;
    ComPtr<ID3D12Resource>      m_order, m_range, m_hist;
};

} // namespace mitiru::render

#endif // _WIN32
