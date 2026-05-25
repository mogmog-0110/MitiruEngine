#pragma once

/// @file Live2DAnimator.hpp
/// @brief Live2D model 向けの高レベル animation 制御
/// @details motion / expression / lip sync / eye tracking / auto-blink を
///          手軽に扱う method 群を提供する。

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

/// @brief Live2D model 向けの高レベル animation controller
/// @details Live2DModel を wrap し、簡略化した animation 制御を提供する:
///          motion 再生 / expression / lip sync / eye tracking / auto-blink。
class Live2DAnimator
{
public:
    /// @brief model 用の animator を構築する
    /// @param model Live2DModel への pointer (non-owning)
    explicit Live2DAnimator(Live2DModel* model) noexcept
        : m_model(model)
    {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
    }

    // copy 禁止
    Live2DAnimator(const Live2DAnimator&) = delete;
    Live2DAnimator& operator=(const Live2DAnimator&) = delete;

    /// @brief group と index を指定して motion を再生する
    /// @param group motion group 名 (例: "Idle", "TapBody")
    /// @param index group 内の motion index
    /// @param priority motion priority (default: Normal)
    void PlayMotion(const std::string& group, int index,
                    MotionPriority priority = MotionPriority::Normal)
    {
        if (!m_model) return;
        m_model->StartMotion(group, index, priority);
    }

    /// @brief 指定 group からランダムに motion を再生する
    /// @param group motion group 名
    /// @param priority motion priority (default: Normal)
    void PlayRandomMotion(const std::string& group,
                          MotionPriority priority = MotionPriority::Normal)
    {
        if (!m_model) return;

        const auto count = m_model->GetMotionCount(group);
        if (count <= 0) return;

        const auto index = std::rand() % count;
        m_model->StartMotion(group, index, priority);
    }

    /// @brief 名前で expression を設定する
    /// @param name expression 名 (例: "F01")
    void SetExpression(const std::string& name)
    {
        if (!m_model) return;
        m_model->SetExpression(name);
    }

    /// @brief 利用可能な expression からランダムに設定する
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

    /// @brief lip sync 値を設定する (0.0 - 1.0)
    /// @param value 口の開き具合
    void SetLipSync(float value)
    {
        if (!m_model) return;
        m_model->SetLipSyncEnabled(true);
        m_model->SetLipSyncValue(value);
    }

    /// @brief lip sync を無効化する
    void DisableLipSync()
    {
        if (!m_model) return;
        m_model->SetLipSyncEnabled(false);
        m_model->SetLipSyncValue(0.0f);
    }

    /// @brief eye tracking の注視点を設定する
    /// @param x model 空間の X 座標 (-1.0 〜 1.0)
    /// @param y model 空間の Y 座標 (-1.0 〜 1.0)
    void SetEyeTracking(float x, float y)
    {
        if (!m_model) return;
        m_model->SetDragging(x, y);
    }

    /// @brief auto-blink の有効/無効を切り替える
    /// @param enabled true で auto-blink 有効
    void SetAutoBlinkEnabled(bool enabled) noexcept
    {
        m_autoBlinkEnabled = enabled;
    }

    /// @brief auto-blink が有効か返す
    [[nodiscard]] bool IsAutoBlinkEnabled() const noexcept
    {
        return m_autoBlinkEnabled;
    }

    /// @brief animator を更新する (model Update の前に毎フレーム呼ぶ)
    /// @param deltaTime 前フレームからの経過秒数
    void Update(float deltaTime)
    {
        m_idleTimer += deltaTime;

        // 自動 idle motion
        if (m_autoIdleEnabled && m_idleTimer > m_idleInterval)
        {
            PlayRandomMotion("Idle", MotionPriority::Idle);
            m_idleTimer = 0.0f;
        }
    }

    /// @brief 自動 idle motion の有効/無効を切り替える
    void SetAutoIdleEnabled(bool enabled) noexcept { m_autoIdleEnabled = enabled; }

    /// @brief 自動 idle の間隔を秒で設定する
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
