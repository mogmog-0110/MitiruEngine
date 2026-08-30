#pragma once

/// @file MitiruCefSchemeHandler.hpp
/// @brief "app://" カスタムスキームハンドラー
///
/// HTML/CSS/JS/画像などを実行ファイルに埋め込んで CEF から配信する。
/// URL 形式: app://ui/title.html  →  embedded_assets::lookup("ui/title.html")
///
/// 使い方:
///   1. CMakeLists.txt で embed_assets() を使い embedded_assets.hpp を生成する
///   2. CefSettings::scheme_handler_factories に MitiruCefSchemeHandlerFactory を登録する
///      (MitiruCefApp::OnRegisterCustomSchemes で "app" スキームを宣言する)
///   3. loadUrl("app://ui/title.html") で HTML をロードする

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <fstream>
#include <string>
#include <vector>

#ifdef GetFirstChild
#  undef GetFirstChild
#endif
#ifdef GetNextSibling
#  undef GetNextSibling
#endif

#include "include/cef_browser.h"
#include "include/cef_callback.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_scheme.h"

#include <mitiru/asset/AssetPack.hpp> // vfs::readGlobal / hasGlobalMount (runtime pack)

// 生成された埋め込みアセットヘッダー
// EmbedAssets.cmake が OUTPUT_DIR (= CMAKE_CURRENT_BINARY_DIR/generated) を
// インクルードパスに追加するため、ファイル名のみで参照する。
// 生成前 (IDE インテリセンス等) はスタブを使う。
#if __has_include("embedded_assets.hpp")
#  include "embedded_assets.hpp"
#else
// スタブ: 空の lookup を提供する (disk fallback が実際のファイルを提供する)
#  include <span>
#  include <string_view>
namespace mitiru::assets {
    [[nodiscard]] inline std::span<const uint8_t> lookup(std::string_view) noexcept { return {}; }
} // namespace mitiru::assets
#endif

namespace mitiru::cef
{

/// @brief app:// スキームへのリクエストを埋め込みバイト列で応答する
class MitiruCefResourceHandler final : public CefResourceHandler
{
public:
    explicit MitiruCefResourceHandler(std::span<const uint8_t> data, std::string mimeType)
        : m_data(data.begin(), data.end())
        , m_mime(std::move(mimeType))
    {}

    bool ProcessRequest(
        CefRefPtr<CefRequest>  request,
        CefRefPtr<CefCallback> callback) override
    {
        m_offset = 0;
        callback->Continue();
        return true;
    }

    void GetResponseHeaders(
        CefRefPtr<CefResponse> response,
        int64_t&               responseLength,
        CefString&             /*redirectUrl*/) override
    {
        // MIME type と charset は別々に渡す。CefResponse::SetMimeType が期待するのは型だけで、
        // "text/html; charset=utf-8" をまとめて渡すと Chromium はその全体を未知の型として扱い、
        // ページを **プレーンテキストとして表示する**。HTML は出るが DOM は無く、スクリプトも
        // 走らないので、症状は「画面には出ているのにボタンが効かない」になる。
        const std::size_t semi = m_mime.find(';');
        if (semi == std::string::npos)
        {
            response->SetMimeType(m_mime);
        }
        else
        {
            response->SetMimeType(m_mime.substr(0, semi));
            const std::size_t eq = m_mime.find('=', semi);
            if (eq != std::string::npos)
            {
                response->SetCharset(m_mime.substr(eq + 1));
            }
        }
        response->SetStatus(200);
        responseLength = static_cast<int64_t>(m_data.size());
    }

    bool ReadResponse(
        void*                  dataOut,
        int                    bytesToRead,
        int&                   bytesRead,
        CefRefPtr<CefCallback> /*callback*/) override
    {
        if (m_offset >= m_data.size())
        {
            bytesRead = 0;
            return false;
        }
        const int remaining = static_cast<int>(m_data.size() - m_offset);
        bytesRead = std::min(bytesToRead, remaining);
        std::memcpy(dataOut, m_data.data() + m_offset, static_cast<std::size_t>(bytesRead));
        m_offset += static_cast<std::size_t>(bytesRead);
        return true;
    }

    void Cancel() override {}

private:
    std::vector<uint8_t> m_data;
    std::string          m_mime;
    std::size_t          m_offset = 0;

    IMPLEMENT_REFCOUNTING(MitiruCefResourceHandler);
};

// ── MIME タイプ判定 ──────────────────────────────────────────────────────

inline std::string mimeTypeForPath(std::string_view path) noexcept
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos) return "application/octet-stream";
    const auto ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")                  return "text/css; charset=utf-8";
    if (ext == "js")                   return "application/javascript; charset=utf-8";
    if (ext == "json")                 return "application/json; charset=utf-8";
    if (ext == "png")                  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")                  return "image/gif";
    if (ext == "svg")                  return "image/svg+xml";
    if (ext == "woff")                 return "font/woff";
    if (ext == "woff2")                return "font/woff2";
    if (ext == "ttf")                  return "font/ttf";
    if (ext == "webp")                 return "image/webp";
    return "application/octet-stream";
}

/// @brief app:// スキームのファクトリー
/// @details CefRegisterSchemeHandlerFactory() で "app" スキームに登録する。
///
/// 優先順位:
///   1. 埋め込みアセット (embedded_assets.hpp)。リリースビルド向け
///   2. ディスクフォールバック (setAssetRoot で設定)。開発時向け
///   3. 空レスポンス (404相当)
class MitiruCefSchemeHandlerFactory final : public CefSchemeHandlerFactory
{
public:
    /// @brief ディスクフォールバックのルートディレクトリを設定する
    /// @details 埋め込みアセットが空の場合、ここからファイルを読む。
    ///          CefInitialize() 前に呼ぶこと。
    ///          例: setAssetRoot("C:/path/to/exe/assets")
    static void setAssetRoot(const std::string& root) { s_assetRoot = root; }

    /// @brief 追加のディスクフォールバックルートを登録する
    /// @details 主ルート (setAssetRoot) で見つからなかった場合に順番に検索する。
    ///          consumer ゲームが共有 theme pack やホットリロード用ディレクトリを
    ///          登録するために使う (`EngineConfig::cefAdditionalAssetDirs`)。
    ///          CefInitialize() 前に呼ぶこと。
    static void addAssetRoot(const std::string& root)
    {
        if (!root.empty())
        {
            s_extraAssetRoots.push_back(root);
        }
    }

    /// @brief 登録済みの追加ルートをクリアする (テスト用)
    static void clearExtraAssetRoots() { s_extraAssetRoots.clear(); }

    CefRefPtr<CefResourceHandler> Create(
        CefRefPtr<CefBrowser>  /*browser*/,
        CefRefPtr<CefFrame>    /*frame*/,
        const CefString&       /*scheme_name*/,
        CefRefPtr<CefRequest>  request) override
    {
        const std::string url = request->GetURL().ToString();
        std::string virtualPath;

        const std::string prefix = "app://";
        if (url.rfind(prefix, 0) == 0)
        {
            virtualPath = url.substr(prefix.size());
        }

        // 先頭スラッシュ除去
        while (!virtualPath.empty() && virtualPath.front() == '/')
        {
            virtualPath.erase(virtualPath.begin());
        }

        // クエリ文字列・フラグメントを除去
        const auto q = virtualPath.find('?');
        if (q != std::string::npos) virtualPath = virtualPath.substr(0, q);
        const auto f = virtualPath.find('#');
        if (f != std::string::npos) virtualPath = virtualPath.substr(0, f);

        const std::string mime = mimeTypeForPath(virtualPath);

        // 0. runtime pack (assets.mtpak) が mount されていれば、それを正本とする。
        //    秘匿配布: pack 中に無いものは disk を覗かせず 404。dev (未 mount) では従来経路。
        if (mitiru::vfs::hasGlobalMount())
        {
            if (auto packed = mitiru::vfs::readGlobal(virtualPath))
            {
                return new MitiruCefResourceHandler(
                    std::span<const uint8_t>(*packed), mime);
            }
            return new MitiruCefResourceHandler({}, "text/plain");
        }

        // 1. 埋め込みアセットを試みる
        const auto embedded = mitiru::assets::lookup(virtualPath);
        if (!embedded.empty())
        {
            return new MitiruCefResourceHandler(embedded, mime);
        }

        // 2. ディスクフォールバック。主ルート → 追加ルートの順で検索する
        const auto tryRead =
            [&](const std::string& root) -> CefRefPtr<CefResourceHandler>
        {
            if (root.empty()) return nullptr;
            const std::string diskPath = root + "/" + virtualPath;
            std::ifstream ifs(diskPath, std::ios::binary | std::ios::ate);
            if (!ifs.good()) return nullptr;
            const auto size = ifs.tellg();
            ifs.seekg(0);
            std::vector<uint8_t> fileData(static_cast<std::size_t>(size));
            ifs.read(reinterpret_cast<char*>(fileData.data()), size);
            if (!ifs) return nullptr;
            return new MitiruCefResourceHandler(
                std::span<const uint8_t>(fileData), mime);
        };

        if (auto h = tryRead(s_assetRoot)) return h;
        for (const auto& root : s_extraAssetRoots)
        {
            if (auto h = tryRead(root)) return h;
        }

        // 3. 404: nullptr を返すと CEF がクラッシュするので空ハンドラーを返す
        return new MitiruCefResourceHandler({}, "text/plain");
    }

private:
    static inline std::string              s_assetRoot;
    static inline std::vector<std::string> s_extraAssetRoots;
    IMPLEMENT_REFCOUNTING(MitiruCefSchemeHandlerFactory);
};

/// @brief "app" スキームを CEF に登録する
/// @details MitiruCefContext::initialize() の CefInitialize 前に呼ぶこと。
///          またはブラウザプロセスの OnContextInitialized で呼ぶ。
inline void registerAppScheme()
{
    CefRegisterSchemeHandlerFactory(
        "app", "", new MitiruCefSchemeHandlerFactory());
}

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
