#pragma once

/// @file MitiruCefResourceRequestHandler.hpp
/// @brief file:// 本文ナビゲーションが読めない時、Chromium デフォルトのエラーページの
///        代わりに自前 HTML を「成功レスポンス (200)」として返す。失敗ナビゲーション自体が
///        起きないので、デフォルトページが一瞬出る (フラッシュ) ことが無くなる。
/// @details OnLoadError は「出た後に LoadURL で上書き」=フラッシュが残る。こちらは読み込み
///          時に横取りするのでフラッシュゼロ。対象は file:// の本文 (RT_MAIN_FRAME) のみ。
///          http:// は事前に成否を判定できない (要プロキシ) ので従来どおり OnLoadError 経由。

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_response.h"

#include <mitiru/cef/CefErrorPage.hpp>

namespace mitiru::cef
{

/// 与えた HTML 文字列を所有し、200 text/html で返す resource handler。
/// 文字列を自分で持つので CefStreamReader::CreateForData のライフタイム問題を避ける。
class ErrorPageResourceHandler final : public CefResourceHandler
{
public:
    explicit ErrorPageResourceHandler(std::string html) : m_html(std::move(html)) {}

    bool Open(CefRefPtr<CefRequest> /*request*/, bool& handle_request,
              CefRefPtr<CefCallback> /*callback*/) override
    {
        handle_request = true;   // 同期で処理する
        return true;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& response_length,
                            CefString& /*redirectUrl*/) override
    {
        response->SetStatus(200);
        response->SetStatusText("OK");
        response->SetMimeType("text/html");
        response_length = static_cast<int64_t>(m_html.size());
    }

    bool Read(void* data_out, int bytes_to_read, int& bytes_read,
              CefRefPtr<CefResourceReadCallback> /*callback*/) override
    {
        const std::size_t remain = m_html.size() - m_pos;
        if (remain == 0) { bytes_read = 0; return false; }   // EOF
        const std::size_t n =
            std::min(static_cast<std::size_t>(bytes_to_read), remain);
        std::memcpy(data_out, m_html.data() + m_pos, n);
        m_pos += n;
        bytes_read = static_cast<int>(n);
        return true;
    }

    void Cancel() override {}

private:
    std::string m_html;
    std::size_t m_pos = 0;

    IMPLEMENT_REFCOUNTING(ErrorPageResourceHandler);
};

/// file:// 本文ロードを横取りし、読めなければ自前エラーページを返す。
class MitiruCefResourceRequestHandler final : public CefResourceRequestHandler
{
public:
    CefRefPtr<CefResourceHandler> GetResourceHandler(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame>   /*frame*/,
        CefRefPtr<CefRequest> request) override
    {
        // 本文 (メインフレーム) のみ。CSS/JS/img 等のサブリソースは触らない
        // (HUD が部分的に動く場合を壊さない)。
        if (request->GetResourceType() != RT_MAIN_FRAME) { return nullptr; }

        const std::string url = request->GetURL().ToString();
        if (url.rfind("file://", 0) != 0) { return nullptr; }  // file:// のみ

        const std::filesystem::path p = fileUrlToFsPath(url);
        std::error_code ec;
        // 判定不能 or 存在する → 通常ロード (CEF に任せる)。
        if (p.empty() || std::filesystem::exists(p, ec)) { return nullptr; }

        // 読めない → 失敗させず自前ページを 200 で返す = デフォルトページが一切出ない。
        return new ErrorPageResourceHandler(
            buildErrorPageHtml(url, "ファイルが見つかりません", -6));
    }

private:
    /// "file:///E:/a/b.html" → "E:/a/b.html" (% デコード・UTF-8)。判定不能は空 path。
    static std::filesystem::path fileUrlToFsPath(const std::string& url)
    {
        std::string s;
        if (url.rfind("file:///", 0) == 0)     { s = url.substr(8); }
        else if (url.rfind("file://", 0) == 0) { s = url.substr(7); }
        else                                   { return {}; }

        const auto cut = s.find_first_of("?#");   // query/fragment を落とす
        if (cut != std::string::npos) { s.resize(cut); }

        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
            return -1;
        };
        std::string d;
        d.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '%' && i + 2 < s.size())
            {
                const int h = hex(s[i + 1]), l = hex(s[i + 2]);
                if (h >= 0 && l >= 0) { d += static_cast<char>(h * 16 + l); i += 2; continue; }
            }
            d += s[i];
        }
        // UTF-8 バイト列として path を作る (cp932 解釈を避ける)。
        const std::u8string u8(reinterpret_cast<const char8_t*>(d.data()), d.size());
        return std::filesystem::path(u8);
    }

    IMPLEMENT_REFCOUNTING(MitiruCefResourceRequestHandler);
};

}  // namespace mitiru::cef

#endif  // _WIN32 && MITIRU_HAS_CEF
