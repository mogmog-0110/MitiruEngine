#pragma once

/// @file MitiruCefConfig.hpp
/// @brief CEF 初期化設定ビルダー
/// @details CefSettings / CefWindowInfo / CefBrowserSettings を
///          MitiruEngine 向けに設定するヘルパー群。
///          ゲームコードからは直接使用しない (MitiruCefContext 経由)。

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

#include "include/cef_app.h"

namespace mitiru::cef
{

/// @brief CefSettings を OSR (オフスクリーンレンダリング) 向けに構築する
/// @param exeDir              実行ファイルのあるディレクトリ (subprocess / resource の基点)
/// @param helperExeName       CEF renderer/gpu/utility サブプロセスの実行ファイル名
///                            (例 "MyGameHelper.exe")
/// @param logPath             CEF ログファイルパス (空文字 = "cef.log" をexeDirに出力)
/// @param remoteDebuggingPort 0 以外にすると chrome://inspect / chrome-devtools MCP
///                            が `http://localhost:<port>` 経由で attach できるよう
///                            remote-debugging-port を開く (E-02 対応)。
///                            開発ビルドでのみ有効化することを強く推奨。
/// @return 設定済み CefSettings
inline CefSettings buildCefSettings(
    const std::filesystem::path& exeDir,
    const std::string&           helperExeName       = "MitiruCefHelper.exe",
    const std::string&           logPath             = "",
    int                          remoteDebuggingPort = 0)
{
    CefSettings s;

    // ── サブプロセス ─────────────────────────────────────
    // renderer/gpu/utility サブプロセス実行ファイルを指定する。
    // これによりメイン exe 側は CefExecuteProcess() を呼ばなくてよい。
    const auto helperPath = exeDir / helperExeName;
    CefString(&s.browser_subprocess_path) = helperPath.string();

    // ── リソースパス ──────────────────────────────────────
    // libcef.dll と同じフォルダを指定する (fetch_cef.py が配置済み)
    CefString(&s.resources_dir_path) = exeDir.string();
    CefString(&s.locales_dir_path)   = (exeDir / "locales").string();

    // ── OSR (OffScreen Rendering) ────────────────────────
    s.windowless_rendering_enabled = 1;

    // ── サンドボックス ─────────────────────────────────────
    // 開発フェーズではサンドボックスを無効にする。
    // 本番リリース時は CefSandboxInfo を適切に設定すること。
    s.no_sandbox = 1;

    // ── ログ ─────────────────────────────────────────────
    s.log_severity = LOGSEVERITY_WARNING;
    const auto lp  = logPath.empty()
        ? (exeDir / "cef.log").string()
        : logPath;
    CefString(&s.log_file) = lp;

    // ── マルチスレッドメッセージループ ────────────────────
    // false = CefDoMessageLoopWork() を毎フレーム呼ぶ方式 (ゲームループ統合)
    s.multi_threaded_message_loop = 0;

    // ── ユーザーデータ ────────────────────────────────────
    CefString(&s.cache_path) = (exeDir / "cef_cache").string();

    // ── リモートデバッグ (E-02) ───────────────────────────
    // 0 以外にすると Chromium の DevTools protocol が TCP で待ち受けする。
    // `chrome://inspect` / `chrome-devtools://` MCP から attach して
    // OSR CEF の DOM / Console / Network を検査できる。
    // 本番ビルドで有効のまま出荷しないこと。
    if (remoteDebuggingPort > 0 && remoteDebuggingPort < 65536)
    {
        s.remote_debugging_port = remoteDebuggingPort;
    }

    return s;
}

/// @brief CefWindowInfo を OSR 向けに構築する (ウィンドウなし)
/// @note CefWindowInfo は explicit コピーコンストラクターを持つため
///       return-by-value が不可。出力パラメーターで受け取る。
inline void buildWindowInfo(CefWindowInfo& wi, int /*width*/, int /*height*/)
{
    wi.SetAsWindowless(nullptr); // ウィンドウハンドルなし = OSR モード
}

/// @brief CefBrowserSettings をゲーム向けに構築する
/// @param bgTransparent true にすると背景が透明になる
inline CefBrowserSettings buildBrowserSettings(bool bgTransparent = true)
{
    CefBrowserSettings bs;
    // 背景を完全透明にする (ARGB 0x00000000)
    bs.background_color = bgTransparent ? 0x00000000 : 0xFF000000;
    // JavaScript 有効化
    bs.javascript = STATE_ENABLED;
    // OSR フレームレートを 90fps に設定する (デフォルト 30fps → アニメーションがかくかくする)
    bs.windowless_frame_rate = 90;
    // file_access_from_file_urls / universal_access_from_file_urls は
    // CEF 97 以降 CefBrowserSettings から削除された。
    // ローカルファイルアクセスは MitiruCefApp::OnBeforeCommandLineProcessing で
    // コマンドラインスイッチとして設定する。
    return bs;
}

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
