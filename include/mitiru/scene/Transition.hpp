#pragma once

/// @file Transition.hpp
/// @brief renderer 非依存の軽量な screen-transition overlay (G-03)。
///
/// **動機。**
/// `SceneTransitionManager` は scene loading と MitiruScene lifecycle に密結合した
/// 重いシステム。game-level UI ではもっと単純で *standalone* な overlay が必要に
/// なることが多い — 既に描かれているもの (CEF page、2D canvas、native screen)
/// の上に Fade/Dissolve/Slide/Zoom/Custom effect を、scene graph に一切触れず
/// 再生する。`Transition` がその隙間を埋める。
///
/// **設計判断:**
/// - Header-only、GPU 依存ゼロ。class が持つのは timing state のみ。
///   描画は `DrawFn` callback に委譲し、caller が利用可能な primitive
///   (Screen、CEF StateStore、test spy) へ bind する。
/// - `TransitionFrame` は単なる value type — renderer が必要とする全データを
///   1 struct に: kind、progress [0,1]、color、slide 方向、custom result。
/// - `TransitionKind::Custom` は `EasingFn` lambda を受け取り、designer が
///   engine 改変なしに任意の curve (例: `smoothstep`、bounce) を供給できる。
/// - `Color` は `mitiru::render::Color` (`sgc::Colorf` の wrapper) を再利用。
///   default の overlay color は不透明な黒。
/// - delta-time 駆動 — frame-rate に完全非依存。
/// - `start()` は冪等: transition 途中で呼んでも t=0 へきれいに reset される。
///
/// **使い方 (黒へ Fade、0.5 秒):**
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
/// **使い方 (Custom easing — smoothstep):**
/// ```cpp
///   tr.setCustomEasing([](float t) {
///       return t * t * (3.0f - 2.0f * t);  // smoothstep
///   });
///   tr.start(mitiru::scene::TransitionKind::Custom, 1.0f);
/// ```
///
/// @note シーン遷移 overlay。GPU も scene graph も不要。
///       DrawFn callback で描画先を customize できる。

#include <algorithm>
#include <cmath>
#include <functional>

#include <mitiru/render/Style2D.hpp>   // mitiru::render::Color → sgc::Colorf

namespace mitiru::scene
{

// ── Enums ─────────────────────────────────────────────────────────────────

/// @brief 組み込みの transition 種別。
enum class TransitionKind
{
    Fade,      ///< 全画面の color overlay が fade in してから fade out。
    Dissolve,  ///< Fade と同じだが progress curve が smooth-step。
    Slide,     ///< 画面が片端から slide in。overlay が継ぎ目を覆う。
    Zoom,      ///< scale factor が振動: 1→peak→1 (例: zoom-in reveal)。
    Custom,    ///< ユーザ供給の easing lambda (setCustomEasing 参照)。
};

/// @brief TransitionKind::Slide で使う方向。
enum class SlideDirection
{
    Left,   ///< 新コンテンツが左から slide in。
    Right,  ///< 新コンテンツが右から slide in。
    Up,     ///< 新コンテンツが上から slide in。
    Down,   ///< 新コンテンツが下から slide in。
};

// ── TransitionFrame ───────────────────────────────────────────────────────

/// @brief 毎フレーム DrawFn callback へ渡す snapshot。
///
/// renderer から見ると全 field は read-only — 現在の transition state を
/// 記述する。どう適用するかは renderer が決める。
struct TransitionFrame
{
    TransitionKind  kind          = TransitionKind::Fade;
    float           progress      = 0.0f;  ///< [0, 1] に clamp
    sgc::Colorf     color         = {0, 0, 0, 1};  ///< overlay color
    SlideDirection  slideDir      = SlideDirection::Left;
    float           customResult  = 0.0f;  ///< EasingFn(t) が返した値

    // ── Derived helpers ──────────────────────────────────────────────────

    /// @brief 単純な overlay (Fade / Custom / Dissolve) 用の alpha。
    ///
    /// Fade/Dissolve/Custom の場合: transition の前半で 0→1 に上昇し、
    /// 後半で 1→0 に戻る (in/out envelope)。片道の curtain だけ欲しい
    /// caller は `progress` を直接使える。
    [[nodiscard]] float alpha() const noexcept
    {
        // Triangle envelope: [0,0.5] で 0→1、[0.5,1] で 1→0
        return 1.0f - 2.0f * std::abs(progress - 0.5f);
    }

    /// @brief [-1, 1] に正規化された slide offset。
    ///
    /// 正 = slide panel が正方向にまだ画面外。
    /// progress==0 で panel は完全に画面外、progress==1 で反対側へ
    /// 完全に画面外。progress==0.5 で panel は中央 (完全に画面内)。
    [[nodiscard]] float slideOffset() const noexcept
    {
        // 0→-1 のあと -1→0 (triangle、0.5 で負の peak)
        return -(1.0f - 2.0f * std::abs(progress - 0.5f));
    }

    /// @brief Zoom transition の scale factor (開始/終了で 1.0、peak > 1)。
    ///
    /// default の peak は 1.2 (t=0.5 中心の 20% zoom burst)。
    [[nodiscard]] float zoomScale() const noexcept
    {
        constexpr float kPeak = 1.2f;
        return 1.0f + (kPeak - 1.0f) * alpha();
    }
};

// ── Transition ────────────────────────────────────────────────────────────

/// @brief standalone な screen-transition overlay。
///
/// 持つのは timing state のみ。GPU 不要、scene graph 不要。
/// DrawFn を bind すれば任意の rendering backend に接続できる。
class Transition
{
public:
    /// @brief TransitionKind::Custom 用の easing 関数型。
    /// @param t [0, 1] に正規化された時刻。
    /// @return eased された値。通常は [0, 1] (clamp するかは caller の選択)。
    using EasingFn = std::function<float(float t)>;

    /// @brief draw callback 型。
    ///
    /// transition が active な間 `draw()` から呼ばれる。
    /// callback は `TransitionFrame` snapshot を受け取り、renderer が
    /// 自分の backend に合わせて各 field を解釈する。
    using DrawFn = std::function<void(const TransitionFrame&)>;

    Transition() = default;

    // ── Setup ─────────────────────────────────────────────────────────────

    /// @brief draw callback を登録 (active な間 `draw()` から呼ばれる)。
    void setDrawCallback(DrawFn fn) { m_drawFn = std::move(fn); }

    /// @brief TransitionKind::Custom 用の easing lambda を供給。
    ///
    /// lambda は毎 update で t ∈ [0,1] を引数に呼ばれ、戻り値は renderer 向けに
    /// `TransitionFrame::customResult` へ格納される。
    /// default (未設定時): 線形の `t`。
    void setCustomEasing(EasingFn fn) { m_customEasing = std::move(fn); }

    /// @brief slide 方向を設定 (TransitionKind::Slide でのみ意味を持つ)。
    void setSlideDirection(SlideDirection dir) noexcept { m_slideDir = dir; }

    // ── Control ───────────────────────────────────────────────────────────

    /// @brief transition を開始 (または再開始)。
    ///
    /// transition 途中での reset は安全: progress が即座に 0 へ戻る。
    ///
    /// @param kind         再生する transition 種別。
    /// @param durationSec  秒単位の総時間。0 → 即時 (即 isDone)。
    /// @param color        overlay / fill color (default: 不透明な黒)。
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

    /// @brief timing を `dt` 秒進める (delta-time、frame-rate 非依存)。
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

    /// @brief transition が active (まだ done でなく callback が登録済み) なら
    ///        DrawFn callback を発火する。
    ///
    /// 常に最前面に描く — caller は他の全描画の *後* にこれを呼ぶべき。
    void draw() const
    {
        if (!m_drawFn) return;
        m_drawFn(buildFrame());
    }

    // ── Query ─────────────────────────────────────────────────────────────

    /// @brief transition が完了したら true (elapsed >= duration)。
    [[nodiscard]] bool isDone() const noexcept { return m_done; }

    /// @brief [0, 1] に正規化された progress。
    ///
    /// 0 = 開始直後、1 = 完了 (isDone() が true になる瞬間と同じ)。
    [[nodiscard]] float progress() const noexcept
    {
        if (m_duration == 0.0f) return 1.0f;
        return std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
    }

    /// @brief 現在の progress から導かれる triangle-envelope alpha。
    /// @see TransitionFrame::alpha()
    [[nodiscard]] float alpha() const noexcept { return buildFrame().alpha(); }

    /// @brief 現在の kind。
    [[nodiscard]] TransitionKind kind() const noexcept { return m_kind; }

    /// @brief 現在の overlay color。
    [[nodiscard]] sgc::Colorf color() const noexcept { return m_color; }

    /// @brief callback を発火せずに frame snapshot を構築する。
    ///
    /// unit test や CEF bridge の serialise に便利。
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
            // Dissolve は alpha envelope に smooth-step curve を使う。
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
    bool            m_done      = true;   ///< 初期状態が done。最初の start() 前は isDone() == true
    sgc::Colorf     m_color     = {0, 0, 0, 1};
    SlideDirection  m_slideDir  = SlideDirection::Left;
    EasingFn        m_customEasing;
    DrawFn          m_drawFn;
};

} // namespace mitiru::scene
