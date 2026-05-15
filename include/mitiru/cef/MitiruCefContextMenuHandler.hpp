#pragma once

/// @file MitiruCefContextMenuHandler.hpp
/// @brief 右クリックコンテキストメニューを無効化するハンドラー

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include "include/cef_context_menu_handler.h"

namespace mitiru::cef
{

/// @brief コンテキストメニューを無効化する
/// @details ゲームの UI ではブラウザの右クリックメニューは不要。
class MitiruCefContextMenuHandler final : public CefContextMenuHandler
{
public:
    MitiruCefContextMenuHandler() = default;

    // ── CefContextMenuHandler ───────────────────────────────────
    void OnBeforeContextMenu(
        CefRefPtr<CefBrowser>       /*browser*/,
        CefRefPtr<CefFrame>         /*frame*/,
        CefRefPtr<CefContextMenuParams> /*params*/,
        CefRefPtr<CefMenuModel>     model) override
    {
        // メニューモデルを空にすることでコンテキストメニューを表示しない
        model->Clear();
    }

private:
    IMPLEMENT_REFCOUNTING(MitiruCefContextMenuHandler);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
