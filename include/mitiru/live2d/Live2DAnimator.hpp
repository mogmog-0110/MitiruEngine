#pragma once

/// @file Live2DAnimator.hpp
/// @brief High-level animation control for Live2D models
/// @details Provides convenient methods for motions, expressions,
///          lip sync, eye tracking, and auto-blink control.

#ifdef MITIRU_HAS_CUBISM

#include <cstdlib>
#include <ctime>
#include <string>

#include <CubismFramework.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Id/CubismIdManager.hpp>

#include <mitiru/live2d/Live2DModel.hpp>

namespace mitiru::live2d
{

/// @brief High-level animation controller for Live2D models
/// @details Wraps a Live2DModel to provide simplified animation control:
///          motion playback, expressions, lip sync, eye tracking, auto-blink.
class Live2DAnimator
{
public:
    /// @brief Construct animator for a model
    /// @param model Pointer to the Live2DModel (non-owning)
    explicit Live2DAnimator(Live2DModel* model) noexcept
        : m_model(model)
    {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
    }

    // Non-copyable
    Live2DAnimator(const Live2DAnimator&) = delete;
    Live2DAnimator& operator=(const Live2DAnimator&) = delete;

    /// @brief Play a motion by group and index
    /// @param group Motion group name (e.g. "Idle", "TapBody")
    /// @param index Motion index within the group
    /// @param priority Motion priority (default: Normal)
    void PlayMotion(const std::string& group, int index,
                    MotionPriority priority = MotionPriority::Normal)
    {
        if (!m_model) return;
        m_model->StartMotion(group, index, priority);
    }

    /// @brief Play a random motion from the specified group
    /// @param group Motion group name
    /// @param priority Motion priority (default: Normal)
    void PlayRandomMotion(const std::string& group,
                          MotionPriority priority = MotionPriority::Normal)
    {
        if (!m_model) return;

        const auto count = m_model->GetMotionCount(group);
        if (count <= 0) return;

        const auto index = std::rand() % count;
        m_model->StartMotion(group, index, priority);
    }

    /// @brief Set expression by name
    /// @param name Expression name (e.g. "F01")
    void SetExpression(const std::string& name)
    {
        if (!m_model) return;
        m_model->SetExpression(name);
    }

    /// @brief Set a random expression from available expressions
    void SetRandomExpression()
    {
        if (!m_model) return;

        auto* setting = m_model->GetModelSetting();
        if (!setting) return;

        const auto count = setting->GetExpressionCount();
        if (count <= 0) return;

        const auto index = std::rand() % count;
        const auto name = setting->GetExpressionName(index);
        if (name)
        {
            m_model->SetExpression(name);
        }
    }

    /// @brief Set lip sync value (0.0 - 1.0)
    /// @param value Lip openness
    void SetLipSync(float value)
    {
        if (!m_model) return;
        m_model->SetLipSyncEnabled(true);
        m_model->SetLipSyncValue(value);
    }

    /// @brief Disable lip sync
    void DisableLipSync()
    {
        if (!m_model) return;
        m_model->SetLipSyncEnabled(false);
        m_model->SetLipSyncValue(0.0f);
    }

    /// @brief Set eye tracking target position
    /// @param x X coordinate in model space (-1.0 to 1.0)
    /// @param y Y coordinate in model space (-1.0 to 1.0)
    void SetEyeTracking(float x, float y)
    {
        if (!m_model) return;
        m_model->SetDragging(x, y);
    }

    /// @brief Enable or disable auto-blink
    /// @param enabled true to enable auto-blink
    void SetAutoBlinkEnabled(bool enabled) noexcept
    {
        m_autoBlinkEnabled = enabled;
    }

    /// @brief Check if auto-blink is enabled
    [[nodiscard]] bool IsAutoBlinkEnabled() const noexcept
    {
        return m_autoBlinkEnabled;
    }

    /// @brief Update the animator (call each frame before model Update)
    /// @param deltaTime Time elapsed since last frame in seconds
    void Update(float deltaTime)
    {
        m_idleTimer += deltaTime;

        // Auto idle motion
        if (m_autoIdleEnabled && m_idleTimer > m_idleInterval)
        {
            PlayRandomMotion("Idle", MotionPriority::Idle);
            m_idleTimer = 0.0f;
        }
    }

    /// @brief Enable or disable auto idle motion
    void SetAutoIdleEnabled(bool enabled) noexcept { m_autoIdleEnabled = enabled; }

    /// @brief Set auto idle interval in seconds
    void SetAutoIdleInterval(float interval) noexcept { m_idleInterval = interval; }

private:
    Live2DModel* m_model = nullptr;
    bool m_autoBlinkEnabled = true;
    bool m_autoIdleEnabled = true;
    float m_idleTimer = 0.0f;
    float m_idleInterval = 4.0f;
};

} // namespace mitiru::live2d

#endif // MITIRU_HAS_CUBISM
