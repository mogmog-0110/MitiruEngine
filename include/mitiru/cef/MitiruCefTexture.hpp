#pragma once

/// @file MitiruCefTexture.hpp
/// @brief CEF BGRA ピクセルを DX12 テクスチャにアップロードし、
///        フルスクリーン合成パス (アルファブレンド) で描画する。
///
/// 使用手順:
///   1. initialize(device, width, height)
///   2. 毎フレーム: if (frame.valid) upload(frame) または uploadPartial(frame)
///   3. 毎フレーム: composite(cmdList, rtvHandle, viewportW, viewportH)
///   4. リサイズ時: resize(device, newW, newH)
///
/// 部分アップロード (uploadPartial):
///   dirty rect リストが空なら no-op。
///   矩形の合計面積がテクスチャ全体の 70% を超える場合はフルアップロードに
///   フォールバックする。それ以下なら各矩形ごとに CopyTextureRegion を発行する。
///
///   D3D12 行ピッチ戦略: 各矩形のピクセルをアップロードバッファの先頭から書く。
///   矩形ごとに offset=0、Footprint.Width/Height = rect サイズ として
///   CopyTextureRegion を呼ぶ。これにより複数矩形の重複オフセット計算が不要になり
///   実装がシンプルになる (clarity > cleverness)。

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/cef/CefUploadPlanner.hpp>   // planUploadPlacements (#29、GPU 非依存の純ロジック)

// CefRect 型を使うために CEF ヘッダーをインクルードする
#include "include/cef_render_handler.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::cef
{

using Microsoft::WRL::ComPtr;

/// @brief 部分アップロードに使う dirty rect 閾値 (テクスチャ面積に対する比率)
/// @details この値を超えると全画面アップロードにフォールバックする
static constexpr float kPartialUploadThreshold = 0.70f;

/// @brief CEF フルスクリーンオーバーレイ (テクスチャ + 合成パイプライン)
class MitiruCefTexture
{
public:
    MitiruCefTexture() = default;

    ~MitiruCefTexture()
    {
        // フェンスイベントハンドルを解放する前に GPU が完了しているか確認
        if (m_compositeEvent)
        {
            if (m_compositeFence &&
                m_compositeFence->GetCompletedValue() < m_compositeFenceValue)
            {
                m_compositeFence->SetEventOnCompletion(m_compositeFenceValue,
                                                        m_compositeEvent);
                WaitForSingleObject(m_compositeEvent, INFINITE);
            }
            CloseHandle(m_compositeEvent);
            m_compositeEvent = nullptr;
        }
    }

    MitiruCefTexture(const MitiruCefTexture&)            = delete;
    MitiruCefTexture& operator=(const MitiruCefTexture&) = delete;

    /// @brief 初期化 — テクスチャと合成パイプラインを作成する
    /// @return 成功したか (失敗は例外でも通知される)
    bool initialize(gfx::Dx12Device& device, int width, int height)
    {
        m_device = device.nativeDevice();
        m_queue  = device.commandQueue();
        resize(device, width, height);
        createPipeline();
        createCompositeResources();
        m_initialized = true;
        return true;
    }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    /// @brief 現在の texture 実 dim (last applied paint dim)。
    /// resize() が pending を立てただけの段階では依然として古い dim を返す。
    /// caller (CefContext::resize の sync pump) はこれを poll して
    /// "新 dim での paint が arrived & applied" を検知する。
    [[nodiscard]] int width()  const noexcept { return m_width;  }
    [[nodiscard]] int height() const noexcept { return m_height; }

    /// @brief テクスチャサイズを変更する (deferred: 実際の recreate は
    ///        upload() が新サイズの paint データを受け取った瞬間に行う)。
    ///
    /// 即時 createTexture() すると、CEF の WasResized → OnPaint
    /// (async) が来る前に composite() が走る → 空の新テクスチャを描画
    /// → UI が一瞬消える。そこで「pending mark を立てるだけ」 にして、
    /// upload() が来た時に「届いた data dim == pending dim?」 をチェックし、
    /// 一致したらその場で createTexture + populate を atomic に実行する。
    /// pending 中は古いテクスチャがそのまま使われ、composite は
    /// 古いサイズの内容を window に bilinear stretch する (一時的ながら
    /// 内容は維持される)。
    void resize(gfx::Dx12Device& /*device*/, int width, int height)
    {
        if (width <= 0 || height <= 0) { return; }
        if (width == m_width && height == m_height && m_texture)
        {
            m_pendingWidth = 0;
            m_pendingHeight = 0;
            return;
        }
        if (!m_texture)
        {
            // 初回 init: deferred 経路を通らず即座に作成する。
            m_width  = width;
            m_height = height;
            m_pendingWidth = 0;
            m_pendingHeight = 0;
            createTexture();
            return;
        }
        m_pendingWidth  = width;
        m_pendingHeight = height;
    }

    /// @brief upload() / uploadPartial() の頭で呼ぶ — 届いた paint data の
    ///        dim と current texture dim が一致しなければ texture を作り直す。
    ///
    /// 元の実装は "pending dim と data dim が一致した時だけ" 作り直していた
    /// が、CEF は drag-resize 中に intermediate dim でも paint を送ってきて
    /// pending と mismatch するケースが多発し、結果 texture が古いまま残って
    /// composite が古い texture を新 window に bilinear stretch する不具合が
    /// 発生していた。**CEF の OnPaint dim を authoritative とみなす** ことで
    /// この問題が構造的に消える (CEF が送ってくる dim = 真の current viewport)。
    /// @return true なら texture を recreate 済み (caller は m_width/m_height を再読み)
    bool applyPendingResize(int dataWidth, int dataHeight)
    {
        if (dataWidth <= 0 || dataHeight <= 0) { return false; }
        if (dataWidth == m_width && dataHeight == m_height && m_texture)
        {
            return false; // 既にこのサイズ
        }
        m_width  = dataWidth;
        m_height = dataHeight;
        m_pendingWidth = 0;
        m_pendingHeight = 0;
        createTexture();
        return true;
    }

    /// @brief MitiruCefRenderHandler::Frame を GPU テクスチャにフルアップロードする
    /// @param data   BGRA バイト列 (width * height * 4 バイト)
    void upload(const uint8_t* data, int width, int height)
    {
        if (!data) { return; }
        // resize が pending で、この paint が新 dim と一致するなら、ここで
        // texture を作り直してから upload を続行する。
        applyPendingResize(width, height);
        if (!m_uploadBuffer || width != m_width || height != m_height)
        {
            return;
        }

        // アップロードヒープにマップして memcpy
        void* mapped = nullptr;
        D3D12_RANGE range{0, 0};
        if (FAILED(m_uploadBuffer->Map(0, &range, &mapped)))
        {
            return;
        }
        const size_t rowBytes = static_cast<size_t>(m_width) * 4;
        for (int y = 0; y < m_height; ++y)
        {
            std::memcpy(
                static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * m_uploadRowPitch,
                data + static_cast<size_t>(y) * rowBytes,
                rowBytes);
        }
        D3D12_RANGE written{0, static_cast<size_t>(m_height) * m_uploadRowPitch};
        m_uploadBuffer->Unmap(0, &written);

        issueFullCopy();
    }

    /// @brief 部分アップロード — dirty rect リストに基づき必要な矩形だけ転送する
    /// @param data       BGRA フルバッファ (CEF は常にフルバッファを渡す)
    /// @param width      バッファ幅
    /// @param height     バッファ高さ
    /// @param dirtyRects 変更された矩形リスト (空なら no-op)
    ///
    /// ### フォールバック条件
    /// dirty rect の合計面積が全体の kPartialUploadThreshold (70%) を超える場合、
    /// フル upload() にフォールバックする (部分コピー複数回より全体 1 回が速い)。
    void uploadPartial(
        const uint8_t*              data,
        int                         width,
        int                         height,
        std::span<const CefRect>    dirtyRects)
    {
        if (dirtyRects.empty())
        {
            return; // 変更なし — 転送しない
        }
        if (!data) { return; }
        // この partial が新 dim と一致するなら pending resize を適用する。
        // resize 時に partial のみの paint が来るのは稀 (CEF は WasResized で
        // 通常フル repaint を送る) だが、correctness のため処理しておく。
        const bool didApply = applyPendingResize(width, height);
        if (!m_uploadBuffer || width != m_width || height != m_height)
        {
            return;
        }
        if (didApply)
        {
            // resize 後の texture は空白 — partial upload だけでは dirty で
            // ない領域が未初期化のまま残る。このケースではフル upload() に
            // fall through する。
            upload(data, width, height);
            return;
        }

        // ── 70% フォールバック判定 ────────────────────────────────
        const int64_t totalArea = static_cast<int64_t>(m_width) * m_height;
        int64_t dirtySum = 0;
        for (const auto& r : dirtyRects)
        {
            dirtySum += static_cast<int64_t>(r.width) * r.height;
        }
        if (dirtySum >= static_cast<int64_t>(
                static_cast<float>(totalArea) * kPartialUploadThreshold))
        {
            upload(data, width, height);
            return;
        }

        const size_t srcStride = static_cast<size_t>(m_width) * 4;

        // ── 各 dirty rect にアップロードバッファ内の独立オフセットを割り当てる (#29) ──
        // 旧実装は全 rect を offset=0 に書いていたため、ループ内の memcpy が前 rect の
        // 転送元ピクセルを GPU 実行 (ループ後の ExecuteCommandLists) 前に上書きし、
        // 全コピーが「最後の rect」を読んでしまった (別位置にゲージ縞が複製)。
        // rect ごとに 512B 整列 (PLACED_FOOTPRINT.Offset) の独立領域へ書き、コピーの
        // src.Offset もそこを指すようにすれば、同一実行内で全コピーが正しい元を読む。
        const size_t bufferSize = m_uploadRowPitch * static_cast<size_t>(m_height);
        bool overflow = false;
        const std::vector<UploadPlacement> placed =
            planUploadPlacements(dirtyRects, m_width, m_height, bufferSize, overflow);
        if (overflow)
        {
            // 整列オーバーヘッドでバッファに収まらない稀ケース: フル転送に退避。
            upload(data, width, height);
            return;
        }
        if (placed.empty()) { return; }

        // ── 全 rect をバッファへ一括書き込み (各 rect は自分の offset へ) ──
        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(m_uploadBuffer->Map(0, &readRange, &mapped))) { return; }
        for (const auto& p : placed)
        {
            const size_t rectRowBytes = static_cast<size_t>(p.width) * 4;
            for (int y = 0; y < p.height; ++y)
            {
                const uint8_t* srcRow = data
                    + static_cast<size_t>(p.y + y) * srcStride
                    + static_cast<size_t>(p.x) * 4;
                uint8_t* dstRow = static_cast<uint8_t*>(mapped) + p.offset
                    + static_cast<size_t>(y) * p.rowPitch;
                std::memcpy(dstRow, srcRow, rectRowBytes);
            }
        }
        const size_t writtenEnd = placed.back().offset
            + placed.back().rowPitch * static_cast<size_t>(placed.back().height);
        D3D12_RANGE written{0, writtenEnd};
        m_uploadBuffer->Unmap(0, &written);

        // ── コピーコマンドリストを準備し、全 rect のコピーを記録する ──
        ComPtr<ID3D12CommandAllocator> alloc;
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&alloc));
        ComPtr<ID3D12GraphicsCommandList> cl;
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            alloc.Get(), nullptr, IID_PPV_ARGS(&cl));

        // Barrier: SHADER_RESOURCE → COPY_DEST (一度だけ発行)
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_texture.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = 0;
        cl->ResourceBarrier(1, &b);

        for (const auto& p : placed)
        {
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource        = m_texture.Get();
            dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource                          = m_uploadBuffer.Get();
            src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset             = p.offset;
            src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_B8G8R8A8_UNORM;
            src.PlacedFootprint.Footprint.Width    = static_cast<UINT>(p.width);
            src.PlacedFootprint.Footprint.Height   = static_cast<UINT>(p.height);
            src.PlacedFootprint.Footprint.Depth    = 1;
            src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(p.rowPitch);

            cl->CopyTextureRegion(&dst, p.x, p.y, 0, &src, nullptr);
        }

        // Barrier: COPY_DEST → SHADER_RESOURCE (一度だけ発行)
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        cl->ResourceBarrier(1, &b);
        cl->Close();

        ID3D12CommandList* lists[] = {cl.Get()};
        m_queue->ExecuteCommandLists(1, lists);
        flushGpu();
    }

    /// @brief バックバッファにフルスクリーン合成する
    /// @details キャッシュ済みコマンドアロケーターを使い回す。
    ///          フレーム開始時にフェンスで前フレームの GPU 完了を確認してから
    ///          アロケーターをリセットする。これにより
    ///          "COMMAND_ALLOCATOR_SYNC" D3D12 エラーを防ぐ。
    /// @param rtvHandle  バックバッファの RTV ハンドル
    /// @param windowW    現在の window dim (composite 表示先)
    /// @param windowH    現在の window dim
    ///
    /// ### aspect 比を保つ fit composite (canonical な UI scaling)
    /// texture の aspect ratio を保ったまま window 内に最大サイズで収まる
    /// 矩形を計算し、その viewport に fullscreen 三角形を描画する。
    /// 余白 (letterbox/pillarbox) には engine clear color (paper amber 等) が見える。
    ///
    /// 利点 (vs. window dim stretch / texture dim 1:1):
    ///  - 必ず content 全体が見える (見切れ無し) ← 端 clip 防止
    ///  - aspect 維持 (滲み無し / 比率歪み無し)
    ///  - 拡大時は texture を sampler で補間 (LINEAR で滑らか)
    ///  - drag 中 deferred resize なら、texture aspect は前 paint のまま
    ///    window が伸縮しても fit-rect が滑らかに追従 → 中身は静止
    void composite(D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, int windowW, int windowH,
                   const float clearRGBA[4] = nullptr)
    {
        if (!m_initialized || !m_pipeline || !m_texture ||
            !m_compositeAlloc || !m_compositeCl)
        {
            return;
        }
        if (windowW <= 0 || windowH <= 0 || m_width <= 0 || m_height <= 0)
        {
            return;
        }

        // 前フレームの GPU 実行完了を待ってからアロケーターをリセット
        if (m_compositeFence->GetCompletedValue() < m_compositeFenceValue)
        {
            m_compositeFence->SetEventOnCompletion(m_compositeFenceValue,
                                                    m_compositeEvent);
            WaitForSingleObject(m_compositeEvent, INFINITE);
        }
        m_compositeAlloc->Reset();
        m_compositeCl->Reset(m_compositeAlloc.Get(), nullptr);

        m_compositeCl->SetGraphicsRootSignature(m_rootSig.Get());
        m_compositeCl->SetPipelineState(m_pipeline.Get());

        // SRV デスクリプタヒープをバインドする
        ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
        m_compositeCl->SetDescriptorHeaps(1, heaps);
        m_compositeCl->SetGraphicsRootDescriptorTable(
            0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

        m_compositeCl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // ── Aspect-fit rect (letterbox/pillarbox 中央配置) ─────────
        // window aspect が texture より wide なら 上下フル + 左右 pad (pillar)
        // window aspect が texture より tall なら 左右フル + 上下 pad (letter)
        const float texAspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        const float winAspect = static_cast<float>(windowW) / static_cast<float>(windowH);
        float fitW, fitH;
        if (winAspect > texAspect)
        {
            // window がより wide → height 基準で fit、左右 pad
            fitH = static_cast<float>(windowH);
            fitW = fitH * texAspect;
        }
        else
        {
            // window がより tall → width 基準で fit、上下 pad
            fitW = static_cast<float>(windowW);
            fitH = fitW / texAspect;
        }
        const float offX = (static_cast<float>(windowW) - fitW) * 0.5f;
        const float offY = (static_cast<float>(windowH) - fitH) * 0.5f;

        // 入力逆変換用に fit-rect を記録 (mapWindowToCef が参照)
        m_fitX = offX;
        m_fitY = offY;
        m_fitW = fitW;
        m_fitH = fitH;

        D3D12_VIEWPORT vp{offX, offY, fitW, fitH, 0.0f, 1.0f};
        D3D12_RECT sci{
            static_cast<LONG>(offX),
            static_cast<LONG>(offY),
            static_cast<LONG>(offX + fitW),
            static_cast<LONG>(offY + fitH)
        };
        // letterbox/pillarbox の余白を clearColor で塗る (fit-rect の外側のみ)。
        // fit-rect は触らないので game の 2D-under-CEF は保持される。余白に
        // backbuffer の素 (黒) が残る不具合 (端の黒帯) を防ぐ。
        if (clearRGBA != nullptr && (offX > 0.5f || offY > 0.5f))
        {
            D3D12_RECT bars[4];
            int nbars = 0;
            if (offY > 0.5f)
            {
                bars[nbars++] = {0, 0, windowW, static_cast<LONG>(offY + 0.5f)};
                bars[nbars++] = {0, static_cast<LONG>(offY + fitH), windowW, windowH};
            }
            if (offX > 0.5f)
            {
                bars[nbars++] = {0, 0, static_cast<LONG>(offX + 0.5f), windowH};
                bars[nbars++] = {static_cast<LONG>(offX + fitW), 0, windowW, windowH};
            }
            m_compositeCl->ClearRenderTargetView(rtvHandle, clearRGBA, nbars, bars);
        }

        m_compositeCl->RSSetViewports(1, &vp);
        m_compositeCl->RSSetScissorRects(1, &sci);

        m_compositeCl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_compositeCl->DrawInstanced(3, 1, 0, 0); // fit 矩形を覆う三角形 (NDC -1..1)
        m_compositeCl->Close();

        ID3D12CommandList* lists[] = {m_compositeCl.Get()};
        m_queue->ExecuteCommandLists(1, lists);

        // フェンスをシグナルして次フレームの待機に使う
        ++m_compositeFenceValue;
        m_queue->Signal(m_compositeFence.Get(), m_compositeFenceValue);
    }

private:
    // ── DX12 リソース作成 ────────────────────────────────────────
    void createTexture()
    {
        // GPU テクスチャ (DEFAULT ヒープ、BGRA)
        D3D12_RESOURCE_DESC td{};
        td.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width              = static_cast<UINT64>(m_width);
        td.Height             = static_cast<UINT>(m_height);
        td.DepthOrArraySize   = 1;
        td.MipLevels          = 1;
        td.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count   = 1;
        td.Flags              = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        m_texture.Reset();
        HRESULT hr = m_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&m_texture));
        if (FAILED(hr))
        {
            throw std::runtime_error("MitiruCefTexture: CreateCommittedResource (texture) failed");
        }

        // SRV を作成する
        if (!m_srvHeap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC dhd{};
            dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            dhd.NumDescriptors = 1;
            dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            m_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&m_srvHeap));
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC svd{};
        svd.Format                        = DXGI_FORMAT_B8G8R8A8_UNORM;
        svd.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        svd.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        svd.Texture2D.MipLevels           = 1;
        m_device->CreateShaderResourceView(m_texture.Get(), &svd,
            m_srvHeap->GetCPUDescriptorHandleForHeapStart());

        // アップロードヒープ (CPU から書き込める)
        m_uploadRowPitch = alignTo256(static_cast<size_t>(m_width) * 4);
        const size_t uploadBytes = m_uploadRowPitch * static_cast<size_t>(m_height);

        D3D12_RESOURCE_DESC ud{};
        ud.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width              = static_cast<UINT64>(uploadBytes);
        ud.Height             = 1;
        ud.DepthOrArraySize   = 1;
        ud.MipLevels          = 1;
        ud.Format             = DXGI_FORMAT_UNKNOWN;
        ud.SampleDesc.Count   = 1;
        ud.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES uhp{};
        uhp.Type = D3D12_HEAP_TYPE_UPLOAD;

        m_uploadBuffer.Reset();
        m_device->CreateCommittedResource(
            &uhp, D3D12_HEAP_FLAG_NONE, &ud,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&m_uploadBuffer));
    }

    void createPipeline()
    {
        // ── ルートシグネチャ ──────────────────────────────────
        // Slot 0: デスクリプタテーブル (SRV t0)
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors                    = 1;
        range.BaseShaderRegister                = 0;
        range.RegisterSpace                     = 0;
        range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges   = &range;
        param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        // LINEAR: aspect-fit composite では drag 中も texture が拡大/縮小
        // されうるため、bilinear で滑らかに sample する (POINT だと縮小時に
        // 残酷なエイリアスが出る)。aspect 維持されているので歪みは出ない。
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister   = 0;
        sampler.RegisterSpace    = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters     = 1;
        rsd.pParameters       = &param;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers   = &sampler;
        rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
            &sigBlob, &errBlob);
        m_device->CreateRootSignature(0,
            sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_rootSig));

        // ── シェーダー ────────────────────────────────────────
        // フルスクリーン三角形: 頂点バッファなし、SV_VertexID だけで座標を計算する
        static const char* kVS = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID)
{
    float2 uv  = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    o.uv  = uv;
    return o;
}
)";
        // CEF は BGRA 出力だが DXGI_FORMAT_B8G8R8A8_UNORM の SRV 経由で
        // サンプリングすると HLSL では正しい RGBA float4 が返る。
        static const char* kPS = R"(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PSIn i) : SV_Target
{
    return tex.Sample(samp, i.uv);
}
)";
        UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL0;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> vsBlob, psBlob, e;
        if (FAILED(D3DCompile(kVS, strlen(kVS), "cef_vs", nullptr, nullptr,
            "main", "vs_5_0", flags, 0, &vsBlob, &e)) || !vsBlob)
        {
            ::OutputDebugStringA("[CEF] VS compile failed: ");
            if (e) ::OutputDebugStringA(static_cast<const char*>(e->GetBufferPointer()));
            ::OutputDebugStringA("\n");
        }
        e.Reset();
        if (FAILED(D3DCompile(kPS, strlen(kPS), "cef_ps", nullptr, nullptr,
            "main", "ps_5_0", flags, 0, &psBlob, &e)) || !psBlob)
        {
            ::OutputDebugStringA("[CEF] PS compile failed: ");
            if (e) ::OutputDebugStringA(static_cast<const char*>(e->GetBufferPointer()));
            ::OutputDebugStringA("\n");
        }

        // ── PSO ──────────────────────────────────────────────
        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        blend.BlendEnable           = TRUE;
        blend.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp               = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha         = D3D12_BLEND_ONE;
        blend.DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        if (!vsBlob || !psBlob)
        {
            ::OutputDebugStringA("[CEF] Shader blob null — pipeline will not be created\n");
            return;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psd{};
        psd.pRootSignature        = m_rootSig.Get();
        psd.VS                    = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
        psd.PS                    = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
        psd.BlendState.RenderTarget[0] = blend;
        psd.SampleMask            = UINT_MAX;
        psd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        psd.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
        psd.RasterizerState.DepthClipEnable       = TRUE;
        psd.DepthStencilState.DepthEnable         = FALSE;
        psd.DepthStencilState.StencilEnable       = FALSE;
        psd.InputLayout                           = {nullptr, 0};
        psd.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psd.NumRenderTargets                      = 1;
        psd.RTVFormats[0]                         = DXGI_FORMAT_R8G8B8A8_UNORM;
        psd.SampleDesc.Count                      = 1;

        if (FAILED(m_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&m_pipeline))))
        {
            ::OutputDebugStringA("[CEF] CreateGraphicsPipelineState failed\n");
        }
        else
        {
            ::OutputDebugStringA("[CEF] Pipeline created OK\n");
        }
    }

    /// @brief composite() 用の永続コマンドリスト/フェンスを作成する
    void createCompositeResources()
    {
        m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_compositeAlloc));

        m_device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_compositeAlloc.Get(), nullptr, IID_PPV_ARGS(&m_compositeCl));
        m_compositeCl->Close(); // 初期状態は closed

        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&m_compositeFence));
        m_compositeFenceValue = 0;
        m_compositeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    /// @brief 全画面コピーコマンドを発行して GPU に転送する (upload() の共通後半)
    void issueFullCopy()
    {
        ComPtr<ID3D12CommandAllocator> alloc;
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&alloc));

        ComPtr<ID3D12GraphicsCommandList> cl;
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            alloc.Get(), nullptr, IID_PPV_ARGS(&cl));

        // Barrier: SHADER_RESOURCE → COPY_DEST
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_texture.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = 0;
        cl->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource        = m_texture.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource                          = m_uploadBuffer.Get();
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = 0;
        src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_B8G8R8A8_UNORM;
        src.PlacedFootprint.Footprint.Width    = static_cast<UINT>(m_width);
        src.PlacedFootprint.Footprint.Height   = static_cast<UINT>(m_height);
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(m_uploadRowPitch);

        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // Barrier: COPY_DEST → SHADER_RESOURCE
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        cl->ResourceBarrier(1, &b);
        cl->Close();

        ID3D12CommandList* lists[] = {cl.Get()};
        m_queue->ExecuteCommandLists(1, lists);
        flushGpu();
    }

    void flushGpu()
    {
        ComPtr<ID3D12Fence> fence;
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, ev);
        WaitForSingleObject(ev, INFINITE);
        CloseHandle(ev);
    }

    static size_t alignTo256(size_t size) noexcept
    {
        return (size + 255) & ~size_t{255};
    }


    // ── メンバー ─────────────────────────────────────────────────
    ID3D12Device*        m_device = nullptr;
    ID3D12CommandQueue*  m_queue  = nullptr;

    ComPtr<ID3D12Resource>         m_texture;
    ComPtr<ID3D12Resource>         m_uploadBuffer;
    ComPtr<ID3D12DescriptorHeap>   m_srvHeap;
    ComPtr<ID3D12RootSignature>    m_rootSig;
    ComPtr<ID3D12PipelineState>    m_pipeline;

    // composite() 用永続コマンドリスト — フェンスで GPU 完了を追跡して
    // アロケーターを安全にリセットする (COMMAND_ALLOCATOR_SYNC 回避)
    ComPtr<ID3D12CommandAllocator>      m_compositeAlloc;
    ComPtr<ID3D12GraphicsCommandList>   m_compositeCl;
    ComPtr<ID3D12Fence>                 m_compositeFence;
    UINT64                              m_compositeFenceValue = 0;
    HANDLE                              m_compositeEvent = nullptr;

    size_t m_uploadRowPitch = 0;
    int    m_width          = 0;
    int    m_height         = 0;
    // pending resize: resize() で予告された目標 dim だが、まだ実現していない
    // (CEF OnPaint から一致する paint data が来るのを待っている)。
    int    m_pendingWidth   = 0;
    int    m_pendingHeight  = 0;
    bool   m_initialized    = false;

    // ── 直近の composite fit-rect (window 座標系) ─────────────────────
    // composite() で計算した letterbox/pillarbox 矩形を保持する。
    // 入力経路 (mapWindowToCef) がこれを使って window 座標を CEF 論理座標に
    // 逆変換する。これを欠くと texture dim != window dim の時に click 位置と
    // HTML layout 位置がズレる (= ボタン当たり判定バグ)。
    float  m_fitX = 0.0f;
    float  m_fitY = 0.0f;
    float  m_fitW = 0.0f;
    float  m_fitH = 0.0f;

public:
    /// @brief window 座標 (mx,my) を CEF 論理座標 (cx,cy) に逆変換する
    /// @details composite() の fit-rect を逆に適用。fit-rect 未計算時 (初回
    ///          frame 前) は恒等で返す。letterbox 余白を click した場合は
    ///          [0,texDim] にクランプ (CEF へは端の座標を渡す)。
    void mapWindowToCef(int mx, int my, int& cx, int& cy) const
    {
        if (m_fitW <= 0.0f || m_fitH <= 0.0f || m_width <= 0 || m_height <= 0)
        {
            cx = mx;
            cy = my;
            return;
        }
        const float nx = (static_cast<float>(mx) - m_fitX) / m_fitW; // 0..1
        const float ny = (static_cast<float>(my) - m_fitY) / m_fitH;
        float lx = nx * static_cast<float>(m_width);
        float ly = ny * static_cast<float>(m_height);
        if (lx < 0.0f) { lx = 0.0f; }
        if (ly < 0.0f) { ly = 0.0f; }
        if (lx > static_cast<float>(m_width))  { lx = static_cast<float>(m_width); }
        if (ly > static_cast<float>(m_height)) { ly = static_cast<float>(m_height); }
        cx = static_cast<int>(lx);
        cy = static_cast<int>(ly);
    }
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
