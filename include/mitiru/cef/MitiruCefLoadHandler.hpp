#pragma once

/// @file MitiruCefLoadHandler.hpp
/// @brief ページロードイベントハンドラー

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <atomic>
#include <functional>
#include <string>
#include <string_view>

#include "include/cef_load_handler.h"

namespace mitiru::cef
{

/// @brief ページロードイベントを処理する
/// @details ロード状態を追跡し、ゲームコードからクエリできる。
class MitiruCefLoadHandler final : public CefLoadHandler
{
public:
    MitiruCefLoadHandler() = default;

    // ── 状態クエリ ──────────────────────────────────────────────
    [[nodiscard]] bool isLoading()  const noexcept { return m_loading.load(); }
    [[nodiscard]] bool hasError()   const noexcept { return m_hasError.load(); }
    [[nodiscard]] int  errorCode()  const noexcept { return m_errorCode.load(); }

    /// @brief G-17 hook: OnLoadEnd が呼ばれた時に追加で実行するコールバック
    /// @details SceneTransition の fade-in を OnLoadEnd でトリガするための拡張点。
    ///          1 本だけ保持 (上書き置換)。frame.GetURL() を string_view で渡す。
    void setOnLoadEndCallback(std::function<void(std::string_view /*url*/)> cb)
    {
        m_onLoadEnd = std::move(cb);
    }

    // ── CefLoadHandler ──────────────────────────────────────────
    void OnLoadStart(
        CefRefPtr<CefBrowser>  /*browser*/,
        CefRefPtr<CefFrame>    /*frame*/,
        TransitionType         /*transition_type*/) override
    {
        m_loading  = true;
        m_hasError = false;
    }

    void OnLoadEnd(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame>   frame,
        int                   /*httpStatusCode*/) override
    {
        m_loading = false;
        if (m_onLoadEnd && frame)
        {
            const std::string url = frame->GetURL().ToString();
            m_onLoadEnd(url);
        }
    }

    void OnLoadError(
        CefRefPtr<CefBrowser>  /*browser*/,
        CefRefPtr<CefFrame>    /*frame*/,
        ErrorCode              errorCode,
        const CefString&       /*errorText*/,
        const CefString&       /*failedUrl*/) override
    {
        m_loading   = false;
        m_hasError  = true;
        m_errorCode = static_cast<int>(errorCode);
    }

private:
    std::atomic<bool> m_loading  {false};
    std::atomic<bool> m_hasError {false};
    std::atomic<int>  m_errorCode{0};
    std::function<void(std::string_view)> m_onLoadEnd;   ///< G-17 hook (optional)

    IMPLEMENT_REFCOUNTING(MitiruCefLoadHandler);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
