#pragma once

/// @file Dx12ComputeContext.hpp
/// @brief DX12 コンピュートシェーダー実行コンテキスト
/// @details グラフィクスパイプラインのみだった DX12 バックエンドに
///          コンピュートシェーダーパスを追加する軽量ラッパー。
///
///          外部から提供されたコマンドリスト上に Dispatch を記録する設計で、
///          独自のコマンドキューやフェンスは持たない。
///
/// @code
/// // 基本的な使い方
/// Dx12ComputeContext ctx;
/// ctx.initialize(device);
/// ctx.setShader(hlslStr, "CSMain");
/// ctx.setCommandList(cmdList);
/// ctx.setRootCBV(0, cbuffer.GetGPUVirtualAddress());
/// ctx.setRootUAVTable(uavHeap->GetGPUDescriptorHandleForHeapStart());
/// ctx.dispatch(64, 1, 1);
/// @endcode

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::render::dx12
{

/// @brief DX12 コンピュートシェーダー実行コンテキスト
/// @details ルートシグネチャの構成:
///   - スロット 0..3 : ルート CBV (b0..b3)
///   - スロット 4    : SRV ディスクリプタテーブル (t0..t3, 4 エントリ)
///   - スロット 5    : UAV ディスクリプタテーブル (u0..u3, 4 エントリ)
///
///   ディスクリプタヒープの確保と遷移バリアは呼び出し元の責任。
class Dx12ComputeContext
{
public:
    /// @brief ComPtr エイリアス
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    /// @brief ルート CBV スロット数
    static constexpr UINT kCbvCount = 4u;

    /// @brief SRV / UAV テーブル内のディスクリプタ数
    static constexpr UINT kSrvCount = 4u;
    static constexpr UINT kUavCount = 4u;

    /// @brief ルートパラメータのインデックス定数
    static constexpr UINT kRootIdxCbv0   = 0u;  ///< b0
    static constexpr UINT kRootIdxCbv1   = 1u;  ///< b1
    static constexpr UINT kRootIdxCbv2   = 2u;  ///< b2
    static constexpr UINT kRootIdxCbv3   = 3u;  ///< b3
    static constexpr UINT kRootIdxSrvTbl = 4u;  ///< SRV テーブル
    static constexpr UINT kRootIdxUavTbl = 5u;  ///< UAV テーブル

    Dx12ComputeContext() = default;
    ~Dx12ComputeContext() { destroy(); }

    // コピー禁止、ムーブ許可
    Dx12ComputeContext(const Dx12ComputeContext&) = delete;
    Dx12ComputeContext& operator=(const Dx12ComputeContext&) = delete;
    Dx12ComputeContext(Dx12ComputeContext&&) = default;
    Dx12ComputeContext& operator=(Dx12ComputeContext&&) = default;

    /// @brief デバイスを受け取り汎用コンピュートルートシグネチャを構築する
    /// @param device 有効な ID3D12Device ポインタ（null の場合は false を返す）
    /// @return 成功時 true
    bool initialize(ID3D12Device* device)
    {
        if (!device) return false;
        m_device = device;
        return buildRootSignature();
    }

    /// @brief HLSL 文字列からコンピュートシェーダーをコンパイルし PSO を生成する
    /// @details 前回と同じソースハッシュであれば再コンパイルをスキップする。
    /// @param hlsl HLSL ソース文字列
    /// @param entryPoint エントリーポイント名（デフォルト "CSMain"）
    /// @return 成功時 true
    bool setShader(const char* hlsl, const char* entryPoint = "CSMain")
    {
        if (!m_device || !m_rootSig) return false;
        if (!hlsl || !entryPoint) return false;

        const std::size_t newHash = hashString(hlsl);
        if (m_shaderHash == newHash && m_pso) return true;

        ComPtr<ID3DBlob> csBlob;
        ComPtr<ID3DBlob> errBlob;

        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        const HRESULT hr = D3DCompile(
            hlsl, std::strlen(hlsl),
            nullptr, nullptr, nullptr,
            entryPoint, "cs_5_0",
            flags, 0,
            csBlob.GetAddressOf(),
            errBlob.GetAddressOf());

        if (FAILED(hr)) return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_rootSig.Get();
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength  = csBlob->GetBufferSize();

        ComPtr<ID3D12PipelineState> newPso;
        if (FAILED(m_device->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(newPso.GetAddressOf()))))
        {
            return false;
        }

        m_pso        = std::move(newPso);
        m_shaderHash = newHash;
        return true;
    }

    /// @brief Dispatch を記録するコマンドリストを設定する
    /// @param cmdList recording 中の ID3D12GraphicsCommandList（null 可 — dispatch 時にエラー）
    void setCommandList(ID3D12GraphicsCommandList* cmdList)
    {
        m_cmdList = cmdList;
    }

    /// @brief ルート CBV を設定する
    /// @param slot 0..3 (b0..b3 に対応)
    /// @param gpuAddr GPU 仮想アドレス（256 バイトアライン済みであること）
    void setRootCBV(UINT slot, D3D12_GPU_VIRTUAL_ADDRESS gpuAddr)
    {
        if (slot >= kCbvCount) return;
        m_cbvAddrs[slot]     = gpuAddr;
        m_cbvSet[slot]       = true;
    }

    /// @brief SRV ディスクリプタテーブルのベースハンドルを設定する
    /// @param baseHandle シェーダー可視ヒープ上の先頭 GPU ハンドル
    void setRootSRVTable(D3D12_GPU_DESCRIPTOR_HANDLE baseHandle)
    {
        m_srvTableHandle  = baseHandle;
        m_srvTableSet     = true;
    }

    /// @brief UAV ディスクリプタテーブルのベースハンドルを設定する
    /// @param baseHandle シェーダー可視ヒープ上の先頭 GPU ハンドル
    void setRootUAVTable(D3D12_GPU_DESCRIPTOR_HANDLE baseHandle)
    {
        m_uavTableHandle  = baseHandle;
        m_uavTableSet     = true;
    }

    /// @brief コマンドリストに Dispatch を記録する
    /// @details 設定済みのルートシグネチャ・PSO・バインディングを先に記録してから
    ///          ID3D12GraphicsCommandList::Dispatch を呼ぶ。
    /// @param groupsX X 方向スレッドグループ数
    /// @param groupsY Y 方向スレッドグループ数
    /// @param groupsZ Z 方向スレッドグループ数
    /// @return 成功時 true
    bool dispatch(UINT groupsX, UINT groupsY, UINT groupsZ)
    {
        if (!m_cmdList || !m_rootSig || !m_pso) return false;

        m_cmdList->SetComputeRootSignature(m_rootSig.Get());
        m_cmdList->SetPipelineState(m_pso.Get());

        for (UINT i = 0; i < kCbvCount; ++i)
        {
            if (m_cbvSet[i])
            {
                m_cmdList->SetComputeRootConstantBufferView(i, m_cbvAddrs[i]);
            }
        }

        if (m_srvTableSet)
        {
            m_cmdList->SetComputeRootDescriptorTable(kRootIdxSrvTbl, m_srvTableHandle);
        }

        if (m_uavTableSet)
        {
            m_cmdList->SetComputeRootDescriptorTable(kRootIdxUavTbl, m_uavTableHandle);
        }

        m_cmdList->Dispatch(groupsX, groupsY, groupsZ);
        return true;
    }

    /// @brief すべての GPU リソースを解放し未初期化状態に戻す
    void destroy()
    {
        m_pso.Reset();
        m_rootSig.Reset();
        m_device     = nullptr;
        m_cmdList    = nullptr;
        m_shaderHash = 0;
        m_srvTableSet = false;
        m_uavTableSet = false;
        for (UINT i = 0; i < kCbvCount; ++i)
        {
            m_cbvSet[i]   = false;
            m_cbvAddrs[i] = 0;
        }
    }

    // ── 状態クエリ ────────────────────────────────────────────────────────

    /// @brief initialize() 済みかどうか
    [[nodiscard]] bool isInitialized() const noexcept { return m_device != nullptr; }

    /// @brief setShader() 成功済みかどうか
    [[nodiscard]] bool hasPso() const noexcept { return m_pso != nullptr; }

    /// @brief 現在のシェーダーソースハッシュ（0 = 未コンパイル）
    [[nodiscard]] std::size_t shaderHash() const noexcept { return m_shaderHash; }

private:
    // ── 内部ヘルパー ─────────────────────────────────────────────────────

    /// @brief 汎用コンピュートルートシグネチャを構築する
    /// @details 構成: CBV x4 (root descriptor) + SRV table + UAV table
    bool buildRootSignature()
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = kSrvCount;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors                    = kUavCount;
        uavRange.BaseShaderRegister                = 0;
        uavRange.RegisterSpace                     = 0;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[6] = {};

        // CBV 0..3 — ルートディスクリプタ（ヒープ不要、直接 GPU アドレス）
        for (UINT i = 0; i < kCbvCount; ++i)
        {
            params[i].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[i].Descriptor.ShaderRegister = i;
            params[i].Descriptor.RegisterSpace  = 0;
            params[i].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        }

        // SRV テーブル (スロット 4)
        params[kRootIdxSrvTbl].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootIdxSrvTbl].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootIdxSrvTbl].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[kRootIdxSrvTbl].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        // UAV テーブル (スロット 5)
        params[kRootIdxUavTbl].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootIdxUavTbl].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootIdxUavTbl].DescriptorTable.pDescriptorRanges   = &uavRange;
        params[kRootIdxUavTbl].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters     = 6;
        rsd.pParameters       = params;
        rsd.NumStaticSamplers = 0;
        rsd.pStaticSamplers   = nullptr;
        rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errBlob;
        if (FAILED(D3D12SerializeRootSignature(
                &rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                sigBlob.GetAddressOf(), errBlob.GetAddressOf())))
        {
            return false;
        }

        if (FAILED(m_device->CreateRootSignature(
                0,
                sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                IID_PPV_ARGS(m_rootSig.GetAddressOf()))))
        {
            return false;
        }

        return true;
    }

    /// @brief FNV-1a 64 bit でヌル終端文字列をハッシュする
    [[nodiscard]] static std::size_t hashString(const char* str) noexcept
    {
        constexpr std::size_t kFnvOffset = 14695981039346656037ull;
        constexpr std::size_t kFnvPrime  = 1099511628211ull;
        std::size_t h = kFnvOffset;
        while (*str)
        {
            h ^= static_cast<std::size_t>(static_cast<unsigned char>(*str++));
            h *= kFnvPrime;
        }
        return h;
    }

    // ── メンバー ──────────────────────────────────────────────────────────

    ID3D12Device*                m_device   = nullptr;    ///< 借用ポインタ（所有しない）
    ID3D12GraphicsCommandList*   m_cmdList  = nullptr;    ///< 借用ポインタ（所有しない）
    ComPtr<ID3D12RootSignature>  m_rootSig;               ///< 汎用コンピュートルートシグネチャ
    ComPtr<ID3D12PipelineState>  m_pso;                   ///< コンピュート PSO

    std::size_t m_shaderHash = 0;                         ///< 最後にコンパイルしたソースのハッシュ

    D3D12_GPU_VIRTUAL_ADDRESS m_cbvAddrs[kCbvCount] = {}; ///< ルート CBV GPU アドレス
    bool                      m_cbvSet[kCbvCount]   = {}; ///< 各スロットが設定済みか

    D3D12_GPU_DESCRIPTOR_HANDLE m_srvTableHandle = {};    ///< SRV テーブルベースハンドル
    bool                        m_srvTableSet    = false; ///< SRV テーブル設定済みフラグ

    D3D12_GPU_DESCRIPTOR_HANDLE m_uavTableHandle = {};    ///< UAV テーブルベースハンドル
    bool                        m_uavTableSet    = false; ///< UAV テーブル設定済みフラグ
};

} // namespace mitiru::render::dx12

#endif // _WIN32
