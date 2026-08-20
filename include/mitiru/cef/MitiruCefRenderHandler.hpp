#pragma once

/// @file MitiruCefRenderHandler.hpp
/// @brief CEF OSR 描画コールバック — BGRA バッファをスレッドセーフに転送する
///
/// CEF は描画更新時に OnPaint() を CEF UI スレッドから呼ぶ。
/// ゲームレンダリングスレッドは毎フレーム takePixels() で最新バッファを取り出す。
/// 二重バッファ + mutex によりスレッド間競合を防ぐ。
///
/// パフォーマンス最適化:
///   - dirtyRects を蓄積し Frame に同梱する (部分アップロードに使う)
///   - 描画統計カウンターを atomic で保持する (ロックフリー)

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "include/cef_render_handler.h"

namespace mitiru::cef
{

/// @brief CEF オフスクリーンレンダリングのピクセル受信
class MitiruCefRenderHandler final : public CefRenderHandler
{
public:
    /// @brief ビューポートサイズを設定する
    /// @details front/back 両バッファを初期化する。
    ///          takePixels() が swap した後 m_back が空になる問題を防ぐ。
    void setSize(int width, int height)
    {
        std::lock_guard lock(m_mutex);
        m_width  = width;
        m_height = height;
        const size_t bytes = static_cast<size_t>(width) * height * 4;
        m_back.assign(bytes, 0);
        m_front.assign(bytes, 0);
        m_backDirtyRects.clear();
        m_frontDirtyRects.clear();
        m_dirty = false;
        m_everPainted = false;
    }

    int width()  const noexcept { return m_width;  }
    int height() const noexcept { return m_height; }

    /// @brief 新しいフレームが届いているか (ゲームスレッドから呼ぶ)
    [[nodiscard]] bool isDirty() const noexcept { return m_dirty.load(); }

    /// @brief OnPaint が一度でも呼ばれたか — ブラウザ準備完了の判定に使う
    [[nodiscard]] bool hasEverPainted() const noexcept { return m_everPainted.load(); }

    /// @brief 最新ピクセルと蓄積 dirty rect リストを front バッファにスワップして返す
    /// @details dirty フラグがリセットされる。バッファが空なら empty な Frame を返す。
    ///          呼び出し後 front バッファの内容は次回 takePixels() まで有効。
    ///
    /// ## dirtyRects の意味
    /// 前回 takePixels() 以降に OnPaint が呼ばれた回数分の矩形をすべて含む。
    /// CEF は OSR モードで常にフルバッファを渡すため、pixels 自体は常に完全。
    /// dirtyRects は「どこが変わったか」のヒントとして部分アップロードに使う。
    struct Frame
    {
        const uint8_t*       data        = nullptr;
        int                  width       = 0;
        int                  height      = 0;
        bool                 valid       = false;
        /// @brief 前回 takePixels() 以降に変更された矩形の集合 (蓄積済み)
        /// empty ならフルアップロードにフォールバックすること。
        std::vector<CefRect> dirtyRects;
    };

    Frame takePixels()
    {
        if (!m_dirty.load())
        {
            return {};
        }
        std::lock_guard lock(m_mutex);
        std::swap(m_front, m_back);
        std::swap(m_frontDirtyRects, m_backDirtyRects);
        // back は次回 OnPaint のために空にしておく
        m_backDirtyRects.clear();
        m_dirty = false;
        if (m_front.empty())
        {
            return {};
        }
        Frame f;
        f.data       = m_front.data();
        f.width      = m_width;
        f.height     = m_height;
        f.valid      = true;
        f.dirtyRects = m_frontDirtyRects; // コピー (front バッファが有効な間だけ必要)
        return f;
    }

    // ── CefRenderHandler ────────────────────────────────────────
    void GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) override
    {
        rect.x      = 0;
        rect.y      = 0;
        rect.width  = m_width;
        rect.height = m_height;
    }

    /// @brief 描画完了コールバック (CEF UI スレッドから呼ばれる)
    /// @param type        PET_VIEW (メイン) または PET_POPUP
    /// @param dirtyRects  更新された矩形リスト (蓄積して Frame に同梱する)
    /// @param buffer      BGRA バッファ (width * height * 4 バイト、常にフル)
    void OnPaint(
        CefRefPtr<CefBrowser>   /*browser*/,
        PaintElementType        type,
        const RectList&         dirtyRects,
        const void*             buffer,
        int                     width,
        int                     height) override
    {
        if (type != PET_VIEW)
        {
            return; // PET_POPUP は未対応 (ゲームではポップアップ不要)
        }

        // ── 描画統計 (ロックフリー) ─────────────────────────────
        const auto t0 = std::chrono::high_resolution_clock::now();

        // dirty 面積を集計する (スクリーン全体を超えないようクランプ)
        const int64_t viewArea = static_cast<int64_t>(width) * height;
        int64_t dirtyArea = 0;
        for (const auto& r : dirtyRects)
        {
            dirtyArea += static_cast<int64_t>(r.width) * r.height;
        }
        dirtyArea = (dirtyArea < viewArea) ? dirtyArea : viewArea;
        m_lastDirtyArea.store(static_cast<uint64_t>(dirtyArea));
        m_lastPaintBytes.store(static_cast<uint64_t>(dirtyArea) * 4u);

        if (!m_everPainted)
        {
            char dbg[128];
            std::snprintf(dbg, sizeof(dbg),
                "[CEF] OnPaint first call %dx%d\n", width, height);
            ::OutputDebugStringA(dbg);
        }
        m_everPainted = true;

        const size_t bytes = static_cast<size_t>(width) * height * 4;

        {
            std::lock_guard lock(m_mutex);
            // サイズ不一致 or takePixels() swap 後に m_back が空になった場合を両方ガード
            if (m_width != width || m_height != height || m_back.size() < bytes)
            {
                m_width  = width;
                m_height = height;
                m_back.assign(bytes, 0);
                // サイズが変わったら蓄積 rect を捨てる (旧座標系は無効)
                m_backDirtyRects.clear();
            }
            std::memcpy(m_back.data(), buffer, bytes);

            // dirty rect を蓄積する
            // 複数の OnPaint が 1 フレームに来ても、buffer は常に最新のフルバッファ。
            // rect は「takePixels() 以降にどこが変わったか」のヒントとして追記する。
            for (const auto& r : dirtyRects)
            {
                m_backDirtyRects.push_back(r);
            }

            m_dirty = true;
        }

        // ── 統計カウンター更新 (ロック外でよい) ─────────────────
        const auto t1 = std::chrono::high_resolution_clock::now();
        const uint64_t ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        ++m_paintCount;
        m_totalPaintNanos.fetch_add(ns);
    }

    bool GetScreenInfo(CefRefPtr<CefBrowser> /*browser*/, CefScreenInfo& info) override
    {
        info.device_scale_factor = 1.0f;
        info.depth               = 32;
        info.depth_per_component = 8;
        info.is_monochrome       = false;
        info.rect                = {0, 0, m_width, m_height};
        info.available_rect      = info.rect;
        return true;
    }

    // ── 描画統計アクセサー ────────────────────────────────────────
    /// @brief OnPaint が呼ばれた累計回数
    [[nodiscard]] uint64_t paintCount()      const noexcept { return m_paintCount.load(); }
    /// @brief OnPaint の累計処理時間 (ナノ秒)
    [[nodiscard]] uint64_t totalPaintNanos() const noexcept { return m_totalPaintNanos.load(); }
    /// @brief 直近 OnPaint で触れたバイト数 (dirty area * 4)
    [[nodiscard]] uint64_t lastPaintBytes()  const noexcept { return m_lastPaintBytes.load(); }

    /// @brief CEF 論理座標 (x,y) のアルファ値。ページが無ければ 0。
    /// @details ホストは「ポインタが UI の上か、素通しの穴の上か」を知る必要がある。
    ///          板の位置を C++ 側に写すとレイアウトと二重管理になり、パネルを 1 枚
    ///          足した日にズレる。**ページ自身の不透明度が答え**であって、それは
    ///          ここにある -- OSR は CPU バッファに描くので、追加のリードバックは要らない。
    ///
    ///          front を見るのは takePixels() が swap した後の「いま画面に出ている絵」が
    ///          そちらだからで、まだ 1 度も paint していない間は 0 (= どこも UI でない)。
    [[nodiscard]] std::uint8_t alphaAt(int x, int y) const
    {
        std::lock_guard lock(m_mutex);
        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
        {
            return 0;
        }
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width)
                                   + static_cast<std::size_t>(x)) * 4u + 3u;  // BGRA の A
        if (index >= m_front.size())
        {
            return 0;
        }
        return m_front[index];
    }
    /// @brief 直近 OnPaint の dirty ピクセル面積
    [[nodiscard]] uint64_t lastDirtyArea()   const noexcept { return m_lastDirtyArea.load(); }

private:
    mutable std::mutex    m_mutex;
    std::vector<uint8_t>  m_front;           ///< ゲームスレッドが読む
    std::vector<uint8_t>  m_back;            ///< CEF スレッドが書く
    std::vector<CefRect>  m_frontDirtyRects; ///< front に対応する dirty rect リスト
    std::vector<CefRect>  m_backDirtyRects;  ///< back に蓄積中の dirty rect リスト
    std::atomic<bool>     m_dirty{false};
    std::atomic<bool>     m_everPainted{false};
    int m_width  = 1920;
    int m_height = 1080;

    // ── 描画統計カウンター (ロックフリー) ─────────────────────────
    std::atomic<uint64_t> m_paintCount{0};
    std::atomic<uint64_t> m_totalPaintNanos{0};
    std::atomic<uint64_t> m_lastPaintBytes{0};
    std::atomic<uint64_t> m_lastDirtyArea{0};

    IMPLEMENT_REFCOUNTING(MitiruCefRenderHandler);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
