#pragma once

/// @file MitiruCefDisplayHandler.hpp
/// @brief 表示イベントハンドラー — JS console.log をデバッグ出力に転送する

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string>

#include "include/cef_display_handler.h"

namespace mitiru::cef
{

/// @brief CEF 表示イベントを処理する
/// @details JS の console.log / console.error を OutputDebugStringA に転送する。
class MitiruCefDisplayHandler final : public CefDisplayHandler
{
public:
    MitiruCefDisplayHandler() = default;

    // ── CefDisplayHandler ───────────────────────────────────────
    bool OnConsoleMessage(
        CefRefPtr<CefBrowser>  /*browser*/,
        cef_log_severity_t     level,
        const CefString&       message,
        const CefString&       source,
        int                    line) override
    {
        const char* prefix = (level >= LOGSEVERITY_ERROR) ? "[CEF][ERR] " : "[CEF] ";
        const std::string msg = prefix + message.ToString()
            + "  (" + source.ToString() + ":" + std::to_string(line) + ")\n";
        OutputDebugStringA(msg.c_str());
        return false; // false = CEF に通常ログも行わせる
    }

private:
    IMPLEMENT_REFCOUNTING(MitiruCefDisplayHandler);
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
