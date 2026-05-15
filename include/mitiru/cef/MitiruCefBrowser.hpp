#pragma once

/// @file MitiruCefBrowser.hpp
/// @brief ブラウザライフサイクル管理 — 生成/URL読込/リサイズ/クローズ

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <string>

#include "include/cef_browser.h"
#include "include/cef_client.h"

#include <mitiru/cef/MitiruCefClient.hpp>
#include <mitiru/cef/MitiruCefConfig.hpp>

namespace mitiru::cef
{

/// @brief CEF ブラウザインスタンスのラッパー
/// @details 生成・URL 読み込み・HTML 直接注入・リサイズ・クローズを担う。
///          MitiruCefContext が所有し、Engine には公開しない。
class MitiruCefBrowser
{
public:
    MitiruCefBrowser() = default;

    /// @brief ブラウザを生成する
    /// @param client    CefClient 実装
    /// @param width     初期幅
    /// @param height    初期高さ
    /// @param startUrl  最初に開く URL (空なら about:blank)
    /// @return 生成に成功したか
    bool create(
        CefRefPtr<MitiruCefClient> client,
        int                        width,
        int                        height,
        const std::string&         startUrl = "about:blank")
    {
        if (m_browser)
        {
            return true; // 既に存在
        }

        CefWindowInfo windowInfo;
        buildWindowInfo(windowInfo, width, height);
        const auto browserSettings = buildBrowserSettings(true);
        const CefString url(startUrl.empty() ? "about:blank" : startUrl);

        m_browser = CefBrowserHost::CreateBrowserSync(
            windowInfo, client, url, browserSettings, nullptr, nullptr);

        if (!m_browser)
        {
            return false;
        }

        // ホストへの参照を保持する (生ポインタ返しによる一時 CefRefPtr 問題を回避)
        m_host = m_browser->GetHost();

        // OSR ブラウザは起動時に hidden 状態。WasHidden(false) で表示状態にする。
        // これをしないと render widget が未初期化のまま SendMouseMoveEvent が
        // 無効なポインタを返しクラッシュする。
        m_host->WasHidden(false);

        // 初回 OnPaint をトリガーして描画パイプラインを確立する
        m_host->WasResized();

        client->onBrowserCreated(m_browser);
        client->renderHandler()->setSize(width, height);
        m_width  = width;
        m_height = height;
        return true;
    }

    /// @brief URL に遷移する
    void loadUrl(const std::string& url)
    {
        if (auto frame = mainFrame())
        {
            frame->LoadURL(url);
        }
    }

    /// @brief HTML 文字列を直接注入する
    /// @param html  HTML コンテンツ (UTF-8)
    /// @param baseUrl  相対パス解決のベース URL
    void loadHtml(const std::string& html, const std::string& baseUrl = "about:blank")
    {
        if (auto frame = mainFrame())
        {
            frame->LoadURL("data:text/html;charset=utf-8," + html);
            (void)baseUrl; // CEF 130+ は LoadString が廃止済み — data URI を使う
        }
    }

    /// @brief JavaScript を実行する
    void executeJavaScript(const std::string& code)
    {
        if (auto frame = mainFrame())
        {
            frame->ExecuteJavaScript(code, frame->GetURL(), 0);
        }
    }

    /// @brief ビューポートをリサイズする
    void resize(int width, int height)
    {
        if (!m_host || (width == m_width && height == m_height))
        {
            return;
        }
        m_width  = width;
        m_height = height;
        m_host->WasResized();
    }

    /// @brief ブラウザを閉じる (非同期)
    /// @details LifeSpanHandler::OnBeforeClose でブラウザ参照がリセットされる。
    void close()
    {
        if (m_host)
        {
            m_host->CloseBrowser(true);
        }
    }

    /// @brief フォーカスを設定/解除する
    void setFocus(bool focused)
    {
        if (m_host)
        {
            m_host->SetFocus(focused);
        }
    }

    /// @brief キーボードフォーカスを CEF 側に要求する (H-08)
    /// @details OSR モードでは host->SetFocus(true) を呼ばないと
    ///          SendKeyEvent がドキュメントにルーティングされない。
    ///          MitiruCefContext が初回ペイント完了時 / setInputEnabled(true)
    ///          時に自動呼び出しする — 通常はゲーム側から直接呼ぶ必要は無い。
    ///          手動制御したい場合は setAutoFocusOnFirstPaint(false) で
    ///          自動呼び出しを抑止し、本メソッドを任意のタイミングで呼ぶ。
    void claimKeyboardFocus()
    {
        setFocus(true);
    }

    // ── アクセサー ────────────────────────────────────────────
    [[nodiscard]] CefRefPtr<CefBrowser>     browser() const { return m_browser; }
    /// @brief ブラウザホストを返す。キャッシュ済み CefRefPtr から取得するため
    ///        一時オブジェクトの lifetime 問題が発生しない。
    [[nodiscard]] CefBrowserHost*           host()    const { return m_host.get(); }
    [[nodiscard]] bool isValid() const { return m_browser != nullptr; }
    [[nodiscard]] int  width()   const { return m_width;  }
    [[nodiscard]] int  height()  const { return m_height; }

    /// @brief LifeSpanHandler からブラウザが閉じられた後に呼ぶ
    void onClosed()
    {
        m_host    = nullptr;
        m_browser = nullptr;
    }

private:
    [[nodiscard]] CefRefPtr<CefFrame> mainFrame() const
    {
        return m_browser ? m_browser->GetMainFrame() : nullptr;
    }

    CefRefPtr<CefBrowser>     m_browser;
    CefRefPtr<CefBrowserHost> m_host;   ///< GetHost() 結果をキャッシュ — 一時 CefRefPtr 問題を回避
    int m_width  = 1920;
    int m_height = 1080;
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
