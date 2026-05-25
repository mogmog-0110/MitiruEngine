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
        m_client = client;          // runtime の renderHandler->setSize 用に参照を保持
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
    /// @details CEF の `WasResized()` は browser host にサイズ通知を送るが、
    ///          実際に CEF が再描画する際の viewport 寸法は
    ///          `MitiruCefRenderHandler::GetViewRect` の返値で決まる。
    ///          そのため render handler 側にも `setSize` を流して、CEF が
    ///          新サイズで HTML を re-layout + OnPaint するようにする。
    ///          これを呼ばないと CSS `@media` が反応せず、新サイズ window
    ///          に古いレイアウトが bilinear stretch される。
    void resize(int width, int height)
    {
        if (!m_host || (width == m_width && height == m_height))
        {
            return;
        }
        m_width  = width;
        m_height = height;
        if (m_client && m_client->renderHandler())
        {
            m_client->renderHandler()->setSize(width, height);
        }
        m_host->WasResized();
        // CRITICAL: resize イベントが短時間に連続して届く場合 (例: 最大化
        // → 復元 → drag-resize)、WasResized() 単体だと CEF に debounce され
        // ることがある。Invalidate で新 viewport での OnPaint を強制し、保留中
        // の texture resize が確実に適用されるようにする。これがないと texture
        // が中間の stale サイズのまま残り、composite が最終 window 寸法へ stretch
        // してしまう (2026-05-21 user verdict, maximize chain)。
        m_host->Invalidate(PET_VIEW);
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
    /// @details m_client も release する。これをしないと
    ///          ~MitiruCefBrowser (= MitiruCefContext destructor) が
    ///          **CefShutdown 後** に走り、~MitiruCefClient の
    ///          `router->RemoveHandler` で `Check failed: CefCurrentlyOn(TID_UI)`
    ///          FATAL になる。release ここで済ませれば、shutdown 内の
    ///          `m_client = nullptr` で refcount 0 確定 → destructor が
    ///          CEF alive な UI thread で走る。
    void onClosed()
    {
        m_host    = nullptr;
        m_browser = nullptr;
        m_client  = nullptr;
    }

private:
    [[nodiscard]] CefRefPtr<CefFrame> mainFrame() const
    {
        return m_browser ? m_browser->GetMainFrame() : nullptr;
    }

    CefRefPtr<CefBrowser>      m_browser;
    CefRefPtr<CefBrowserHost>  m_host;     ///< GetHost() 結果をキャッシュ — 一時 CefRefPtr 問題を回避
    CefRefPtr<MitiruCefClient> m_client;   ///< render handler への参照を runtime resize で使う
    int m_width  = 1920;
    int m_height = 1080;
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
