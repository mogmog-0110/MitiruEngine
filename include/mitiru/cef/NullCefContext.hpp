#pragma once

/// @file NullCefContext.hpp
/// @brief MitiruCefContext と同一 API を持つ no-op スタブ
///
/// MITIRU_HAS_CEF が未定義の場合 (非Windows / CEF なしビルド) に
/// Engine.hpp が #include するファイル。
/// すべてのメソッドはコンパイル時に消え、ランタイムコストゼロ。

#include <functional>
#include <string>
#include <string_view>

// InputState は CEF 無しでも存在する
#include <mitiru/input/InputState.hpp>

// cef::json (= nlohmann::json) は CEF 非依存。CEF あり build では StateStore 等が
// alias を定義するが、CEF 無し build では Engine の inspector 集約 (cef::json) が
// 解決できるよう、ここで unconditionally に定義する。重複 alias は同一なので無害。
#include <nlohmann/json.hpp>

// DX12 型の前方宣言 (Windows 非依存ヘッダーから参照してもよい)
#if defined(_WIN32)
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
namespace mitiru::gfx::dx12 { class Dx12Device; }
#endif

namespace mitiru::cef
{

/// nlohmann::json の alias。CEF あり build (StateStore 等) と同名。
using json = ::nlohmann::json;

/// @brief CEF 無しビルド向けの no-op ファサード
/// @details MitiruCefContext と同じ public API を持つ。
///          Engine.hpp は `#ifdef MITIRU_HAS_CEF` の代わりに
///          型エイリアスで切り替えることで #ifdef の氾濫を防ぐ。
///
/// ```cpp
/// // Engine.hpp 側
/// #if defined(_WIN32) && defined(MITIRU_HAS_CEF)
///   #include <mitiru/cef/MitiruCefContext.hpp>
///   using CefContext = mitiru::cef::MitiruCefContext;
/// #else
///   #include <mitiru/cef/NullCefContext.hpp>
///   using CefContext = mitiru::cef::NullCefContext;
/// #endif
/// ```
class NullCefContext
{
public:
    using HandlerFn = std::function<std::string(std::string_view)>;

    NullCefContext()  = default;
    ~NullCefContext() = default;

    NullCefContext(const NullCefContext&)            = delete;
    NullCefContext& operator=(const NullCefContext&) = delete;
    NullCefContext(NullCefContext&&)                 = delete;
    NullCefContext& operator=(NullCefContext&&)      = delete;

    // ── ライフサイクル ────────────────────────────────────────
#if defined(_WIN32)
    bool initialize(
        mitiru::gfx::dx12::Dx12Device& /*device*/,
        const std::string&             /*exeDir*/,
        const std::string&             /*logPath*/,
        int                            /*width*/,
        int                            /*height*/,
        const std::string&             /*startUrl*/ = "about:blank")
    {
        return false; // CEF なし → 常に失敗
    }

    /// @brief 生ハンドル版。本物と同じ入口を、CEF 無しビルドにも用意しておく
    /// @details 片方にしかない入口は、CEF ありでは通ってなしでは落ちるコードを許してしまう。
    bool initialize(
        ID3D12Device*       /*device*/,
        ID3D12CommandQueue* /*queue*/,
        const std::string&  /*exeDir*/,
        const std::string&  /*logPath*/,
        int                 /*width*/,
        int                 /*height*/,
        const std::string&  /*startUrl*/            = "about:blank",
        int                 /*remoteDebuggingPort*/ = 0)
    {
        return false; // CEF なし → 常に失敗
    }
#endif

    void shutdown()           {}
    void doMessageLoopWork()  {}

    // ── 毎フレーム API ────────────────────────────────────────
    [[nodiscard]] bool hasDirtyFrame() const { return false; }
    void upload()                            {}

#if defined(_WIN32)
    void composite(
        ID3D12GraphicsCommandList*  /*cmdList*/,
        D3D12_CPU_DESCRIPTOR_HANDLE /*rtvHandle*/,
        int                         /*width*/,
        int                         /*height*/)
    {}

    void recordComposite(
        ID3D12GraphicsCommandList*  /*cl*/,
        D3D12_CPU_DESCRIPTOR_HANDLE /*rtvHandle*/,
        int                         /*width*/,
        int                         /*height*/,
        const float                 /*clearRGBA*/[4] = nullptr)
    {}

    void composite(
        D3D12_CPU_DESCRIPTOR_HANDLE /*rtvHandle*/,
        int                         /*width*/,
        int                         /*height*/,
        const float                 /*clearRGBA*/[4] = nullptr)
    {}

    void resize(
        mitiru::gfx::dx12::Dx12Device& /*device*/,
        int /*width*/,
        int /*height*/)
    {}

    void resize(int /*width*/, int /*height*/) {}
#endif

    void handleInput(const InputState& /*input*/) {}
    void setInputEnabled(bool /*enabled*/) noexcept {}
    [[nodiscard]] bool isInputEnabled() const noexcept { return false; }
    void setVisible(bool /*visible*/) noexcept {}
    [[nodiscard]] bool isVisible() const noexcept { return false; }

    // ── ナビゲーション ────────────────────────────────────────
    void loadUrl(const std::string& /*url*/)                              {}
    void setAllowRemoteUrls(bool /*allow*/) noexcept                      {}
    [[nodiscard]] bool allowRemoteUrls() const noexcept { return false; }
    void loadHtml(const std::string& /*html*/,
                  const std::string& /*baseUrl*/ = "about:blank")        {}
    void executeJavaScript(const std::string& /*code*/)                  {}

    // ── ナビゲーション コールバック ──────────────────────────
    void setLoadEndCallback(std::function<void(std::string_view /*url*/)> /*cb*/) {}

    // ── ブリッジ ──────────────────────────────────────────────
    void registerHandler(const std::string& /*name*/, HandlerFn /*fn*/)  {}
    void unregisterHandler(const std::string& /*name*/)                  {}
    void unregisterAll()                                                  {}

    // ── フレームレート制御 ────────────────────────────────────────
    void setWindowlessFrameRate(int /*fps*/) {}
    [[nodiscard]] int windowlessFrameRate() const noexcept { return 0; }

    // ── 描画統計 ────────────────────────────────────────────────
    struct PaintStats
    {
        uint64_t paintCount     = 0;
        uint64_t totalNanos     = 0;
        uint64_t lastPaintBytes = 0;
        uint64_t lastDirtyArea  = 0;
    };
    [[nodiscard]] PaintStats paintStats() const noexcept { return {}; }

    // ── アクセサー ────────────────────────────────────────────
    [[nodiscard]] bool isInitialized() const { return false; }
    [[nodiscard]] bool isLoading()     const { return false; }
    [[nodiscard]] bool hasError()      const { return false; }
};

} // namespace mitiru::cef
