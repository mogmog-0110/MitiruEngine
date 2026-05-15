#pragma once

/**
 * @file UITheme.hpp
 * @brief Runtime theme system for UI rendering.
 *
 * Provides colour palettes, metric presets, and per-role style overrides
 * that can be swapped at runtime without rebuilding the UI tree.
 */

#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>

#include <map>
#include <string>

namespace mitiru::ui {

/**
 * @struct ThemeColors
 * @brief Semantic colour palette used by all UI elements.
 */
struct ThemeColors {
    sgc::Colorf background{0.1f, 0.1f, 0.1f, 0.8f};   ///< Default panel background.
    sgc::Colorf foreground{1.0f, 1.0f, 1.0f, 1.0f};    ///< Primary text / icon colour.
    sgc::Colorf accent{0.0f, 0.8f, 1.0f, 1.0f};        ///< Interactive highlight.
    sgc::Colorf danger{1.0f, 0.2f, 0.2f, 1.0f};        ///< Error / critical state.
    sgc::Colorf success{0.2f, 1.0f, 0.2f, 1.0f};       ///< Positive / healthy state.
    sgc::Colorf warning{1.0f, 0.8f, 0.0f, 1.0f};       ///< Cautionary state.
    sgc::Colorf disabled{0.5f, 0.5f, 0.5f, 0.5f};      ///< Greyed-out / inactive.
    sgc::Colorf border{0.4f, 0.4f, 0.4f, 1.0f};        ///< Default border stroke.
};

/**
 * @struct ThemeMetrics
 * @brief Scalar sizing values shared across UI elements.
 */
struct ThemeMetrics {
    float fontSize      = 16.0f;  ///< Body text size.
    float titleFontSize = 24.0f;  ///< Title / heading text size.
    float padding       = 8.0f;   ///< Inner padding.
    float margin        = 4.0f;   ///< Outer margin.
    float borderWidth   = 1.0f;   ///< Border stroke width.
    float barHeight     = 20.0f;  ///< Height of progress / health bars.
    float buttonHeight  = 32.0f;  ///< Default button height.
    float cornerRadius  = 4.0f;   ///< Cosmetic corner radius (future use).
};

/**
 * @struct UIThemeStyle
 * @brief Resolved visual style for a single UI element.
 *
 * Renderers consume this struct directly; it collapses theme colours
 * and metrics into the handful of values needed to draw one element.
 */
struct UIThemeStyle {
    sgc::Colorf background;  ///< Fill colour.
    sgc::Colorf foreground;  ///< Text / glyph colour.
    sgc::Colorf border;      ///< Border colour.
    float fontSize;          ///< Text size.
    float padding;           ///< Inner padding.
};

/**
 * @class UITheme
 * @brief Manages colours, metrics, and per-role style overrides.
 *
 * The theme can be hot-swapped at runtime and provides four built-in
 * presets: dark, light, cyberpunk, and retro.
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
    /** @brief Default-construct with dark theme colours. */
    UITheme() = default;

    /**
     * @brief Construct with explicit colours and optional metrics.
     * @param colors   Semantic colour palette.
     * @param metrics  Sizing values (defaults used if omitted).
     */
    explicit UITheme(ThemeColors colors, ThemeMetrics metrics = {})
        : m_colors(colors), m_metrics(metrics) {}

    /** @brief Access the current colour palette. */
    const ThemeColors& colors() const noexcept { return m_colors; }

    /** @brief Access the current metrics. */
    const ThemeMetrics& metrics() const noexcept { return m_metrics; }

    /** @brief Replace the colour palette. */
    void setColors(const ThemeColors& colors) { m_colors = colors; }

    /** @brief Replace the metrics. */
    void setMetrics(const ThemeMetrics& metrics) { m_metrics = metrics; }

    // -----------------------------------------------------------------
    // Per-role styling
    // -----------------------------------------------------------------

    /**
     * @brief Resolve the visual style for a given UI role.
     *
     * If a per-role override has been registered via @c setStyleFor, that
     * override is returned. Otherwise a sensible default is computed from
     * the base colour palette and metrics:
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
     * @param role  The UI role to resolve.
     * @return Fully populated UIThemeStyle.
     */
    UIThemeStyle styleFor(UIRole role) const {
        // Check for explicit override first.
        {
            auto it = m_roleOverrides.find(role);
            if (it != m_roleOverrides.end()) {
                return it->second;
            }
        }

        // Compute from base palette.
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
     * @brief Register a per-role style override.
     *
     * @param role   The role to override.
     * @param style  The style that @c styleFor will return for @p role.
     */
    void setStyleFor(UIRole role, const UIThemeStyle& style) {
        m_roleOverrides[role] = style;
    }

    // -----------------------------------------------------------------
    // Preset themes
    // -----------------------------------------------------------------

    /**
     * @brief Dark theme (default).
     *
     * Dark backgrounds, white text, cyan accent.
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
     * @brief Light theme.
     *
     * Near-white backgrounds, dark text, blue accent.
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
     * @brief Cyberpunk theme.
     *
     * Deep purple/blue background, neon magenta accent, electric cyan text.
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
     * @brief Retro / pixel-art theme.
     *
     * Warm sepia tones, amber accent, classic game feel.
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
        m.cornerRadius = 0.0f;  // Sharp corners for pixel-art aesthetic.
        return UITheme(c, m);
    }

    // -----------------------------------------------------------------
    // Serialisation
    // -----------------------------------------------------------------

    /**
     * @brief Serialise the theme's colour palette to a JSON string.
     *
     * Useful for saving user-customised themes to disk.
     *
     * @return JSON object string with all ThemeColors fields.
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
     * @brief Format a single colour as a JSON field with an RGBA array value.
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
