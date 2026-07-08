#pragma once

/// @file MitiruCefContext.hpp
/// @brief Engine 向け CEF トップレベルファサード
///
/// 使い方:
///   // 初期化
///   MitiruCefContext ctx;
///   ctx.initialize(exeDir, logPath, width, height);
///   ctx.loadUrl("file:///assets/ui/title.html");
///   ctx.registerHandler("menu.start", [](std::string_view) { return "{}"; });
///
///   // 毎フレーム
///   ctx.doMessageLoopWork();
///   if (ctx.hasDirtyFrame())
///       ctx.upload();                 // CPU → GPU 転送
///   ctx.composite(cmdList, rtv, w, h); // アルファブレンドで合成
///   ctx.handleInput(inputState);
///
///   // 終了
///   ctx.shutdown();

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// windowsx.h は GetFirstChild/GetNextSibling を Win32 マクロとして定義する。
// CEF の cef_dom.h に同名の仮想メソッドがあるため、CEF include 前に undef する。
#ifdef GetFirstChild
#  undef GetFirstChild
#endif
#ifdef GetNextSibling
#  undef GetNextSibling
#endif

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "include/cef_app.h"
#include "include/cef_base.h"

#include <mitiru/cef/MitiruCefApp.hpp>
#include <mitiru/cef/StateStore.hpp>
#include <mitiru/cef/MitiruCefBridge.hpp>
#include <mitiru/cef/MitiruCefBrowser.hpp>
#include <mitiru/cef/MitiruCefClient.hpp>
#include <mitiru/cef/MitiruCefConfig.hpp>
#include <mitiru/cef/MitiruCefInput.hpp>
#include <mitiru/cef/MitiruCefTexture.hpp>
#include <mitiru/input/InputState.hpp>

#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>

namespace mitiru::cef
{

/// @brief Engine が直接使う CEF コンテキスト
/// @details CEF 初期化・メッセージポンプ・ブラウザライフサイクル・
///          テクスチャ合成・入力転送をすべて内包する。
///          インスタンスは Engine が 1 つ所有する (シーン間で再利用)。
class MitiruCefContext
{
public:
    using HandlerFn = MitiruCefBridge::HandlerFn;

    MitiruCefContext()  = default;
    ~MitiruCefContext() = default;

    // コピー/ムーブ禁止 (CEF は単一インスタンスを想定)
    MitiruCefContext(const MitiruCefContext&)            = delete;
    MitiruCefContext& operator=(const MitiruCefContext&) = delete;
    MitiruCefContext(MitiruCefContext&&)                 = delete;
    MitiruCefContext& operator=(MitiruCefContext&&)      = delete;

    // ── ライフサイクル ────────────────────────────────────────

    /// @brief CEF を初期化してブラウザを生成する
    /// @param device              DX12 デバイス (テクスチャ/パイプライン作成用)
    /// @param exeDir              実行ファイルのあるディレクトリ (CEF helper exe の検索に使う)
    /// @param logPath             CEF ログファイルパス
    /// @param width               初期ビューポート幅
    /// @param height              初期ビューポート高さ
    /// @param startUrl            最初に開く URL (省略時 about:blank)
    /// @param remoteDebuggingPort 0 以外にすると chrome-devtools MCP が
    ///                            `http://localhost:<port>` 経由で attach できる (E-02)。
    ///                            開発ビルドのみで使うこと。
    /// @return 成功したか
    bool initialize(
        mitiru::gfx::Dx12Device& device,
        const std::string&             exeDir,
        const std::string&             logPath,
        int                            width,
        int                            height,
        const std::string&             startUrl            = "about:blank",
        int                            remoteDebuggingPort = 0)
    {
        if (m_initialized)
        {
            return true;
        }

        // CefMainArgs — HINSTANCE は GetModuleHandle(nullptr) で取得
        CefMainArgs mainArgs(::GetModuleHandle(nullptr));

        const auto settings = buildCefSettings(
            exeDir, "MitiruCefHelper.exe", logPath, remoteDebuggingPort);

        // app:// ディスクフォールバック — 埋め込みアセットが未生成の場合にディスクから提供
        // (exeDir)/assets/ が POST_BUILD でコピーされているため常に有効
        MitiruCefSchemeHandlerFactory::setAssetRoot(exeDir + "/assets");

        CefRefPtr<MitiruCefApp> app = new MitiruCefApp();
        if (!CefInitialize(mainArgs, settings, app, nullptr))
        {
            std::fprintf(stderr,
                "[mitiru][cef] CefInitialize failed (log: %s)\n", logPath.c_str());
            return false;
        }

        m_client = new MitiruCefClient();

        // 起動 URL も loadUrl と同じ allowlist を通す (C-5)
        std::string effectiveUrl = startUrl;
        if (!isUrlAllowed(effectiveUrl))
        {
            std::fprintf(stderr,
                "[mitiru][cef] start URL denied (remote scheme, opt-in: allowRemoteUrls): %s\n",
                effectiveUrl.c_str());
            effectiveUrl = "about:blank";
        }

        if (!m_browser.create(m_client, width, height, effectiveUrl))
        {
            std::fprintf(stderr, "[mitiru][cef] browser create failed\n");
            closeAndShutdownCef();
            return false;
        }

        if (!m_texture.initialize(device, width, height))
        {
            std::fprintf(stderr,
                "[mitiru][cef] UI texture init failed (%dx%d)\n", width, height);
            closeAndShutdownCef();
            return false;
        }

        m_width       = width;
        m_height      = height;
        m_initialized = true;
        return true;
    }

    /// @brief CEF をシャットダウンする
    /// @details ブラウザを閉じ、CefShutdown() を呼ぶ。
    ///          二重呼び出しは安全。
    void shutdown()
    {
        if (!m_initialized)
        {
            return;
        }
        m_initialized = false;
        closeAndShutdownCef();
    }

    // ── 毎フレーム API ────────────────────────────────────────

    /// @brief CEF メッセージポンプを 1 回実行する
    /// @details ゲームループの先頭で呼ぶこと。
    void doMessageLoopWork()
    {
        if (m_initialized)
        {
            CefDoMessageLoopWork();
        }
    }

    /// @brief 新しいフレームが届いているか
    [[nodiscard]] bool hasDirtyFrame() const
    {
        if (!m_initialized || !m_client)
        {
            return false;
        }
        return m_client->renderHandler()->isDirty();
    }

    /// @brief 新フレームを GPU テクスチャにアップロードする
    /// @details hasDirtyFrame() が true の時だけ呼ぶ (不要な転送を避ける)。
    ///          dirty rect リストが空でなければ部分アップロードを試みる。
    ///          70% 閾値を超える場合はフルアップロードにフォールバックする。
    void upload()
    {
        if (!m_initialized || !m_client)
        {
            return;
        }
        const auto frame = m_client->renderHandler()->takePixels();
        if (!frame.valid)
        {
            return;
        }
        if (frame.dirtyRects.empty())
        {
            // dirty rect 情報がない場合はフルアップロード
            m_texture.upload(frame.data, frame.width, frame.height);
        }
        else
        {
            m_texture.uploadPartial(
                frame.data, frame.width, frame.height,
                std::span<const CefRect>(frame.dirtyRects));
        }
    }

    /// @brief UI レイヤーをバックバッファにアルファ合成する
    /// @details スワップチェーンから現在の RTV を取得して自己完結型の
    ///          コマンドリストで描画する。
    /// @param device  DX12 デバイス (スワップチェーン RTV 取得用)
    /// @param width   バックバッファ幅
    /// @param height  バックバッファ高さ
    void composite(
        mitiru::gfx::Dx12Device& device,
        int width,
        int height,
        const float clearRGBA[4] = nullptr)
    {
        if (!m_initialized || !m_visible)
        {
            return; // 非表示 → backbuffer に重ね合わせない
        }
        auto* swap = device.getSwapChain();
        if (!swap)
        {
            return;
        }
        auto* rt = static_cast<mitiru::gfx::Dx12RenderTarget*>(swap->backBuffer());
        if (!rt)
        {
            return;
        }
        m_texture.composite(rt->rtvHandle(), width, height, clearRGBA);
    }

    // ── フレームレート制御 ────────────────────────────────────────

    /// @brief CEF OSR のターゲットフレームレートを変更する
    /// @details シーンに応じて動的に調整できる。
    ///          VN のようなほぼ静的な UI では低め (30fps)、
    ///          リアルタイム HUD では高め (90fps) に設定することで
    ///          CPU/GPU 負荷を削減できる。
    ///
    /// @param fps  目標フレームレート。[1, 120] の範囲にクランプされる。
    ///             ブラウザ未作成の場合は値を保持し、作成後に適用される
    ///             (ただし現時点では initialize() 後に呼ぶことを想定)。
    ///
    /// 使用例:
    /// ```cpp
    /// ctx.setWindowlessFrameRate(30); // VN シーン
    /// ctx.setWindowlessFrameRate(90); // アクション HUD
    /// ```
    void setWindowlessFrameRate(int fps)
    {
        fps = (fps < 1) ? 1 : (fps > 120) ? 120 : fps;
        m_currentFrameRate = fps;
        if (!m_initialized || !m_client)
        {
            return; // ブラウザ未作成 — 値のみ保持
        }
        auto browser = m_browser.browser();
        if (!browser)
        {
            return;
        }
        browser->GetHost()->SetWindowlessFrameRate(fps);
    }

    /// @brief 現在設定されているターゲットフレームレートを返す
    [[nodiscard]] int windowlessFrameRate() const noexcept { return m_currentFrameRate; }

    // ── 描画統計 ────────────────────────────────────────────────

    /// @brief OnPaint の累積統計スナップショット
    struct PaintStats
    {
        uint64_t paintCount;      ///< OnPaint が呼ばれた累計回数
        uint64_t totalNanos;      ///< OnPaint の累計処理時間 (ナノ秒)
        uint64_t lastPaintBytes;  ///< 直近 OnPaint で触れたバイト数 (dirty area * 4)
        uint64_t lastDirtyArea;   ///< 直近 OnPaint の dirty ピクセル面積
    };

    /// @brief 描画統計を返す
    /// @details RenderHandler が存在しない場合はすべてゼロを返す。
    ///
    /// 使用例:
    /// ```cpp
    /// const auto s = ctx.paintStats();
    /// float avgMs = s.paintCount > 0
    ///     ? static_cast<float>(s.totalNanos) / s.paintCount / 1e6f
    ///     : 0.0f;
    /// ```
    [[nodiscard]] PaintStats paintStats() const noexcept
    {
        if (!m_client || !m_client->renderHandler())
        {
            return {};
        }
        const auto* rh = m_client->renderHandler();
        return {
            rh->paintCount(),
            rh->totalPaintNanos(),
            rh->lastPaintBytes(),
            rh->lastDirtyArea()
        };
    }

    /// @brief CEF overlay をバックバッファに合成するかを切替える
    /// @details 非表示中も CEF 本体は動き続ける (message loop / ページ state は
    ///          維持)。次に visible=true に戻した瞬間から即座に overlay が
    ///          復帰する。シーン切替で overlay を一時的に隠したい時用。
    void setVisible(bool visible) noexcept { m_visible = visible; }

    /// @brief 現在 overlay が合成されるかを返す
    [[nodiscard]] bool isVisible() const noexcept { return m_visible; }

    /// @brief 入力イベントを CEF に転送する
    /// @details OnPaint が一度も来ていない場合はスキップする。
    ///          CEF レンダープロセスが起動完了する前に SendMouseMoveEvent を
    ///          呼ぶとクラッシュするため、初回描画まで入力転送を保留する。
    ///          `setInputEnabled(false)` が呼ばれている間はスキップされる
    ///          (ハイブリッド構成で native レイヤーに入力を渡したい時用)。
    void handleInput(const InputState& input)
    {
        if (!m_initialized || !m_client)
        {
            return;
        }
        if (!m_inputEnabled)
        {
            return; // フォーカスが native 側 — CEF への転送を抑止
        }
        if (!m_client->renderHandler()->hasEverPainted())
        {
            return; // レンダープロセス未起動 — 入力は保留
        }
        // H-08: 初回ペイント完了後に一度だけキーボードフォーカスを自動取得する。
        //       OSR モードでは host->SetFocus(true) を呼ばないとキー入力が
        //       ドキュメントに届かないため、ゲーム側のボイラープレートを排除。
        if (m_autoFocusOnFirstPaint && !m_focusClaimedOnce)
        {
            m_browser.claimKeyboardFocus();
            m_focusClaimedOnce = true;
        }
        m_input.update(m_browser.host(), input, &m_texture);
    }

    /// @brief CEF への入力転送を有効/無効にする
    /// @details ハイブリッド UI で native シーンがフォーカスを持つ時は false。
    ///          デフォルトは true (既存の pure-CEF ゲーム互換)。
    ///          H-08: false → true 遷移時、ブラウザが初期化済みなら
    ///          自動でキーボードフォーカスを取得する (`setAutoFocusOnFirstPaint(false)`
    ///          で抑止可能)。
    void setInputEnabled(bool enabled)
    {
        const bool wasEnabled = m_inputEnabled;
        m_inputEnabled = enabled;
        if (enabled && !wasEnabled && m_initialized && m_autoFocusOnFirstPaint)
        {
            m_browser.claimKeyboardFocus();
        }
    }

    /// @brief 現在 CEF が入力を受け取るかを返す
    [[nodiscard]] bool isInputEnabled() const noexcept { return m_inputEnabled; }

    /// @brief 初回ペイント時 / setInputEnabled(true) 時にキーボードフォーカスを
    ///        自動取得するかを設定する (H-08)
    /// @details デフォルト true。手動制御したいゲームは false にし、
    ///          任意のタイミングで `cefContext()->browser().claimKeyboardFocus()`
    ///          を呼ぶ。
    void setAutoFocusOnFirstPaint(bool enable) noexcept { m_autoFocusOnFirstPaint = enable; }

    /// @brief 自動フォーカス設定を返す
    [[nodiscard]] bool autoFocusOnFirstPaint() const noexcept { return m_autoFocusOnFirstPaint; }

    /// @brief MitiruCefBrowser への参照を返す (上級者向け — claimKeyboardFocus 等)
    [[nodiscard]] MitiruCefBrowser&       browser()       noexcept { return m_browser; }
    [[nodiscard]] const MitiruCefBrowser& browser() const noexcept { return m_browser; }

    // ── ナビゲーション ────────────────────────────────────────

    /// @brief URL に遷移する
    /// @details scheme allowlist (C-5): app:// / file:// / data: / about: のみ。
    ///          http(s) 等のリモート URL は setAllowRemoteUrls(true) の明示
    ///          opt-in が無い限り deny (stderr 1 行)。
    void loadUrl(const std::string& url)
    {
        if (!m_initialized)
        {
            return;
        }
        if (!isUrlAllowed(url))
        {
            std::fprintf(stderr,
                "[mitiru][cef] loadUrl denied (remote scheme, opt-in: allowRemoteUrls): %s\n",
                url.c_str());
            return;
        }
        m_browser.loadUrl(url);
    }

    /// @brief リモート URL (http/https 等) の読込を許可する (既定 false)
    void setAllowRemoteUrls(bool allow) noexcept { m_allowRemoteUrls = allow; }

    /// @brief リモート URL が許可されているかを返す
    [[nodiscard]] bool allowRemoteUrls() const noexcept { return m_allowRemoteUrls; }

    /// @brief HTML 文字列を直接読み込む
    void loadHtml(const std::string& html, const std::string& baseUrl = "about:blank")
    {
        if (m_initialized)
        {
            m_browser.loadHtml(html, baseUrl);
        }
    }

    /// @brief JavaScript を実行する
    void executeJavaScript(const std::string& code)
    {
        if (m_initialized)
        {
            m_browser.executeJavaScript(code);
        }
    }

    // ── JS ↔ C++ ブリッジ ────────────────────────────────────

    /// @brief JS ハンドラーを登録する
    /// @param name     ハンドラー名 (JS 側 request フィールドに使う)
    /// @param fn       コールバック (payload → response json)
    void registerHandler(const std::string& name, HandlerFn fn)
    {
        if (m_initialized && m_client)
        {
            m_client->bridge()->registerHandler(name, std::move(fn));
        }
    }

    /// @brief JS ハンドラーを解除する
    void unregisterHandler(const std::string& name)
    {
        if (m_initialized && m_client)
        {
            m_client->bridge()->unregisterHandler(name);
        }
    }

    /// @brief OnLoadEnd コールバックを設定する (G-17 SceneTransition 用)
    /// @details ページロード完了時に url を引数に呼ばれる。1 本だけ保持 (上書き置換)。
    void setLoadEndCallback(std::function<void(std::string_view /*url*/)> cb)
    {
        if (m_initialized && m_client && m_client->loadHandler())
        {
            m_client->loadHandler()->setOnLoadEndCallback(std::move(cb));
        }
    }

    /// @brief StateStore を生成し、OnLoadEnd で retained state を自動再送する。
    /// @details ページロード完了時に StateStore::replayRetainedState() が
    ///          呼ばれるため、ゲーム側のハートビート再送ハックが不要になる。
    ///
    /// **使い方:**
    /// ```cpp
    ///   auto store = ctx.makeStateStore();
    ///   store->set("stats.hp", 100);   // 以降ロード完了時に自動再送
    /// ```
    [[nodiscard]] std::shared_ptr<StateStore> makeStateStore()
    {
        auto store = std::make_shared<StateStore>(
            [this](const std::string& js) { executeJavaScript(js); },
            [this](const std::string& name, HandlerFn fn)
            {
                registerHandler(name, std::move(fn));
            });

        // weak 捕捉 — store が context より先に死んでもロード完了時は no-op
        // (H-19: 生ポインタ捕捉による UAF を構造で排除)。
        std::weak_ptr<StateStore> weak = store;
        setLoadEndCallback([weak](std::string_view /*url*/)
        {
            if (auto s = weak.lock())
            {
                s->replayRetainedState();
            }
        });

        return store;
    }

    /// @brief 全 JS ハンドラーを解除する
    void unregisterAll()
    {
        if (m_initialized && m_client)
        {
            m_client->bridge()->unregisterAll();
        }
    }

    // ── リサイズ ──────────────────────────────────────────────

    /// @brief ビューポートをリサイズする
    /// @details non-blocking。composite は texture dim を viewport にするので
    ///          (MitiruCefTexture::composite 参照) CEF が catch-up するまでは
    ///          texture が old size のまま old content を 1:1 表示し、余白に
    ///          engine clear color が見える (letterbox)。CEF が新 dim で paint
    ///          したら自動的に viewport が拡大する。stretch / 判定ズレなし。
    void resize(
        mitiru::gfx::Dx12Device& device,
        int width,
        int height)
    {
        if (!m_initialized || (width == m_width && height == m_height))
        {
            return;
        }
        m_width  = width;
        m_height = height;
        m_browser.resize(width, height); // WasResized + Invalidate
        m_texture.resize(device, width, height); // pending mark
    }

    // ── アクセサー ────────────────────────────────────────────
    [[nodiscard]] bool isInitialized()  const { return m_initialized; }
    [[nodiscard]] bool isLoading()      const
    {
        if (!m_initialized || !m_client)
        {
            return false;
        }
        return m_client->loadHandler()->isLoading();
    }
    [[nodiscard]] bool hasError()       const
    {
        if (!m_initialized || !m_client)
        {
            return false;
        }
        return m_client->loadHandler()->hasError();
    }

    /// @brief 生の CefBrowser 参照を取得する (上級者向け)
    [[nodiscard]] CefRefPtr<CefBrowser> rawBrowser() const
    {
        return m_browser.browser();
    }

private:
    /// @brief allowlist 判定 — ローカル scheme のみ許可 (C-5)
    [[nodiscard]] bool isUrlAllowed(const std::string& url) const noexcept
    {
        if (m_allowRemoteUrls)
        {
            return true;
        }
        return url.rfind("app://", 0) == 0
            || url.rfind("file://", 0) == 0
            || url.rfind("data:", 0) == 0
            || url.rfind("about:", 0) == 0;
    }

    /// @brief browser close → OnBeforeClose 待ちポンプ → ref 解放 → CefShutdown
    /// @details shutdown() と initialize() 失敗パスで共通。ポンプ無しの即
    ///          CefShutdown は shutdown_checker assert / browser リークになる。
    ///          browser 未生成でも安全 (close / onClosed は null ガード済み)。
    void closeAndShutdownCef()
    {
        m_browser.close();
        // LifeSpanHandler::OnBeforeClose が発火するまで待機する
        // isCreated() が false になった = ブラウザが実際に閉じられた
        for (int i = 0; i < 100 &&
             m_client && m_client->lifespanHandler()->isCreated(); ++i)
        {
            CefDoMessageLoopWork();
        }

        // CefShutdown 前にすべての CefRefPtr を解放する (shutdown_checker 対策)
        m_browser.onClosed(); // m_browser / m_host を null に
        m_client = nullptr;   // renderHandler / lifespanHandler 等を解放
        CefShutdown();
    }

    bool                       m_initialized           = false;
    bool                       m_allowRemoteUrls       = false; ///< C-5: http(s) 読込の明示 opt-in
    bool                       m_inputEnabled          = true;  ///< false なら handleInput が CEF への転送をスキップ
    bool                       m_visible               = true;  ///< false なら composite が早期 return (overlay 非表示)
    bool                       m_autoFocusOnFirstPaint = true;  ///< H-08: 初回ペイント / setInputEnabled(true) で自動 SetFocus
    bool                       m_focusClaimedOnce      = false; ///< H-08: handleInput 経由の自動フォーカスを 1 回だけ走らせるガード
    int                        m_width  = 0;
    int                        m_height = 0;
    int                        m_currentFrameRate = 90; ///< SetWindowlessFrameRate に渡す目標 fps (デフォルト 90)
    CefRefPtr<MitiruCefClient> m_client;
    MitiruCefBrowser           m_browser;
    MitiruCefTexture           m_texture;
    MitiruCefInput             m_input;
};

} // namespace mitiru::cef

#endif // _WIN32 && MITIRU_HAS_CEF
