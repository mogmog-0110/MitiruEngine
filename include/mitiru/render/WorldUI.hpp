#pragma once
/// @file WorldUI.hpp
/// @brief 3D空間内のUI要素（進捗バー・ラベル）

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/core/Screen.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>
#include <string>
#include <vector>

namespace mitiru::render {

/// @brief 3D空間に配置されるUI要素の種別
enum class WorldUIType : uint8_t {
    ProgressBar,
    Label,
    Icon
};

/// @brief 3D空間内のUI要素
struct WorldUIElement {
    WorldUIType type = WorldUIType::Label;
    sgc::Vec3f worldPosition;        ///< 3Dワールド座標
    sgc::Vec2f offset{0, -20};       ///< スクリーン座標上のオフセット(px)

    // ProgressBar
    float progress = 0.0f;           ///< 0.0-1.0
    float barWidth = 60.0f;          ///< バー幅(px)
    float barHeight = 8.0f;          ///< バー高さ(px)
    sgc::Colorf barBgColor{0.3f, 0.3f, 0.3f, 0.7f};
    sgc::Colorf barFillColor{0.2f, 0.9f, 0.3f, 1.0f};

    // Label
    std::string text;
    sgc::Colorf textColor{1, 1, 1, 1};
    float fontSize = 12.0f;

    // Icon
    sgc::Colorf iconColor{1, 1, 1, 1};
    float iconSize = 16.0f;

    bool visible = true;
};

/// @brief 3D空間内UIレンダラー
class WorldUI {
public:
    void add(const WorldUIElement& element) {
        m_elements.push_back(element);
    }

    void clear() { m_elements.clear(); }

    /// @brief 全要素をカメラ射影して2D Screenに描画する
    void render(Screen& screen, const Camera3D& camera,
                float screenWidth, float screenHeight) const {
        for (const auto& elem : m_elements) {
            if (!elem.visible) continue;
            if (!camera.isInFront(elem.worldPosition)) continue;

            const auto screenPos = camera.worldToScreen(
                elem.worldPosition, screenWidth, screenHeight);

            const float sx = screenPos.x + elem.offset.x;
            const float sy = screenPos.y + elem.offset.y;

            switch (elem.type) {
            case WorldUIType::ProgressBar:
                drawProgressBar(screen, sx, sy, elem);
                break;
            case WorldUIType::Label:
                screen.drawText({sx, sy}, elem.text, elem.textColor, elem.fontSize);
                break;
            case WorldUIType::Icon:
                screen.drawCircle({sx, sy}, elem.iconSize * 0.5f, elem.iconColor);
                break;
            }
        }
    }

    [[nodiscard]] std::size_t elementCount() const noexcept { return m_elements.size(); }

private:
    std::vector<WorldUIElement> m_elements;

    static void drawProgressBar(Screen& screen, float x, float y,
                                const WorldUIElement& elem) {
        const float bx = x - elem.barWidth * 0.5f;
        screen.drawRect(sgc::Rectf{bx, y, elem.barWidth, elem.barHeight}, elem.barBgColor);
        if (elem.progress > 0.0f) {
            const float fillW = elem.barWidth * std::clamp(elem.progress, 0.0f, 1.0f);
            screen.drawRect(sgc::Rectf{bx, y, fillW, elem.barHeight}, elem.barFillColor);
        }
    }
};

} // namespace mitiru::render
