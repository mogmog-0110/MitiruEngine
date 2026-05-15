#pragma once

/// @file SceneTransition.hpp
/// @brief CEF scene transition with JS-timer-driven fade/dissolve overlays (G-17)
///
/// **Why callback-injection.** This header never includes `MitiruCefContext.hpp`
/// so it is free of the windows.h/min-max macro pollution that caused issues in
/// G-16 (AudioBridge). The game wires three short lambdas once at startup, then
/// calls `transitionTo()` from anywhere.
///
/// **Registered handler (JS → C++):**
/// - `__mitiru_scene_next__`  (internal) — fired after the fade-out half completes;
///                             the handler calls `loadUrl(pendingUrl)` and returns "{}".
///
/// **Protocol (timer-driven):**
/// - `Transition::None` or `duration_ms <= 0`: calls `loadUrl(url)` immediately.
/// - `Transition::Fade`:  injects an overlay `<div>` that fades to black over
///   `duration_ms/2` ms via CSS transition. A `setTimeout` fires after the same
///   delay and dispatches `cefQuery({request:'__mitiru_scene_next__'})`. The C++
///   handler calls `loadUrl(pendingUrl)`. On `onLoadEnd(pendingUrl)`, injects a
///   fade-in that removes the overlay over the second half of `duration_ms`.
/// - `Transition::Dissolve`: identical to Fade but uses a radial-gradient overlay
///   instead of a solid-black overlay (different visual effect, same timing logic).
///
/// **Concurrency.** State is held in a `shared_ptr<SceneTransitionState>`. Each
/// call to `transitionTo()` cancels any in-flight transition (the stale
/// `onLoadEnd` from a superseded URL is a no-op because `pendingUrl` no longer
/// matches).
///
/// **Usage:**
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

// ── Transition kind ───────────────────────────────────────────────────────────

/// @brief Visual style of the CEF scene transition.
enum class Transition
{
    None,      ///< Instant URL swap, no overlay
    Fade,      ///< Black solid overlay fades out then in
    Dissolve,  ///< Radial-gradient overlay (same timing, different look)
};

// ── Dependency bag (callback-injection) ──────────────────────────────────────

/// @brief Callbacks the scene-transition state machine drives.
/// @details Mirrors `MitiruCefContext` API but avoids any include of that header.
///          Pass lambdas wrapping the real context. In tests pass mock captures.
struct CefTransitionDeps
{
    /// Execute arbitrary JavaScript in the currently loaded page.
    std::function<void(std::string_view js)> executeJs;

    /// Navigate the browser to a new URL.
    std::function<void(std::string_view url)> loadUrl;

    /// Register a named cefQuery handler (`window.cefQuery({request:'name|payload'})`).
    std::function<void(
        std::string_view name,
        std::function<std::string(std::string_view payload)> fn)> registerHandler;
};

// ── Internal state holder ────────────────────────────────────────────────────

/// @brief Mutable transition state owned by the caller via `shared_ptr`.
/// @details One instance is created by `bindSceneTransition()`. Multiple
///          `transitionTo()` calls share the same state; starting a new
///          transition atomically replaces the pending URL so stale
///          `onLoadEnd` callbacks become no-ops.
class SceneTransitionState
{
public:
    SceneTransitionState() = default;

    // Non-copyable, non-movable (shared_ptr handles ownership)
    SceneTransitionState(const SceneTransitionState&)            = delete;
    SceneTransitionState& operator=(const SceneTransitionState&) = delete;

    // ── Called by transitionTo() ─────────────────────────────────────────

    /// @brief Begin a transition towards `url` with the given visual kind.
    /// @details Atomically replaces any pending transition (cancels previous).
    void beginTransition(
        const CefTransitionDeps& deps,
        std::string              url,
        Transition               kind,
        int                      durationMs)
    {
        // Instant case: just navigate.
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
            m_generation += 1; // invalidate any stale onLoadEnd
        }

        // Inject overlay + setTimeout that fires cefQuery after duration/2 ms
        const std::string js = buildFadeOutJs(kind, durationMs);
        deps.executeJs(js);
    }

    /// @brief Called from the `__mitiru_scene_next__` cefQuery handler.
    /// @details Navigates to the pending URL.  Returns "{}" to the JS caller.
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

    /// @brief Called when the browser finishes loading a URL.
    /// @details If `url` matches the pending URL, inject the fade-in JS and
    ///          clear the pending state. Non-matching URLs are silently ignored
    ///          (covers navigation from address bar, redirects, etc.).
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
                return; // not our pending URL — ignore
            }
            pendingCopy = m_pendingUrl;
            kind        = m_kind;
            durationMs  = m_durationMs;
            generation  = m_generation;
            m_pendingUrl.clear();
        }
        (void)pendingCopy;
        (void)generation;

        // Inject fade-in overlay removal
        const std::string js = buildFadeInJs(kind, durationMs);
        deps.executeJs(js);
    }

    /// @brief Cancel any in-flight transition without navigating.
    void cancelPending()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingUrl.clear();
        m_generation += 1;
    }

    /// @brief Returns the pending URL (empty if none in flight).
    [[nodiscard]] std::string pendingUrl() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pendingUrl;
    }

private:
    // ── JS generation helpers ─────────────────────────────────────────────

    /// @brief Escape a string for embedding inside a JS single-quoted literal.
    /// @details Escapes backslashes and single-quotes.
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

    /// @brief Overlay background CSS for the given transition kind.
    static std::string overlayBackground(Transition kind)
    {
        switch (kind)
        {
        case Transition::Dissolve:
            // Semi-transparent radial gradient — visually distinct from Fade
            return "radial-gradient(ellipse at center, rgba(0,0,0,0.5) 0%, rgba(0,0,0,1) 100%)";
        case Transition::Fade:
        default:
            return "#000";
        }
    }

    /// @brief Build JS that injects the overlay div and schedules cefQuery.
    static std::string buildFadeOutJs(Transition kind, int durationMs)
    {
        const int halfMs = durationMs / 2;
        const std::string bg = overlayBackground(kind);

        // Language: JavaScript (injected into the running page)
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
              // Force reflow so the initial opacity:0 is rendered before transition
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

    /// @brief Build JS that fades out and removes the overlay div.
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
        js += std::to_string(halfMs + 50); // +50ms safety margin after CSS transition
        js += ");})();";
        return js;
    }

    mutable std::mutex m_mutex;
    std::string        m_pendingUrl;
    Transition         m_kind       = Transition::None;
    int                m_durationMs = 0;
    uint64_t           m_generation = 0; ///< incremented on each new transition
};

// ── Public API ────────────────────────────────────────────────────────────────

/// @brief Wire up the `__mitiru_scene_next__` handler and return the state object.
/// @details Call once per `MitiruCefContext` lifetime.  The returned
///          `shared_ptr<SceneTransitionState>` must be kept alive as long as the
///          browser is alive. Pass it to `transitionTo()` and wire its
///          `onLoadEnd()` to `MitiruCefLoadHandler::setOnLoadEndCallback`.
///
/// @param deps  Dependency callbacks wrapping the real `MitiruCefContext`.
/// @return      Shared state; keep alive for the browser's lifetime.
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

/// @brief Transition the CEF browser to `url` with the specified visual effect.
/// @details Calling this while a transition is already in flight atomically
///          replaces the pending URL — the previous fade-in will never fire.
///
/// @param deps        Same deps passed to `bindSceneTransition`.
/// @param state       State returned by `bindSceneTransition`.
/// @param url         Target URL (any scheme CEF accepts).
/// @param kind        Visual overlay style.
/// @param durationMs  Total transition duration in ms.  Half is fade-out,
///                    half is fade-in. `<= 0` is treated as `Transition::None`.
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
