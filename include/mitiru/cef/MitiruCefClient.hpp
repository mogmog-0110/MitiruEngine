#pragma once

/// @file MitiruCefClient.hpp
/// @brief CefClient 実装 — 全ハンドラーを集約するアグリゲーター

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <memory>

#include "include/cef_client.h"
#include "include/cef_request_handler.h"
#include "include/wrapper/cef_message_router.h"

#include <mitiru/cef/MitiruCefBridge.hpp>
#include <mitiru/cef/MitiruCefContextMenuHandler.hpp>
#include <mitiru/cef/MitiruCefDisplayHandler.hpp>
#include <mitiru/cef/MitiruCefLifeSpanHandler.hpp>
#include <mitiru/cef/MitiruCefLoadHandler.hpp>
#include <mitiru/cef/MitiruCefRenderHandler.hpp>
#include <mitiru/cef/MitiruCefResourceRequestHandler.hpp>

namespace mitiru::cef
{

/// @brief MitiruEngine 用 CefClient アグリゲーター
/// @details 各ハンドラーを所有し、CEF に提供する。
///          MitiruCefBridge と MitiruCefRenderHandler への参照を外部に公開する。
class MitiruCefClient final
    : public CefClient
    , public CefRequestHandler
{
public:
    MitiruCefClient()
    {
        m_renderHandler  = new MitiruCefRenderHandler();
        m_loadHandler    = new MitiruCefLoadHandler();
        m_lifespanHandler= new MitiruCefLifeSpanHandler();
        m_displayHandler = new MitiruCefDisplayHandler();
        m_menuHandler    = new MitiruCefContextMenuHandler();
        m_resourceReqHandler = new MitiruCefResourceRequestHandler();
        m_bridge         = std::make_shared<MitiruCefBridge>();

        // メッセージルーターを初期化する
        CefMessageRouterConfig routerConfig;
        routerConfig.js_query_function  = "cefQuery";
        routerConfig.js_cancel_function = "cefQueryCancel";
        m_router = CefMessageRouterBrowserSide::Create(routerConfig);
        m_router->AddHandler(m_bridge.get(), true);
    }

    ~MitiruCefClient()
    {
        if (m_router && m_bridge)
        {
            m_router->RemoveHandler(m_bridge.get());
        }
    }

    // ── ハンドラー公開 API ──────────────────────────────────────
    [[nodiscard]] MitiruCefRenderHandler*  renderHandler()  const { return m_renderHandler.get();  }
    [[nodiscard]] MitiruCefLoadHandler*    loadHandler()    const { return m_loadHandler.get();    }
    [[nodiscard]] MitiruCefLifeSpanHandler* lifespanHandler() const { return m_lifespanHandler.get(); }
    [[nodiscard]] std::shared_ptr<MitiruCefBridge> bridge() const { return m_bridge; }

    // ── CefClient ───────────────────────────────────────────────
    CefRefPtr<CefRenderHandler>      GetRenderHandler()      override { return m_renderHandler;   }
    CefRefPtr<CefLoadHandler>        GetLoadHandler()        override { return m_loadHandler;     }
    CefRefPtr<CefLifeSpanHandler>    GetLifeSpanHandler()    override { return m_lifespanHandler; }
    CefRefPtr<CefDisplayHandler>     GetDisplayHandler()     override { return m_displayHandler;  }
    CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return m_menuHandler;     }
    CefRefPtr<CefRequestHandler>     GetRequestHandler()     override { return this; }

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser>        browser,
        CefRefPtr<CefFrame>          frame,
        CefProcessId                 /*source_process*/,
        CefRefPtr<CefProcessMessage> message) override
    {
        return m_router->OnProcessMessageReceived(browser, frame,
            PID_RENDERER, message);
    }

    /// @brief ブラウザ作成完了後に呼ぶ (現在は no-op — router は自動接続)
    void onBrowserCreated(CefRefPtr<CefBrowser> /*browser*/) {}

    // ── CefRequestHandler (メッセージルーター用) ─────────────────
    bool OnBeforeBrowse(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame>   frame,
        CefRefPtr<CefRequest> /*request*/,
        bool                  /*user_gesture*/,
        bool                  /*is_redirect*/) override
    {
        m_router->OnBeforeBrowse(browser, frame);
        return false;
    }

    /// file:// 本文の読み込みを横取りし、読めなければ自前エラーページを 200 で返す
    /// (Chromium デフォルトエラーページのフラッシュを根絶。詳細は同ハンドラー参照)。
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame>   /*frame*/,
        CefRefPtr<CefRequest> /*request*/,
        bool                  /*is_navigation*/,
        bool                  /*is_download*/,
        const CefString&      /*request_initiator*/,
        bool&                 /*disable_default_handling*/) override
    {
        return m_resourceReqHandler;
    }

    // OnRenderProcessTerminated は CEF バージョンによりシグネチャが異なるため省略
    // router->OnRenderProcessTerminated(browser) を呼ぶと pending query がキャンセルされる

private:
    CefRefPtr<MitiruCefRenderHandler>    m_renderHandler;
    CefRefPtr<MitiruCefLoadHandler>      m_loadHandler;
    CefRefPtr<MitiruCefLifeSpanHandler>  m_lifespanHandler;
    CefRefPtr<MitiruCefDisplayHandler>   m_displayHandler;
    CefRefPtr<MitiruCefContextMenuHandler> m_menuHandler;
    CefRefPtr<MitiruCefResourceRequestHandler> m_resourceReqHandler;
    std::shared_ptr<MitiruCefBridge>     m_bridge;
    CefRefPtr<CefMessageRouterBrowserSide> m_router;

    IMPLEMENT_REFCOUNTING(MitiruCefClient);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
