#pragma once

/// @file MitiruCefApp.hpp
/// @brief CefApp サブクラス。ブラウザプロセス起動フック

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <mitiru/cef/CefIncludeGuardBegin.hpp>
#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_command_line.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_message_router.h"
#include <mitiru/cef/CefIncludeGuardEnd.hpp>

#include <cstdlib>

#include <mitiru/cef/MitiruCefSchemeHandler.hpp>

namespace mitiru::cef
{

/// @brief MitiruEngine 用 CefApp 実装 (browser process 側)
/// @details 起動設定と `app://` スキームの登録だけを持つ。renderer / GPU プロセスは別 exe
///          (`MitiruCefHelper.exe`、`cef_subprocess_main.cpp` の CefSubprocessApp) が担い、
///          `window.cefQuery` の注入もそちら側の `OnWebKitInitialized` が行う (ADR 0026)。
class MitiruCefApp final
    : public CefApp
    , public CefBrowserProcessHandler
{
public:
    // ── CefApp ──────────────────────────────────────────────────
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
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
#ifndef NDEBUG
        // Debug のみ: file:/// からの XHR / fetch とクロスオリジンを許可。
        // CEF 97 以降 CefBrowserSettings からこれらが削除されたためフラグで設定。
        // Release では付けない。ローカル資産は CORS 対応の app:// スキーム
        // (registerAppScheme / cefAdditionalAssetDirs) で配信する。
        // 注意: mitiru.fetch / mitiru.loadJson (mitiru_runtime.js) は file://
        // ページからの JSON 読込にこのスイッチへ依存する。Release では
        // cefStartUrl を app:// にすること。
        command_line->AppendSwitch("allow-file-access-from-files");
        command_line->AppendSwitch("disable-web-security");
#endif
        // ログ削減。ただし MITIRU_CEF_DEBUG_LOG=1 のときは CHECK()/FATAL 失敗の
        // メッセージを見るため抑制しない (K4 調査用、既定の挙動は変えない)。
        if (!isCefDebugLogEnabled())
        {
            command_line->AppendSwitch("disable-logging");
        }
        else
        {
            command_line->AppendSwitchWithValue("enable-logging", "stderr");
            command_line->AppendSwitchWithValue("v", "1");
        }
        // network service 関連の追加ノイズ抑制 (どれも HUD 用途で不要)。
        // MITIRU_CEF_DEBUG_LOG=1 のときは上の enable-logging と競合するため付けない。
        if (!isCefDebugLogEnabled())
        {
            command_line->AppendSwitchWithValue("log-severity", "disable");
        }
    }

    /// @details "=0" 等で無効化したつもりが (存在するだけで) verbose になる事故を
    ///          防ぐため、値そのものを見る。
    [[nodiscard]] static bool isCefDebugLogEnabled()
    {
        const char* v = std::getenv("MITIRU_CEF_DEBUG_LOG");
        return v != nullptr && v[0] == '1';
    }

    // ── CefBrowserProcessHandler ──────────────────────────────
    void OnContextInitialized() override
    {
        // "app://" スキームハンドラーを登録する
        // (CefInitialize 後、最初のブラウザ作成前に呼ばれる)
        mitiru::cef::registerAppScheme();
    }

private:
    IMPLEMENT_REFCOUNTING(MitiruCefApp);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
