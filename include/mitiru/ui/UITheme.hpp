#pragma once

/**
 * @file UITheme.hpp
 * @brief UI 描画用の runtime theme system。
 *
 * color palette、metric preset、role 別の style override を提供し、
 * UI tree を作り直さずに runtime で差し替えられる。
 */

#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>

#include <map>
#include <string>

namespace mitiru::ui {

/**
 * @struct ThemeColors
 * @brief 全 UI 要素が使う意味付き color palette。
 */
struct ThemeColors {
    sgc::Colorf background{0.1f, 0.1f, 0.1f, 0.8f};   ///< 既定の panel 背景。
    sgc::Colorf foreground{1.0f, 1.0f, 1.0f, 1.0f};    ///< 主要なテキスト / アイコン色。
    sgc::Colorf accent{0.0f, 0.8f, 1.0f, 1.0f};        ///< 操作系の highlight。
    sgc::Colorf danger{1.0f, 0.2f, 0.2f, 1.0f};        ///< エラー / 致命的状態。
    sgc::Colorf success{0.2f, 1.0f, 0.2f, 1.0f};       ///< 肯定的 / 健全な状態。
    sgc::Colorf warning{1.0f, 0.8f, 0.0f, 1.0f};       ///< 注意状態。
    sgc::Colorf disabled{0.5f, 0.5f, 0.5f, 0.5f};      ///< グレーアウト / 非活性。
    sgc::Colorf border{0.4f, 0.4f, 0.4f, 1.0f};        ///< 既定の border stroke。
};

/**
 * @struct ThemeMetrics
 * @brief UI 要素間で共有する scalar の寸法値。
 */
struct ThemeMetrics {
    float fontSize      = 16.0f;  ///< 本文テキストのサイズ。
    float titleFontSize = 24.0f;  ///< タイトル / 見出しテキストのサイズ。
    float padding       = 8.0f;   ///< 内側 padding。
    float margin        = 4.0f;   ///< 外側 margin。
    float borderWidth   = 1.0f;   ///< border stroke の幅。
    float barHeight     = 20.0f;  ///< progress / health bar の高さ。
    float buttonHeight  = 32.0f;  ///< 既定の button 高さ。
    float cornerRadius  = 4.0f;   ///< 装飾用の角丸半径 (将来用)。
};

/**
 * @struct UIThemeStyle
 * @brief 単一 UI 要素について解決済みの visual style。
 *
 * renderer はこの struct を直接消費する。theme の color と metrics を、
 * 1 要素を描くのに必要な少数の値へ畳み込んだもの。
 */
struct UIThemeStyle {
    sgc::Colorf background;  ///< 塗り色。
    sgc::Colorf foreground;  ///< テキスト / glyph の色。
    sgc::Colorf border;      ///< border の色。
    float fontSize;          ///< テキストサイズ。
    float padding;           ///< 内側 padding。
};

/**
 * @class UITheme
 * @brief color、metrics、role 別 style override を管理する。
 *
 * theme は runtime で hot-swap でき、組み込み preset を 4 つ提供する:
 * dark、light、cyberpunk、retro。
 *
 * @code
 *   UITheme theme = UITheme::cyberpunk();
 *   auto style = theme.styleFor(UIRole::Button);
 *   // use style.background, style.foreground, ... when rendering
 * @endcode
 */
class UITheme {
    ThemeColors m_colors;
    ThemeMetrics m_metrics;
    std::map<UIRole, UIThemeStyle> m_roleOverrides;

public:
    /** @brief dark theme の色で default 構築する。 */
    UITheme() = default;

    /**
     * @brief 明示的な色と任意の metrics で構築する。
     * @param colors   意味付き color palette。
     * @param metrics  寸法値 (省略時は default を使う)。
     */
    explicit UITheme(ThemeColors colors, ThemeMetrics metrics = {})
        : m_colors(colors), m_metrics(metrics) {}

    /** @brief 現在の color palette にアクセスする。 */
    const ThemeColors& colors() const noexcept { return m_colors; }

    /** @brief 現在の metrics にアクセスする。 */
    const ThemeMetrics& metrics() const noexcept { return m_metrics; }

    /** @brief color palette を差し替える。 */
    void setColors(const ThemeColors& colors) { m_colors = colors; }

    /** @brief metrics を差し替える。 */
    void setMetrics(const ThemeMetrics& metrics) { m_metrics = metrics; }

    // -----------------------------------------------------------------
    // role 別の styling
    // -----------------------------------------------------------------

    /**
     * @brief 指定 UI role の visual style を解決する。
     *
     * @c setStyleFor で role 別 override が登録済みならそれを返す。
     * 無ければ base の color palette と metrics から妥当な default を計算する:
     *
     * | Role        | Background         | Foreground    |
     * |-------------|--------------------|---------------|
     * | Panel       | background         | foreground    |
     * | Label       | transparent        | foreground    |
     * | Button      | accent             | white         |
     * | HealthBar   | background (dark)  | accent        |
     * | ProgressBar | background (dark)  | accent        |
     * | Image       | transparent        | foreground    |
     * | Other       | background         | foreground    |
     *
     * @param role  解決する UI role。
     * @return 完全に埋まった UIThemeStyle。
     */
    UIThemeStyle styleFor(UIRole role) const {
        // まず明示的な override を確認する。
        {
            auto it = m_roleOverrides.find(role);
            if (it != m_roleOverrides.end()) {
                return it->second;
            }
        }

        // base palette から計算する。
        UIThemeStyle style;
        style.fontSize = m_metrics.fontSize;
        style.padding  = m_metrics.padding;
        style.border   = m_colors.border;

        switch (role) {
        case UIRole::Panel:
            style.background = m_colors.background;
            style.foreground = m_colors.foreground;
            break;

        case UIRole::Label:
            style.background = sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
            style.foreground = m_colors.foreground;
            break;

        case UIRole::Button:
            style.background = m_colors.accent;
            style.foreground = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f};
            break;

        case UIRole::HealthBar:
            style.background = sgc::Colorf{
                m_colors.background.r * 0.5f,
                m_colors.background.g * 0.5f,
                m_colors.background.b * 0.5f,
                m_colors.background.a};
            style.foreground = m_colors.accent;
            break;

        case UIRole::ProgressBar:
            style.background = sgc::Colorf{
                m_colors.background.r * 0.5f,
                m_colors.background.g * 0.5f,
                m_colors.background.b * 0.5f,
                m_colors.background.a};
            style.foreground = m_colors.accent;
            break;

        case UIRole::Image:
            style.background = sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
            style.foreground = m_colors.foreground;
            break;

        case UIRole::Custom:
            style.background = m_colors.background;
            style.foreground = m_colors.foreground;
            break;

        default:
            style.background = m_colors.background;
            style.foreground = m_colors.foreground;
            break;
        }

        return style;
    }

    /**
     * @brief role 別の style override を登録する。
     *
     * @param role   override する role。
     * @param style  @c styleFor が @p role に対して返す style。
     */
    void setStyleFor(UIRole role, const UIThemeStyle& style) {
        m_roleOverrides[role] = style;
    }

    // -----------------------------------------------------------------
    // Preset theme
    // -----------------------------------------------------------------

    /**
     * @brief dark theme (default)。
     *
     * 暗い背景、白テキスト、cyan の accent。
     */
    static UITheme dark() {
        ThemeColors c;
        c.background = sgc::Colorf{0.1f,  0.1f,  0.1f,  0.85f};
        c.foreground = sgc::Colorf{1.0f,  1.0f,  1.0f,  1.0f};
        c.accent     = sgc::Colorf{0.0f,  0.8f,  1.0f,  1.0f};
        c.danger     = sgc::Colorf{1.0f,  0.2f,  0.2f,  1.0f};
        c.success    = sgc::Colorf{0.2f,  1.0f,  0.2f,  1.0f};
        c.warning    = sgc::Colorf{1.0f,  0.8f,  0.0f,  1.0f};
        c.disabled   = sgc::Colorf{0.5f,  0.5f,  0.5f,  0.5f};
        c.border     = sgc::Colorf{0.4f,  0.4f,  0.4f,  1.0f};
        return UITheme(c);
    }

    /**
     * @brief light theme。
     *
     * ほぼ白の背景、暗いテキスト、blue の accent。
     */
    static UITheme light() {
        ThemeColors c;
        c.background = sgc::Colorf{0.95f, 0.95f, 0.95f, 0.9f};
        c.foreground = sgc::Colorf{0.1f,  0.1f,  0.1f,  1.0f};
        c.accent     = sgc::Colorf{0.2f,  0.4f,  0.9f,  1.0f};
        c.danger     = sgc::Colorf{0.9f,  0.1f,  0.1f,  1.0f};
        c.success    = sgc::Colorf{0.1f,  0.7f,  0.1f,  1.0f};
        c.warning    = sgc::Colorf{0.9f,  0.7f,  0.0f,  1.0f};
        c.disabled   = sgc::Colorf{0.7f,  0.7f,  0.7f,  0.6f};
        c.border     = sgc::Colorf{0.75f, 0.75f, 0.75f, 1.0f};
        return UITheme(c);
    }

    /**
     * @brief cyberpunk theme。
     *
     * 濃い紫 / 青の背景、neon magenta の accent、electric cyan のテキスト。
     */
    static UITheme cyberpunk() {
        ThemeColors c;
        c.background = sgc::Colorf{0.05f, 0.02f, 0.15f, 0.9f};
        c.foreground = sgc::Colorf{0.0f,  1.0f,  0.9f,  1.0f};
        c.accent     = sgc::Colorf{1.0f,  0.0f,  0.8f,  1.0f};
        c.danger     = sgc::Colorf{1.0f,  0.0f,  0.3f,  1.0f};
        c.success    = sgc::Colorf{0.0f,  1.0f,  0.4f,  1.0f};
        c.warning    = sgc::Colorf{1.0f,  0.9f,  0.0f,  1.0f};
        c.disabled   = sgc::Colorf{0.3f,  0.2f,  0.4f,  0.5f};
        c.border     = sgc::Colorf{0.6f,  0.0f,  0.8f,  0.8f};
        return UITheme(c);
    }

    /**
     * @brief retro / pixel-art theme。
     *
     * 暖かいセピア調、amber の accent、古典的なゲームの雰囲気。
     */
    static UITheme retro() {
        ThemeColors c;
        c.background = sgc::Colorf{0.15f, 0.12f, 0.08f, 0.9f};
        c.foreground = sgc::Colorf{0.9f,  0.85f, 0.7f,  1.0f};
        c.accent     = sgc::Colorf{1.0f,  0.7f,  0.0f,  1.0f};
        c.danger     = sgc::Colorf{0.8f,  0.2f,  0.1f,  1.0f};
        c.success    = sgc::Colorf{0.3f,  0.7f,  0.2f,  1.0f};
        c.warning    = sgc::Colorf{0.9f,  0.6f,  0.1f,  1.0f};
        c.disabled   = sgc::Colorf{0.4f,  0.35f, 0.3f,  0.5f};
        c.border     = sgc::Colorf{0.6f,  0.5f,  0.3f,  1.0f};

        ThemeMetrics m;
        m.cornerRadius = 0.0f;  // pixel-art の見た目に合わせて角を尖らせる。
        return UITheme(c, m);
    }

    // -----------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------

    /**
     * @brief theme の color palette を JSON 文字列に serialize する。
     *
     * ユーザーがカスタムした theme をディスクへ保存するのに便利。
     *
     * @return ThemeColors 全フィールドを含む JSON object 文字列。
     */
    std::string toJson() const {
        std::string json = "{\n";
        json += colorToJsonField("background", m_colors.background) + ",\n";
        json += colorToJsonField("foreground", m_colors.foreground) + ",\n";
        json += colorToJsonField("accent",     m_colors.accent)     + ",\n";
        json += colorToJsonField("danger",     m_colors.danger)     + ",\n";
        json += colorToJsonField("success",    m_colors.success)    + ",\n";
        json += colorToJsonField("warning",    m_colors.warning)    + ",\n";
        json += colorToJsonField("disabled",   m_colors.disabled)   + ",\n";
        json += colorToJsonField("border",     m_colors.border)     + "\n";
        json += "}";
        return json;
    }

private:
    /**
     * @brief 単一の色を RGBA 配列値を持つ JSON field として整形する。
     */
    static std::string colorToJsonField(const std::string& name,
                                         const sgc::Colorf& c) {
        return "  \"" + name + "\": ["
             + std::to_string(c.r) + ", "
             + std::to_string(c.g) + ", "
             + std::to_string(c.b) + ", "
             + std::to_string(c.a) + "]";
    }
};

} // namespace mitiru::ui
