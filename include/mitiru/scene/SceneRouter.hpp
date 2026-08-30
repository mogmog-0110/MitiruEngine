#pragma once

/// @file SceneRouter.hpp
/// @brief stack ベースの scene flow router
/// @details IScene instance の stack を管理し、lifecycle callback
///          (onEnter / onExit / onPause / onResume) を厳密な順序で dispatch する。
///          onUpdate を受け取るのは stack 最上位の scene のみ。
///
/// @par Push / Pop の挙動
/// @code
/// SceneRouter router;
///
/// // Push title scene。fires TitleScene::onEnter
/// router.push(std::make_unique<TitleScene>());
/// router.update(dt);
///
/// // Push gameplay on top。fires TitleScene::onPause, GameScene::onEnter
/// router.push(std::make_unique<GameScene>());
/// router.update(dt);   // only GameScene receives update
///
/// // Pop gameplay。fires GameScene::onExit, TitleScene::onResume
/// router.pop();
/// router.update(dt);   // TitleScene is top again
/// @endcode
///
/// @par Replace の挙動
/// @code
/// // Replace current top。fires old::onExit, new::onEnter
/// // onPause / onResume are NOT fired on the scene below.
/// router.replace(std::make_unique<CreditsScene>());
/// @endcode

#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

#include "mitiru/scene/IScene.hpp"

namespace mitiru::scene {

/// @brief stack ベースの scene flow controller。
///
/// thread 安全性: thread-safe ではない。game-loop thread からのみ呼ぶこと。
/// hot-path: update()、current()、depth() は heap 確保ゼロ。
class SceneRouter {
public:
    SceneRouter() = default;

    // copy 不可、move 可。
    SceneRouter(const SceneRouter&)            = delete;
    SceneRouter& operator=(const SceneRouter&) = delete;
    SceneRouter(SceneRouter&&)                 = default;
    SceneRouter& operator=(SceneRouter&&)      = default;

    ~SceneRouter() = default;

    // -----------------------------------------------------------------------
    // 変更操作
    // -----------------------------------------------------------------------

    /// 新しい scene を stack に push する。
    /// 発火: 直前の top::onPause (あれば)、new::onEnter。
    /// @param scene  null 不可。
    void push(std::unique_ptr<IScene> scene) {
        assert(scene && "SceneRouter::push — scene must not be null");

        if (!m_stack.empty()) {
            m_stack.back()->onPause();
        }
        m_stack.push_back(std::move(scene));
        m_stack.back()->onEnter();
    }

    /// stack から top の scene を pop する。
    /// 発火: top::onExit、現れた scene::onResume (あれば)。
    /// stack が空なら何もしない。
    void pop() {
        if (m_stack.empty()) {
            return;
        }
        m_stack.back()->onExit();
        m_stack.pop_back();

        if (!m_stack.empty()) {
            m_stack.back()->onResume();
        }
    }

    /// 下の scene に触れず top の scene を置き換える。
    /// 発火: 旧 top::onExit、new::onEnter。
    /// 下の scene に onPause / onResume は発火しない。
    /// @param scene  null 不可。
    void replace(std::unique_ptr<IScene> scene) {
        assert(scene && "SceneRouter::replace — scene must not be null");

        if (!m_stack.empty()) {
            m_stack.back()->onExit();
            m_stack.pop_back();
        }
        m_stack.push_back(std::move(scene));
        m_stack.back()->onEnter();
    }

    // -----------------------------------------------------------------------
    // Per-frame (hot path。確保ゼロ)
    // -----------------------------------------------------------------------

    /// top の scene を tick する。stack が空なら何もしない。
    /// @param dt  秒単位の frame delta time。
    void update(float dt) {
        if (m_stack.empty()) {
            return;
        }
        m_stack.back()->onUpdate(dt);
    }

    // -----------------------------------------------------------------------
    // Query (hot path。確保ゼロ)
    // -----------------------------------------------------------------------

    /// top の scene への raw pointer を返す。空なら nullptr。
    [[nodiscard]] IScene* current() const noexcept {
        if (m_stack.empty()) {
            return nullptr;
        }
        return m_stack.back().get();
    }

    /// 現在 stack 上にある scene の数を返す。
    [[nodiscard]] std::size_t depth() const noexcept {
        return m_stack.size();
    }

    /// stack に scene が無ければ true を返す。
    [[nodiscard]] bool empty() const noexcept {
        return m_stack.empty();
    }

private:
    std::vector<std::unique_ptr<IScene>> m_stack;
};

} // namespace mitiru::scene
