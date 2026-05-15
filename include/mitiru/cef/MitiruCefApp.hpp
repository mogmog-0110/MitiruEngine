#pragma once

/// @file MitiruCefApp.hpp
/// @brief CefApp サブクラス — ブラウザプロセス起動フック

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_command_line.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_message_router.h"

#include <mitiru/cef/MitiruCefSchemeHandler.hpp>

namespace mitiru::cef
{

/// @brief MitiruEngine 用 CefApp 実装
/// @details ブラウザプロセスの起動設定 + **single-process モードで renderer 側
///          の CefMessageRouterRendererSide も注入する**。
///
/// [SINGLE-PROCESS CAVEAT 2026-04-23]
/// 暫定で `single-process` スイッチを付けているため subprocess (MitiruCefHelper.exe)
/// は起動されない。結果、`cef_subprocess_main.cpp` の CefSubprocessApp の
/// `OnWebKitInitialized` / `OnContextCreated` は一度も呼ばれない。
///
/// それにより `window.cefQuery` が JS 側に注入されず、cefQuery を使った
/// JS→C++ ハンドラーが全て no-op になる症状が発生していた
/// (Title 左クリック → Raising 遷移が起きない、等)。
///
/// 対策: 本クラスが `CefRenderProcessHandler` も兼任することで、single-process
/// でも renderer-side router が初期化されるようにする。
/// multi-process モードに戻す際は subprocess 側で二重初期化にならない点を確認。
class MitiruCefApp final
    : public CefApp
    , public CefBrowserProcessHandler
    , public CefRenderProcessHandler
{
public:
    // ── CefApp ──────────────────────────────────────────────────
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
    {
        return this;
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
    {
        return this;
    }

    /// @brief カスタムスキームを宣言する (全プロセスで呼ばれる)
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override
    {
        // "app" スキームを標準的な安全なスキームとして登録する
        // CEF_SCHEME_OPTION_STANDARD: 標準URL構文 (origin/host/path)
        // CEF_SCHEME_OPTION_CORS_ENABLED: fetch() / XHR でのクロスオリジン許可
        // CEF_SCHEME_OPTION_SECURE: https と同等の安全スキームとして扱う
        registrar->AddCustomScheme(
            "app",
            CEF_SCHEME_OPTION_STANDARD |
            CEF_SCHEME_OPTION_CORS_ENABLED |
            CEF_SCHEME_OPTION_SECURE);
    }

    /// @brief コマンドラインスイッチを設定する
    /// @details OSR に必要なフラグと GPU 同期の無効化を行う。
    void OnBeforeCommandLineProcessing(
        const CefString&         process_type,
        CefRefPtr<CefCommandLine> command_line) override
    {
        // ブラウザプロセスのみ設定する (空文字 = browser process)
        if (!process_type.empty())
        {
            return;
        }

        // OSR フラグ
        command_line->AppendSwitch("off-screen-rendering-enabled");
        command_line->AppendSwitch("disable-gpu-vsync");
        // OSR でも GPU アクセラレーションを使用する
        // disable-gpu 系フラグを外すと Chromium がGPU コンポジットを使用し
        // CSS アニメーションが GPU スレッドで滑らかに動作する
        command_line->AppendSwitch("enable-begin-frame-scheduling");
        // ポップアップを無効化 (ゲームではポップアップウィンドウ不要)
        command_line->AppendSwitch("disable-popup-blocking");
        // ローカルファイル (file:///) からのクロスオリジンアクセスを許可
        // CEF 97 以降 CefBrowserSettings からこれらが削除されたためフラグで設定
        command_line->AppendSwitch("allow-file-access-from-files");
        command_line->AppendSwitch("disable-web-security");
        // ログ削減
        command_line->AppendSwitch("disable-logging");
        // [ARCHITECTURAL 2026-04-22] Multi-process モードで subprocess 起動が
        // "GPU process launch failed: error_code=63" で FATAL 終了する。
        // 根本原因: CEF minimal 配布の libcef.dll は Release CRT (/MD) 固定で
        //   ビルドされており、Debug build (/MDd) の consumer と CRT mismatch
        //   を起こす。libcef_dll_wrapper を /MDd でビルド → /MD 版と混在 →
        //   cef_sandbox.lib が参照する _CrtDbgReport が解決できない、等、
        //   複数段階で構造的に衝突する。
        // 現時点の判断: Debug build で multi-process CEF を正しく動かすには
        //   CEF standard distribution (Debug libcef.dll 同梱) への切替が必要。
        //   minimal 配布のままでの multi-process 対応は非現実的。
        // 暫定: single-process で運用。sandbox/isolation は失われるが、
        //   ゲーム HUD 用途 (file:// のみ) では問題なし。
        // 詳細と次セッション候補: memory/todo_cef_subprocess_launch.md 参照。
        command_line->AppendSwitch("single-process");
    }

    // ── CefBrowserProcessHandler ──────────────────────────────
    void OnContextInitialized() override
    {
        // "app://" スキームハンドラーを登録する
        // (CefInitialize 後、最初のブラウザ作成前に呼ばれる)
        mitiru::cef::registerAppScheme();
    }

    // ── CefRenderProcessHandler (single-process 用) ─────────────
    /// @brief V8/Blink の起動完了時に呼ばれる — renderer-side router を作成
    void OnWebKitInitialized() override
    {
        CefMessageRouterConfig config;
        config.js_query_function  = "cefQuery";
        config.js_cancel_function = "cefQueryCancel";
        m_renderRouter = CefMessageRouterRendererSide::Create(config);
    }

    /// @brief フレーム毎の JS コンテキスト生成時に `window.cefQuery` を注入
    void OnContextCreated(
        CefRefPtr<CefBrowser>    browser,
        CefRefPtr<CefFrame>      frame,
        CefRefPtr<CefV8Context>  context) override
    {
        if (m_renderRouter)
        {
            m_renderRouter->OnContextCreated(browser, frame, context);
        }
    }

    /// @brief フレーム破棄時に router 側も解放
    void OnContextReleased(
        CefRefPtr<CefBrowser>    browser,
        CefRefPtr<CefFrame>      frame,
        CefRefPtr<CefV8Context>  context) override
    {
        if (m_renderRouter)
        {
            m_renderRouter->OnContextReleased(browser, frame, context);
        }
    }

    /// @brief ブラウザ→レンダラーのメッセージを router に橋渡し
    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser>        browser,
        CefRefPtr<CefFrame>          frame,
        CefProcessId                 source_process,
        CefRefPtr<CefProcessMessage> message) override
    {
        if (m_renderRouter &&
            m_renderRouter->OnProcessMessageReceived(browser, frame, source_process, message))
        {
            return true;
        }
        return false;
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> m_renderRouter;
    IMPLEMENT_REFCOUNTING(MitiruCefApp);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
