#pragma once

/// @file SceneTransition.hpp
/// @brief JS タイマー駆動の fade/dissolve overlay による CEF scene transition (G-17)
///
/// **なぜ callback-injection か。** この header は `MitiruCefContext.hpp` を一切
/// include しないため、G-16 (AudioBridge) で問題を起こした windows.h/min-max
/// マクロ汚染と無縁。ゲームは起動時に短い lambda を 3 つ配線するだけで、以後は
/// どこからでも `transitionTo()` を呼べる。
///
/// **登録される handler (JS → C++):**
/// - `__mitiru_scene_next__`  (内部用)。fade-out 前半が完了した後に発火;
///                             handler が `loadUrl(pendingUrl)` を呼び "{}" を返す。
///
/// **プロトコル (timer 駆動):**
/// - `Transition::None` または `duration_ms <= 0`: 即座に `loadUrl(url)` を呼ぶ。
/// - `Transition::Fade`:  CSS transition で `duration_ms/2` ms かけて黒へ fade
///   する overlay `<div>` を注入する。同じ遅延の後に `setTimeout` が発火し
///   `cefQuery({request:'__mitiru_scene_next__'})` を dispatch する。C++ handler
///   が `loadUrl(pendingUrl)` を呼ぶ。`onLoadEnd(pendingUrl)` で `duration_ms` の
///   後半をかけて overlay を除去する fade-in を注入する。
/// - `Transition::Dissolve`: Fade と同一だが、単色黒 overlay の代わりに
///   radial-gradient overlay を使う (視覚効果は異なるが timing ロジックは同じ)。
///
/// **並行性。** state は `shared_ptr<SceneTransitionState>` に保持される。
/// `transitionTo()` の各呼び出しは進行中の transition をキャンセルする (置き換え
/// られた URL からの古い `onLoadEnd` は `pendingUrl` がもう一致しないので no-op)。
///
/// **使い方:**
/// ```cpp
///   auto* ctx = engine.cefContext();
///   mitiru::cef::CefTransitionDeps deps{
///       [ctx](std::string_view js)  { ctx->executeJavaScript(std::string(js)); },
///       [ctx](std::string_view url) { ctx->loadUrl(std::string(url)); },
///       [ctx](std::string_view name, auto fn) {
///           ctx->registerHandler(std::string(name), std::move(fn));
///       }
///   };
///   auto st = mitiru::cef::bindSceneTransition(deps);
///
///   // Wire OnLoadEnd (MitiruCefLoadHandler callback, added in G-17):
///   loadHandler->setOnLoadEndCallback([st](std::string_view url) {
///       st->onLoadEnd(url);
///   });
///
///   // Transition from any code path:
///   mitiru::cef::transitionTo(deps, st, "file:///page2.html",
///                             mitiru::cef::Transition::Fade, 600);
/// ```

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace mitiru::cef
{

// ── Transition の種類 ───────────────────────────────────────────────────────────

/// @brief CEF scene transition の視覚スタイル。
enum class Transition
{
    None,      ///< 即時 URL 切り替え、overlay 無し
    Fade,      ///< 単色黒 overlay が fade out してから fade in する
    Dissolve,  ///< radial-gradient overlay (timing は同じ、見た目が異なる)
};

// ── 依存バッグ (callback-injection) ──────────────────────────────────────

/// @brief scene-transition state machine が駆動する callback 群。
/// @details `MitiruCefContext` API を写すが、その header の include は避ける。
///          実 context を包む lambda を渡す。test では mock capture を渡す。
struct CefTransitionDeps
{
    /// 現在読み込まれている page で任意の JavaScript を実行する。
    std::function<void(std::string_view js)> executeJs;

    /// browser を新しい URL に遷移させる。
    std::function<void(std::string_view url)> loadUrl;

    /// 名前付き cefQuery handler を登録する (`window.cefQuery({request:'name|payload'})`)。
    std::function<void(
        std::string_view name,
        std::function<std::string(std::string_view payload)> fn)> registerHandler;
};

// ── 内部 state holder ────────────────────────────────────────────────────

/// @brief caller が `shared_ptr` 経由で所有する可変 transition state。
/// @details 1 インスタンスが `bindSceneTransition()` で生成される。複数の
///          `transitionTo()` 呼び出しが同じ state を共有する; 新しい transition
///          を開始すると pending URL を atomic に置き換えるため、古い
///          `onLoadEnd` callback は no-op になる。
class SceneTransitionState
{
public:
    SceneTransitionState() = default;

    // copy 不可、move 不可 (所有権は shared_ptr が扱う)
    SceneTransitionState(const SceneTransitionState&)            = delete;
    SceneTransitionState& operator=(const SceneTransitionState&) = delete;

    // ── transitionTo() から呼ばれる ─────────────────────────────────────────

    /// @brief 指定した視覚種別で `url` への transition を開始する。
    /// @details 進行中の transition を atomic に置き換える (前のをキャンセル)。
    void beginTransition(
        const CefTransitionDeps& deps,
        std::string              url,
        Transition               kind,
        int                      durationMs)
    {
        // 即時ケース: 遷移するだけ。
        if (kind == Transition::None || durationMs <= 0)
        {
            cancelPending();
            deps.loadUrl(url);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingUrl  = std::move(url);
            m_kind        = kind;
            m_durationMs  = durationMs;
            m_generation += 1; // 古い onLoadEnd を無効化する
        }

        // overlay + duration/2 ms 後に cefQuery を発火する setTimeout を注入する
        const std::string js = buildFadeOutJs(kind, durationMs);
        deps.executeJs(js);
    }

    /// @brief `__mitiru_scene_next__` cefQuery handler から呼ばれる。
    /// @details pending URL に遷移する。JS caller に "{}" を返す。
    std::string handleSceneNext(const CefTransitionDeps& deps,
                                std::string_view /*payload*/)
    {
        std::string urlCopy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pendingUrl.empty())
            {
                return "{}";
            }
            urlCopy = m_pendingUrl;
        }
        deps.loadUrl(urlCopy);
        return "{}";
    }

    /// @brief browser が URL の読み込みを完了したときに呼ばれる。
    /// @details `url` が pending URL と一致すれば fade-in JS を注入し pending
    ///          state をクリアする。一致しない URL は黙って無視する (address bar
    ///          からの遷移や redirect 等をカバーする)。
    void onLoadEnd(const CefTransitionDeps& deps, std::string_view url)
    {
        std::string pendingCopy;
        Transition  kind;
        int         durationMs;
        uint64_t    generation;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pendingUrl.empty() || m_pendingUrl != url)
            {
                return; // 自分の pending URL ではない — 無視
            }
            pendingCopy = m_pendingUrl;
            kind        = m_kind;
            durationMs  = m_durationMs;
            generation  = m_generation;
            m_pendingUrl.clear();
        }
        (void)pendingCopy;
        (void)generation;

        // fade-in (overlay 除去) を注入する
        const std::string js = buildFadeInJs(kind, durationMs);
        deps.executeJs(js);
    }

    /// @brief 遷移せずに進行中の transition をキャンセルする。
    void cancelPending()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingUrl.clear();
        m_generation += 1;
    }

    /// @brief pending URL を返す (進行中が無ければ空)。
    [[nodiscard]] std::string pendingUrl() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pendingUrl;
    }

private:
    // ── JS 生成 helper ─────────────────────────────────────────────

    /// @brief JS のシングルクォート文字列リテラルに埋め込むため文字列を escape する。
    /// @details backslash とシングルクォートを escape する。
    static std::string jsEscape(std::string_view s)
    {
        std::string out;
        out.reserve(s.size() + 4);
        for (const char c : s)
        {
            if (c == '\\') { out += "\\\\"; }
            else if (c == '\'') { out += "\\'"; }
            else { out += c; }
        }
        return out;
    }

    /// @brief 指定した transition 種別に対する overlay 背景 CSS。
    static std::string overlayBackground(Transition kind)
    {
        switch (kind)
        {
        case Transition::Dissolve:
            // 半透明の radial gradient。Fade と視覚的に区別できる
            return "radial-gradient(ellipse at center, rgba(0,0,0,0.5) 0%, rgba(0,0,0,1) 100%)";
        case Transition::Fade:
        default:
            return "#000";
        }
    }

    /// @brief overlay div を注入し cefQuery を schedule する JS を組み立てる。
    static std::string buildFadeOutJs(Transition kind, int durationMs)
    {
        const int halfMs = durationMs / 2;
        const std::string bg = overlayBackground(kind);

        // 言語: JavaScript (実行中の page に注入される)
        std::string js;
        js.reserve(512);
        js += "(function(){"
              "var d=document.getElementById('__mitiru_overlay__');"
              "if(!d){d=document.createElement('div');"
              "d.id='__mitiru_overlay__';"
              "d.style.cssText='position:fixed;top:0;left:0;width:100%;height:100%;"
              "background:";
        js += bg;
        js += ";opacity:0;transition:opacity ";
        js += std::to_string(halfMs);
        js += "ms linear;z-index:2147483647;pointer-events:none;';"
              "document.body.appendChild(d);}"
              // transition の前に初期 opacity:0 を描画させるため reflow を強制する
              "void d.offsetWidth;"
              "d.style.opacity='1';"
              "setTimeout(function(){"
              "if(window.cefQuery){"
              "window.cefQuery({request:'__mitiru_scene_next__',"
              "onSuccess:function(){},onFailure:function(){}});}"
              "},";
        js += std::to_string(halfMs);
        js += ");})();";
        return js;
    }

    /// @brief overlay div を fade out させて除去する JS を組み立てる。
    static std::string buildFadeInJs(Transition /*kind*/, int durationMs)
    {
        const int halfMs = durationMs / 2;

        std::string js;
        js.reserve(256);
        js += "(function(){"
              "var d=document.getElementById('__mitiru_overlay__');"
              "if(!d){return;}"
              "d.style.transition='opacity ";
        js += std::to_string(halfMs);
        js += "ms linear';"
              "d.style.opacity='0';"
              "setTimeout(function(){if(d.parentNode){d.parentNode.removeChild(d);}},";
        js += std::to_string(halfMs + 50); // CSS transition 後の +50ms 安全マージン
        js += ");})();";
        return js;
    }

    mutable std::mutex m_mutex;
    std::string        m_pendingUrl;
    Transition         m_kind       = Transition::None;
    int                m_durationMs = 0;
    uint64_t           m_generation = 0; ///< 新しい transition ごとに増える
};

// ── Public API ────────────────────────────────────────────────────────────────

/// @brief `__mitiru_scene_next__` handler を配線し state object を返す。
/// @details `MitiruCefContext` の寿命ごとに 1 回呼ぶ。返される
///          `shared_ptr<SceneTransitionState>` は browser が生きている間ずっと
///          生かしておく必要がある。`transitionTo()` に渡し、その
///          `onLoadEnd()` を `MitiruCefLoadHandler::setOnLoadEndCallback` に配線する。
///
/// @param deps  実 `MitiruCefContext` を包む依存 callback。
/// @return      共有 state; browser の寿命の間生かしておくこと。
[[nodiscard]] inline std::shared_ptr<SceneTransitionState>
bindSceneTransition(const CefTransitionDeps& deps)
{
    auto state = std::make_shared<SceneTransitionState>();

    deps.registerHandler("__mitiru_scene_next__",
        [state, deps](std::string_view payload) -> std::string
        {
            return state->handleSceneNext(deps, payload);
        });

    return state;
}

/// @brief 指定した視覚効果で CEF browser を `url` へ遷移させる。
/// @details transition が既に進行中のときにこれを呼ぶと pending URL を atomic に
///          置き換える。前回の fade-in は決して発火しない。
///
/// @param deps        `bindSceneTransition` に渡したのと同じ deps。
/// @param state       `bindSceneTransition` が返した state。
/// @param url         遷移先 URL (CEF が受け付ける任意の scheme)。
/// @param kind        overlay の視覚スタイル。
/// @param durationMs  transition の合計時間 (ms)。半分が fade-out、半分が
///                    fade-in。`<= 0` は `Transition::None` として扱う。
inline void transitionTo(
    const CefTransitionDeps&                     deps,
    const std::shared_ptr<SceneTransitionState>& state,
    const std::string&                           url,
    Transition                                   kind        = Transition::Fade,
    int                                          durationMs  = 500)
{
    state->beginTransition(deps, url, kind, durationMs);
}

} // namespace mitiru::cef
