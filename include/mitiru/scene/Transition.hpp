#pragma once

/// @file Transition.hpp
/// @brief Lightweight, renderer-agnostic screen-transition overlay (G-03).
///
/// **Motivation.**
/// `SceneTransitionManager` is a heavier system tightly coupled to scene
/// loading and MitiruScene lifecycle. Game-level UI often needs a simpler,
/// *standalone* overlay — play a Fade/Dissolve/Slide/Zoom/Custom effect
/// on top of whatever is already drawn (a CEF page, a 2D canvas, a native
/// screen) without touching any scene graph. `Transition` fills that gap.
///
/// **Design decisions:**
/// - Header-only, zero GPU dependency. The class owns timing state only.
///   Rendering is delegated to a `DrawFn` callback that the caller binds to
///   whatever primitive is available (Screen, CEF StateStore, a test spy).
/// - `TransitionFrame` is a plain value type — all data the renderer needs in
///   one struct: kind, progress [0,1], color, slide direction, custom result.
/// - `TransitionKind::Custom` accepts an `EasingFn` lambda so designers can
///   supply any curve (e.g. `smoothstep`, bounce) without engine changes.
/// - `Color` is reused from `mitiru::render::Color` (wraps `sgc::Colorf`).
///   Default overlay color is opaque black.
/// - Delta-time driven — fully frame-rate independent.
/// - `start()` is idempotent: calling it mid-transition resets to t=0 cleanly.
///
/// **Usage (Fade to black, 0.5 s):**
/// ```cpp
///   #include <mitiru/scene/Transition.hpp>
///   #include <mitiru/render/Style2D.hpp>  // mitiru::render::Color
///
///   mitiru::scene::Transition tr;
///   tr.setDrawCallback([](const mitiru::scene::TransitionFrame& f) {
///       // fill the screen with f.color at opacity f.alpha()
///       screen.fillRect(screenRect, f.color);
///   });
///   tr.start(mitiru::scene::TransitionKind::Fade, 0.5f);
///
///   // per-frame:
///   tr.update(dt);
///   tr.draw();         // fires the callback when active
///   if (tr.isDone()) { /* transition finished */ }
/// ```
///
/// **Usage (Custom easing — smoothstep):**
/// ```cpp
///   tr.setCustomEasing([](float t) {
///       return t * t * (3.0f - 2.0f * t);  // smoothstep
///   });
///   tr.start(mitiru::scene::TransitionKind::Custom, 1.0f);
/// ```
///
/// @note Japanese: シーン遷移オーバーレイ。GPUもシーングラフも不要。
///       DrawFnコールバックで描画先をカスタマイズできる。

#include <algorithm>
#include <cmath>
#include <functional>

#include <mitiru/render/Style2D.hpp>   // mitiru::render::Color → sgc::Colorf

namespace mitiru::scene
{

// ── Enums ─────────────────────────────────────────────────────────────────

/// @brief Built-in transition kinds.
enum class TransitionKind
{
    Fade,      ///< Full-screen color overlay fades in then out.
    Dissolve,  ///< Same as Fade but progress curve is a smooth-step.
    Slide,     ///< Screen slides in from one edge; overlay covers the seam.
    Zoom,      ///< Scale factor oscillates: 1→peak→1 (e.g. zoom-in reveal).
    Custom,    ///< User-supplied easing lambda (see setCustomEasing).
};

/// @brief Direction used by TransitionKind::Slide.
enum class SlideDirection
{
    Left,   ///< New content slides in from the left.
    Right,  ///< New content slides in from the right.
    Up,     ///< New content slides in from above.
    Down,   ///< New content slides in from below.
};

// ── TransitionFrame ───────────────────────────────────────────────────────

/// @brief Snapshot passed to the DrawFn callback each frame.
///
/// All fields are read-only from the renderer's perspective — they describe
/// the current transition state.  The renderer decides how to apply them.
struct TransitionFrame
{
    TransitionKind  kind          = TransitionKind::Fade;
    float           progress      = 0.0f;  ///< [0, 1], clamped
    sgc::Colorf     color         = {0, 0, 0, 1};  ///< overlay color
    SlideDirection  slideDir      = SlideDirection::Left;
    float           customResult  = 0.0f;  ///< value returned by EasingFn(t)

    // ── Derived helpers ──────────────────────────────────────────────────

    /// @brief Alpha for a simple overlay (Fade / Custom / Dissolve).
    ///
    /// For Fade/Dissolve/Custom: rises from 0→1 for the first half of the
    /// transition, then falls back 1→0 (in/out envelope). Callers that want
    /// only a one-way curtain can use `progress` directly.
    [[nodiscard]] float alpha() const noexcept
    {
        // Triangle envelope: 0→1 on [0,0.5], 1→0 on [0.5,1]
        return 1.0f - 2.0f * std::abs(progress - 0.5f);
    }

    /// @brief Normalized slide offset in [-1, 1].
    ///
    /// Positive = slide panel is still off-screen in the positive direction.
    /// At progress==0 the panel is fully off-screen; at progress==1 it is
    /// fully off-screen on the other side.  At progress==0.5 the panel is
    /// centred (fully on-screen).
    [[nodiscard]] float slideOffset() const noexcept
    {
        // 0→-1 then -1→0 (triangle, negative peak at 0.5)
        return -(1.0f - 2.0f * std::abs(progress - 0.5f));
    }

    /// @brief Scale factor for Zoom transitions (1.0 at start/end, peak > 1).
    ///
    /// Default peak is 1.2 (20% zoom burst centred at t=0.5).
    [[nodiscard]] float zoomScale() const noexcept
    {
        constexpr float kPeak = 1.2f;
        return 1.0f + (kPeak - 1.0f) * alpha();
    }
};

// ── Transition ────────────────────────────────────────────────────────────

/// @brief Standalone screen-transition overlay.
///
/// Owns timing state only.  GPU-free, scene-graph-free.
/// Bind a DrawFn to connect it to any rendering backend.
class Transition
{
public:
    /// @brief Easing function type for TransitionKind::Custom.
    /// @param t Normalised time in [0, 1].
    /// @return Eased value, typically in [0, 1] (clamping is the caller's choice).
    using EasingFn = std::function<float(float t)>;

    /// @brief Draw callback type.
    ///
    /// Called by `draw()` while the transition is active.
    /// The callback receives a `TransitionFrame` snapshot; the renderer
    /// interprets the fields as appropriate for its backend.
    using DrawFn = std::function<void(const TransitionFrame&)>;

    Transition() = default;

    // ── Setup ─────────────────────────────────────────────────────────────

    /// @brief Register the draw callback (called from `draw()` while active).
    void setDrawCallback(DrawFn fn) { m_drawFn = std::move(fn); }

    /// @brief Supply an easing lambda for TransitionKind::Custom.
    ///
    /// The lambda is called with t ∈ [0,1] each update; its return value is
    /// stored in `TransitionFrame::customResult` for the renderer.
    /// Default (if not set): linear `t`.
    void setCustomEasing(EasingFn fn) { m_customEasing = std::move(fn); }

    /// @brief Set the slide direction (only meaningful for TransitionKind::Slide).
    void setSlideDirection(SlideDirection dir) noexcept { m_slideDir = dir; }

    // ── Control ───────────────────────────────────────────────────────────

    /// @brief Start (or restart) the transition.
    ///
    /// Resetting mid-transition is safe: progress resets to 0 immediately.
    ///
    /// @param kind         Which transition kind to play.
    /// @param durationSec  Total duration in seconds. 0 → instant (isDone immediately).
    /// @param color        Overlay / fill color (default: opaque black).
    void start(TransitionKind kind,
               float          durationSec,
               sgc::Colorf    color = sgc::Colorf{0, 0, 0, 1})
    {
        m_kind       = kind;
        m_duration   = durationSec < 0.0f ? 0.0f : durationSec;
        m_elapsed    = 0.0f;
        m_color      = color;
        m_done       = (m_duration == 0.0f);
    }

    /// @brief Advance timing by `dt` seconds (delta-time, frame-rate independent).
    void update(float dt)
    {
        if (m_done) return;
        if (dt < 0.0f) dt = 0.0f;

        m_elapsed += dt;
        if (m_elapsed >= m_duration)
        {
            m_elapsed = m_duration;
            m_done    = true;
        }
    }

    /// @brief Fire the DrawFn callback if the transition is active (not yet done
    ///        and a callback has been registered).
    ///
    /// Always calls top-most — callers should invoke this *after* all other draws.
    void draw() const
    {
        if (!m_drawFn) return;
        m_drawFn(buildFrame());
    }

    // ── Query ─────────────────────────────────────────────────────────────

    /// @brief True once the transition has completed (elapsed >= duration).
    [[nodiscard]] bool isDone() const noexcept { return m_done; }

    /// @brief Normalised progress in [0, 1].
    ///
    /// 0 = just started, 1 = finished (same moment isDone() becomes true).
    [[nodiscard]] float progress() const noexcept
    {
        if (m_duration == 0.0f) return 1.0f;
        return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
    }

    /// @brief Triangle-envelope alpha derived from current progress.
    /// @see TransitionFrame::alpha()
    [[nodiscard]] float alpha() const noexcept { return buildFrame().alpha(); }

    /// @brief Current kind.
    [[nodiscard]] TransitionKind kind() const noexcept { return m_kind; }

    /// @brief Current overlay color.
    [[nodiscard]] sgc::Colorf color() const noexcept { return m_color; }

    /// @brief Build a frame snapshot without firing the callback.
    ///
    /// Useful for unit tests and CEF bridge serialisation.
    [[nodiscard]] TransitionFrame buildFrame() const
    {
        const float t = progress();

        TransitionFrame frame;
        frame.kind     = m_kind;
        frame.progress = t;
        frame.color    = m_color;
        frame.slideDir = m_slideDir;

        if (m_kind == TransitionKind::Custom)
        {
            frame.customResult = m_customEasing ? m_customEasing(t) : t;
        }
        else if (m_kind == TransitionKind::Dissolve)
        {
            // Dissolve uses a smooth-step curve on the alpha envelope.
            const float a = frame.alpha();
            frame.customResult = a * a * (3.0f - 2.0f * a);  // smoothstep
        }
        else
        {
            frame.customResult = t;
        }

        return frame;
    }

private:
    TransitionKind  m_kind      = TransitionKind::Fade;
    float           m_duration  = 0.0f;
    float           m_elapsed   = 0.0f;
    bool            m_done      = true;   ///< starts done so isDone() == true before first start()
    sgc::Colorf     m_color     = {0, 0, 0, 1};
    SlideDirection  m_slideDir  = SlideDirection::Left;
    EasingFn        m_customEasing;
    DrawFn          m_drawFn;
};

} // namespace mitiru::scene
