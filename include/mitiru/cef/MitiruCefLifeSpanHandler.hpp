#pragma once

/// @file MitiruCefLifeSpanHandler.hpp
/// @brief ブラウザライフスパンハンドラー

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include "include/cef_life_span_handler.h"

namespace mitiru::cef
{

/// @brief ブラウザの生成・破棄イベントを管理する
class MitiruCefLifeSpanHandler final : public CefLifeSpanHandler
{
public:
    MitiruCefLifeSpanHandler() = default;

    /// @brief 生成済みブラウザを取得する (null = まだ生成されていない)
    [[nodiscard]] CefRefPtr<CefBrowser> browser() const { return m_browser; }

    /// @brief ブラウザが生成済みかどうか
    [[nodiscard]] bool isCreated() const noexcept { return m_browser != nullptr; }

    // ── CefLifeSpanHandler ──────────────────────────────────────
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        m_browser = browser;
    }

    bool DoClose(CefRefPtr<CefBrowser> /*browser*/) override
    {
        // false を返してデフォルトのウィンドウ破棄処理を許可する
        // (OSR モードではウィンドウがないので常に false でよい)
        return false;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) override
    {
        m_browser = nullptr;
    }

private:
    CefRefPtr<CefBrowser> m_browser;

    IMPLEMENT_REFCOUNTING(MitiruCefLifeSpanHandler);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
