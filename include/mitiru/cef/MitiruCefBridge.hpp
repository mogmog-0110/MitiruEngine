#pragma once

/// @file MitiruCefBridge.hpp
/// @brief JS ↔ C++ メッセージブリッジ
///
/// JS 側:
///   window.cefQuery({
///       request: JSON.stringify({handler: "menu.newGame", data: {}}),
///       onSuccess: function(resp) { ... },
///       onFailure: function(code, msg) { ... }
///   });
///
/// C++ 側:
///   bridge.registerHandler("menu.newGame", [](std::string_view payload) -> std::string {
///       return "{}";
///   });
///
/// C++ → JS プッシュ:
///   bridge.executeJavaScript(browser, "updateState(...)");

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

// windowsx.h マクロとの衝突回避 (cef_dom.h の GetFirstChild/GetNextSibling)
#ifdef GetFirstChild
#  undef GetFirstChild
#endif
#ifdef GetNextSibling
#  undef GetNextSibling
#endif

#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/wrapper/cef_message_router.h"

namespace mitiru::cef
{

/// @brief JS ↔ C++ メッセージブリッジ
/// @details CefMessageRouterBrowserSide をラップし、名前ベースのハンドラー登録 API を提供する。
///          ハンドラー関数は任意のスレッドから呼ばれうる (CEF UI スレッド)。
class MitiruCefBridge final
    : public CefMessageRouterBrowserSide::Handler
{
public:
    using HandlerFn = std::function<std::string(std::string_view payload)>;

    /// @brief 名前付きハンドラーを登録する
    /// @param name JS 側の request フィールドに含まれるハンドラー名
    /// @param fn   コールバック (payload=requestの json, 戻り値=response json)
    void registerHandler(const std::string& name, HandlerFn fn)
    {
        std::lock_guard lock(m_mutex);
        m_handlers[name] = std::move(fn);
    }

    /// @brief ハンドラーを解除する
    void unregisterHandler(const std::string& name)
    {
        std::lock_guard lock(m_mutex);
        m_handlers.erase(name);
    }

    /// @brief 全ハンドラーを解除する
    void unregisterAll()
    {
        std::lock_guard lock(m_mutex);
        m_handlers.clear();
    }

    /// @brief JavaScript を実行する (C++ → JS プッシュ)
    /// @param browser   対象ブラウザ
    /// @param code      実行する JS コード
    static void executeJavaScript(
        CefRefPtr<CefBrowser> browser,
        const std::string&    code)
    {
        if (!browser)
        {
            return;
        }
        auto frame = browser->GetMainFrame();
        if (frame)
        {
            frame->ExecuteJavaScript(code, frame->GetURL(), 0);
        }
    }

    // ── CefMessageRouterBrowserSide::Handler ────────────────────
    bool OnQuery(
        CefRefPtr<CefBrowser>        /*browser*/,
        CefRefPtr<CefFrame>          /*frame*/,
        int64_t                      /*query_id*/,
        const CefString&             request,
        bool                         /*persistent*/,
        CefRefPtr<Callback>          callback) override
    {
        const std::string req = request.ToString();

        // リクエスト文字列を "handlerName|payload" 形式として解釈する
        // シンプルな規約: "menu.newGame" または "menu.newGame|{...json...}"
        std::string handlerName;
        std::string payload;
        const auto sep = req.find('|');
        if (sep != std::string::npos)
        {
            handlerName = req.substr(0, sep);
            payload     = req.substr(sep + 1);
        }
        else
        {
            handlerName = req;
        }

        HandlerFn fn;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_handlers.find(handlerName);
            if (it == m_handlers.end())
            {
                callback->Failure(404, "handler not found: " + handlerName);
                return true;
            }
            fn = it->second;
        }

        try
        {
            const std::string resp = fn(payload);
            callback->Success(resp);
        }
        catch (const std::exception& e)
        {
            callback->Failure(500, e.what());
        }
        catch (...)
        {
            callback->Failure(500, "unknown error in handler: " + handlerName);
        }
        return true;
    }

    void OnQueryCanceled(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame>   /*frame*/,
        int64_t               /*query_id*/) override
    {
        // キャンセル時は何もしない
    }

private:
    std::mutex                               m_mutex;
    std::unordered_map<std::string, HandlerFn> m_handlers;
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
