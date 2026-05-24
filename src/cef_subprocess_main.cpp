/// @file cef_subprocess_main.cpp
/// @brief CEF サブプロセスエントリーポイント (ゲーム側 *Helper.exe にリンクする)
/// @details renderer / GPU / utility プロセスとして起動される。
///          CefMessageRouterRendererSide を持ち、JS ↔ C++ bridge を支える。

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"
#include "include/wrapper/cef_message_router.h"

/// @brief CEF サブプロセス用 CefApp
/// @details CefRenderProcessHandler を実装し、JS メッセージルーターを renderer
///          コンテキストに注入する。ブラウザプロセス側の MitiruCefClient と
///          同じ js_query_function 名 ("cefQuery") を使うこと。
///          OnRegisterCustomSchemes はブラウザプロセスと renderer プロセスの
///          両方で呼ばれなければならない — ここで "app" スキームを再宣言する。
class CefSubprocessApp final
    : public CefApp
    , public CefRenderProcessHandler
{
public:
    // ── CefApp ──────────────────────────────────────────────────

    /// @brief "app" カスタムスキームを renderer プロセスにも登録する
    /// @note  ブラウザプロセス側 MitiruCefApp と完全に同一の設定にすること
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override
    {
        registrar->AddCustomScheme(
            "app",
            CEF_SCHEME_OPTION_STANDARD |
            CEF_SCHEME_OPTION_CORS_ENABLED |
            CEF_SCHEME_OPTION_SECURE);
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
    {
        return this;
    }

    // ── CefRenderProcessHandler ──────────────────────────────────
    void OnWebKitInitialized() override
    {
        // ブラウザプロセス側と同じ設定で renderer-side router を作成する
        CefMessageRouterConfig config;
        config.js_query_function  = "cefQuery";
        config.js_cancel_function = "cefQueryCancel";
        m_router = CefMessageRouterRendererSide::Create(config);
    }

    void OnContextCreated(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame>   frame,
        CefRefPtr<CefV8Context> context) override
    {
        if (m_router)
        {
            m_router->OnContextCreated(browser, frame, context);
        }
    }

    void OnContextReleased(
        CefRefPtr<CefBrowser>   browser,
        CefRefPtr<CefFrame>     frame,
        CefRefPtr<CefV8Context> context) override
    {
        if (m_router)
        {
            m_router->OnContextReleased(browser, frame, context);
        }
    }

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser>        browser,
        CefRefPtr<CefFrame>          frame,
        CefProcessId                 source_process,
        CefRefPtr<CefProcessMessage> message) override
    {
        if (m_router &&
            m_router->OnProcessMessageReceived(browser, frame, source_process, message))
        {
            return true;
        }
        return false;
    }

    /// @brief renderer process shutdown 前に router を release する
    /// @details m_router destructor が CefExecuteProcess 終了後に走ると
    ///          `Check failed: CefCurrentlyOn(TID_UI)` FATAL になる。
    ///          OnBrowserDestroyed は browser teardown 時に CEF UI 上で
    ///          呼ばれるので、ここで先に release すれば lifecycle 一致する。
    ///          (Mitiru app は renderer 1:1 で browser を持つ前提)
    void OnBrowserDestroyed(CefRefPtr<CefBrowser> /*browser*/) override
    {
        m_router = nullptr;
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> m_router;
    IMPLEMENT_REFCOUNTING(CefSubprocessApp);
};

static int runSubprocess(HINSTANCE hInstance)
{
    CefMainArgs main_args(hInstance);
    CefRefPtr<CefSubprocessApp> app = new CefSubprocessApp();
    return CefExecuteProcess(main_args, app.get(), nullptr);
}

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    LPSTR     /*lpCmdLine*/,
    int       /*nCmdShow*/)
{
    return runSubprocess(hInstance);
}

/// @brief CONSOLE サブシステム向けエントリポイント
/// @details 主 exe (mitiru_cef_overlay.exe 等) が CONSOLE サブシステムの
///          場合、Chromium は subprocess も同じサブシステムで spawn する
///          ことを期待する。両方を CONSOLE に揃えることで
///          "GPU process launch failed: error_code=63" を回避できるか検証中。
int main()
{
    return runSubprocess(::GetModuleHandleW(nullptr));
}

#else

int main(int, char**) { return 0; }

#endif // _WIN32 && MITIRU_HAS_CEF
