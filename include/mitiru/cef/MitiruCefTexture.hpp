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

// MITIRU_CEF_NO_DX12DEVICE — Dx12Device を取る便宜オーバーロードを外す。
//
// この UI 層が要るのは ID3D12Device / ID3D12CommandQueue / RTV ハンドルだけで、
// Dx12Device 版はエンジン自身のために置いてある短縮形にすぎない。ところが型を名前で
// 受けている以上ヘッダは include され、Dx12Device は IDevice / Dx12SwapChain /
// Win32Window / sgc まで芋づるで引く。つまり**自前のデバイスを持つ側は、使わない
// スタックを丸ごと通す羽目になる** — API を D3D12 まで下げても include が下がって
// いなければ、分離は名目だけである。
//
// エンジンは何も定義しない (既定で便宜版が付く)。外して使う側だけが宣言する。
#if !defined(MITIRU_CEF_NO_DX12DEVICE)
#include <mitiru/gfx/dx12/Dx12Device.hpp>
#endif
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
    /// @brief 初期化 — D3D12 のハンドルだけを受け取る版
    /// @details こちらが本体で、Dx12Device を取る版はこれを呼ぶ。
    ///
    ///          合成に要るのはデバイスとキューと RTV だけで、スワップチェーンも
    ///          ウィンドウも要らない。それでも Dx12Device を要求すると、この UI 層は
    ///          「エンジンのゲーム用デバイスを持っているもの」にしか使えなくなる。
    ///          姉妹プロジェクトの Makina は自前の DX12 を持っていて UI だけを借りたい
    ///          ので、借りられる形にしてある。実測して 3 箇所だった -- nativeDevice /
    ///          commandQueue / getSwapChain で、最後の 1 つは composite 側にある。
    bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue, int width, int height)
    {
        if (device == nullptr || queue == nullptr)
        {
            std::fprintf(stderr,
                "[mitiru][cef] MitiruCefTexture::initialize: device or queue is null\n");
            return false;
        }
        m_device = device;
        m_queue  = queue;
        resize(width, height);
        createPipeline();
        createCompositeResources();
        m_initialized = true;
        return true;
    }

#if !defined(MITIRU_CEF_NO_DX12DEVICE)
    /// @brief 初期化 — エンジンのデバイスから
    bool initialize(gfx::Dx12Device& device, int width, int height)
    {
        return initialize(device.nativeDevice(), device.commandQueue(), width, height);
    }
#endif

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
#if !defined(MITIRU_CEF_NO_DX12DEVICE)
    void resize(gfx::Dx12Device& /*device*/, int width, int height)
    {
        resize(width, height);
    }
#endif

    /// @brief リサイズ — デバイスを要らない形にしたもの
    /// @details 元から引数の device は使っていなかった。
    void resize(int width, int height)
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
    void upload(const uint8_t* data, int width, int height);

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
        std::span<const CefRect>    dirtyRects);

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
                   const float clearRGBA[4] = nullptr);

    /// @brief 合成を、渡されたコマンドリストに記録する (提出はしない)
    /// @details 自前のフレームループを持つ側のための入口。Reset / Close / 完了待ちは
    ///          呼び出し側の仕事で、ここではしない。
    void recordComposite(ID3D12GraphicsCommandList* cl, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                         int windowW, int windowH, const float clearRGBA[4] = nullptr);

private:
    // ── DX12 リソース作成 (実装本体は detail/MitiruCefTexture_impl.hpp) ──
    void createTexture();

    void createPipeline();

    /// @brief composite() 用の永続コマンドリスト/フェンスを作成する
    void createCompositeResources();

    /// @brief 全画面コピーコマンドを発行して GPU に転送する (upload() の共通後半)
    void issueFullCopy();

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
    void mapWindowToCef(int mx, int my, int& cx, int& cy) const;
};

} // namespace mitiru::cef

// 実装本体（core/Screen.hpp と同じ末尾 detail include 流儀）
#include <mitiru/cef/detail/MitiruCefTexture_impl.hpp>

#endif // _WIN32 && MITIRU_HAS_CEF
