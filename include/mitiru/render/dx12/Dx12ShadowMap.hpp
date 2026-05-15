#pragma once

#ifdef _WIN32

/// @file Dx12ShadowMap.hpp
/// @brief DX12 バックエンド用シャドウマップ GPU リソース
/// @details
///   深度バッファ (DSV) と SRV を管理するヘッダオンリークラス。
///   シャドウパスでは DSV のみ bind し、通常パスでは PIXEL_SHADER_RESOURCE として
///   深度テクスチャをサンプリングできる状態に遷移する。
///
///   使用例:
///   @code
///   mitiru::render::dx12::Dx12ShadowMap shadowMap;
///   shadowMap.initialize(device, 1024);
///
///   // --- シャドウパス ---
///   shadowMap.beginShadowPass(cmdList);
///   // シャドウ描画
///   shadowMap.endShadowPass(cmdList);
///
///   // --- 通常パス (SRV としてバインド) ---
///   ID3D12DescriptorHeap* heaps[] = { srvHeap };
///   cmdList->SetDescriptorHeaps(1, heaps);
///   // ... root descriptor table に shadowMap.srvHandle() を渡す
///   @endcode

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace mitiru::render::dx12
{

/// @brief DX12 指向性シャドウマップ GPU リソース
/// @details
///   - フォーマット: DXGI_FORMAT_D32_FLOAT (DSV) / DXGI_FORMAT_R32_FLOAT (SRV)
///   - 初期化後は isInitialized() で状態を確認すること
///   - beginShadowPass / endShadowPass を必ずペアで呼ぶこと
class Dx12ShadowMap
{
public:
    /// @brief デフォルトコンストラクタ
    Dx12ShadowMap() noexcept = default;

    /// @brief デストラクタ (destroy を自動呼出し)
    ~Dx12ShadowMap() noexcept { destroy(); }

    // コピー禁止 (GPU リソースはムーブのみ)
    Dx12ShadowMap(const Dx12ShadowMap&) = delete;
    Dx12ShadowMap& operator=(const Dx12ShadowMap&) = delete;

    Dx12ShadowMap(Dx12ShadowMap&&) noexcept = default;
    Dx12ShadowMap& operator=(Dx12ShadowMap&&) noexcept = default;

    /// @brief GPU リソースを初期化する
    /// @param device  DX12 デバイスポインタ
    /// @param mapSize シャドウマップの一辺ピクセル数 (例: 1024)
    /// @return 成功時 true
    bool initialize(ID3D12Device* device, int mapSize)
    {
        if (!device || mapSize <= 0) { return false; }

        m_mapSize = mapSize;
        const auto sz = static_cast<UINT>(mapSize);

        // ── 深度テクスチャ ─────────────────────────────────────
        {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width            = sz;
            desc.Height           = sz;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = DXGI_FORMAT_R32_TYPELESS;
            desc.SampleDesc.Count = 1;
            desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE clear{};
            clear.Format               = DXGI_FORMAT_D32_FLOAT;
            clear.DepthStencil.Depth   = 1.0f;
            clear.DepthStencil.Stencil = 0;

            m_depthTex.Reset();
            if (FAILED(device->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    &clear,
                    IID_PPV_ARGS(m_depthTex.GetAddressOf()))))
            {
                return false;
            }
        }

        // ── DSV ヒープ & DSV ──────────────────────────────────
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd{};
            hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            hd.NumDescriptors = 1;
            hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            m_dsvHeap.Reset();
            if (FAILED(device->CreateDescriptorHeap(
                    &hd, IID_PPV_ARGS(m_dsvHeap.GetAddressOf()))))
            {
                return false;
            }

            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format        = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device->CreateDepthStencilView(
                m_depthTex.Get(), &dsv,
                m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        }

        // ── SRV ヒープ & SRV ──────────────────────────────────
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd{};
            hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.NumDescriptors = 1;
            hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            m_srvHeap.Reset();
            if (FAILED(device->CreateDescriptorHeap(
                    &hd, IID_PPV_ARGS(m_srvHeap.GetAddressOf()))))
            {
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format                    = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels       = 1;
            srv.Texture2D.MostDetailedMip = 0;
            device->CreateShaderResourceView(
                m_depthTex.Get(), &srv,
                m_srvHeap->GetCPUDescriptorHandleForHeapStart());
        }

        m_initialized = true;
        return true;
    }

    /// @brief GPU リソースを解放する
    void destroy() noexcept
    {
        m_srvHeap.Reset();
        m_dsvHeap.Reset();
        m_depthTex.Reset();
        m_initialized = false;
        m_mapSize      = 0;
    }

    /// @brief シャドウパスを開始する
    /// @details
    ///   深度テクスチャを PIXEL_SHADER_RESOURCE → DEPTH_WRITE へ遷移し、
    ///   DSV をクリアして bind する。RT は bind しない (depth-only pass)。
    /// @param cmd 記録中のコマンドリスト
    void beginShadowPass(ID3D12GraphicsCommandList* cmd)
    {
        if (!cmd || !m_initialized) { return; }

        // PSR → DEPTH_WRITE
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = m_depthTex.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &barrier);

        auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        const auto sz = static_cast<float>(m_mapSize);
        D3D12_VIEWPORT vp{ 0.0f, 0.0f, sz, sz, 0.0f, 1.0f };
        D3D12_RECT     sr{ 0, 0, m_mapSize, m_mapSize };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sr);
    }

    /// @brief シャドウパスを終了する
    /// @details
    ///   深度テクスチャを DEPTH_WRITE → PIXEL_SHADER_RESOURCE へ遷移する。
    /// @param cmd 記録中のコマンドリスト
    void endShadowPass(ID3D12GraphicsCommandList* cmd)
    {
        if (!cmd || !m_initialized) { return; }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = m_depthTex.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &barrier);
    }

    /// @brief SRV の GPU ディスクリプタハンドルを返す
    /// @return シェーダーへバインドするための GPU ハンドル
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE srvHandle() const noexcept
    {
        if (!m_srvHeap) { return {}; }
        return m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    }

    /// @brief 深度テクスチャ ID3D12Resource を返す（外部からの CreateShaderResourceView 用）
    [[nodiscard]] ID3D12Resource* nativeResource() const noexcept { return m_depthTex.Get(); }

    /// @brief シャドウマップの一辺ピクセル数を返す
    [[nodiscard]] int mapSize() const noexcept { return m_mapSize; }

    /// @brief GPU リソースが初期化済みかどうかを返す
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

private:
    ComPtr<ID3D12Resource>       m_depthTex;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    int                          m_mapSize    = 0;
    bool                         m_initialized = false;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
