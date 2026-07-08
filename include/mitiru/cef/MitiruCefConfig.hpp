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

#include <cstdlib>
#include <filesystem>
#include <string>

#include "include/cef_app.h"

namespace mitiru::cef
{

namespace detail
{

/// @brief "prefix<PID>suffix" 形のファイル名から PID を取り出す。形が違えば 0。
inline DWORD parsePidFrom(
    const std::string& name, const std::string& prefix, const std::string& suffix)
{
    if (name.size() <= prefix.size() + suffix.size())              { return 0; }
    if (name.compare(0, prefix.size(), prefix) != 0)               { return 0; }
    if (name.compare(name.size() - suffix.size(),
                     suffix.size(), suffix) != 0)                  { return 0; }
    const std::string digits =
        name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    if (digits.empty() ||
        digits.find_first_not_of("0123456789") != std::string::npos) { return 0; }
    return static_cast<DWORD>(std::strtoul(digits.c_str(), nullptr, 10));
}

/// @brief PID のプロセスが生きているか (best-effort)。
inline bool isProcessAlive(DWORD pid)
{
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) { ::CloseHandle(h); return true; }
    // 開けないが実在 (別権限) の可能性 → 生存扱いで消さない
    return ::GetLastError() == ERROR_ACCESS_DENIED;
}

/// @brief 過去起動が残した cef_cache_<PID> / cef_<PID>.log を best-effort 削除する。
/// @details 自 PID と生存プロセスの分は残す。削除失敗 (使用中等) は無視 —
///          次回起動で再試行される。
inline void cleanupStaleCefArtifacts(const std::filesystem::path& exeDir)
{
    const DWORD self = ::GetCurrentProcessId();
    std::error_code ec;
    std::filesystem::directory_iterator it(exeDir, ec), end;
    for (; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        DWORD pid = parsePidFrom(name, "cef_cache_", "");
        if (pid == 0) { pid = parsePidFrom(name, "cef_", ".log"); }
        if (pid == 0 || pid == self || isProcessAlive(pid)) { continue; }
        std::error_code rmEc;
        std::filesystem::remove_all(it->path(), rmEc);
    }
}

} // namespace detail

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
    // single-process 運用 (MitiruCefApp 参照) では Chromium sandbox を併用
    // できないため no_sandbox は必然。multi-process 化する際に CefSandboxInfo
    // と合わせて再検討する。
    s.no_sandbox = 1;

    // ── ログ + キャッシュ (PID で分離) ─────────────────────
    // 複数の mitiru_host が同時起動するケース (launcher + game +
    // dev_companion など) で同じ exeDir/cef_cache を取り合うと CEF が
    // 真っ白 / 真っ黒 のまま動かなくなる。PID を suffix にして per-process
    // で分ける。過去起動の残骸は起動時に best-effort 掃除する。
    detail::cleanupStaleCefArtifacts(exeDir);
    const DWORD pid = ::GetCurrentProcessId();
    const std::string pidSuffix = "_" + std::to_string(pid);

    s.log_severity = LOGSEVERITY_WARNING;
    const auto lp  = logPath.empty()
        ? (exeDir / ("cef" + pidSuffix + ".log")).string()
        : logPath;
    CefString(&s.log_file) = lp;

    // ── マルチスレッドメッセージループ ────────────────────
    // false = CefDoMessageLoopWork() を毎フレーム呼ぶ方式 (ゲームループ統合)
    s.multi_threaded_message_loop = 0;

    // ── ユーザーデータ (per-process) ──────────────────────
    CefString(&s.cache_path) = (exeDir / ("cef_cache" + pidSuffix)).string();

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
