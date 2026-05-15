#pragma once

/// @file SceneRouter.hpp
/// @brief Stack-based scene flow router
/// @details Manages a stack of IScene instances and dispatches lifecycle
///          callbacks (onEnter / onExit / onPause / onResume) in strict order.
///          Only the top-of-stack scene receives onUpdate.
///
/// @par Push / Pop semantics
/// @code
/// SceneRouter router;
///
/// // Push title scene — fires TitleScene::onEnter
/// router.push(std::make_unique<TitleScene>());
/// router.update(dt);
///
/// // Push gameplay on top — fires TitleScene::onPause, GameScene::onEnter
/// router.push(std::make_unique<GameScene>());
/// router.update(dt);   // only GameScene receives update
///
/// // Pop gameplay — fires GameScene::onExit, TitleScene::onResume
/// router.pop();
/// router.update(dt);   // TitleScene is top again
/// @endcode
///
/// @par Replace semantics
/// @code
/// // Replace current top — fires old::onExit, new::onEnter
/// // onPause / onResume are NOT fired on the scene below.
/// router.replace(std::make_unique<CreditsScene>());
/// @endcode

#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

#include "mitiru/scene/IScene.hpp"

namespace mitiru::scene {

/// @brief Stack-based scene flow controller.
///
/// Thread safety: NOT thread-safe. Call from the game-loop thread only.
/// Hot-path: update(), current(), depth() perform zero heap allocations.
class SceneRouter {
public:
    SceneRouter() = default;

    // Non-copyable; movable.
    SceneRouter(const SceneRouter&)            = delete;
    SceneRouter& operator=(const SceneRouter&) = delete;
    SceneRouter(SceneRouter&&)                 = default;
    SceneRouter& operator=(SceneRouter&&)      = default;

    ~SceneRouter() = default;

    // -----------------------------------------------------------------------
    // Mutation
    // -----------------------------------------------------------------------

    /// Push a new scene onto the stack.
    /// Fires: top-before::onPause (if any), new::onEnter.
    /// @param scene  Must not be null.
    void push(std::unique_ptr<IScene> scene) {
        assert(scene && "SceneRouter::push — scene must not be null");

        if (!m_stack.empty()) {
            m_stack.back()->onPause();
        }
        m_stack.push_back(std::move(scene));
        m_stack.back()->onEnter();
    }

    /// Pop the top scene off the stack.
    /// Fires: top::onExit, revealed::onResume (if any).
    /// No-op if the stack is empty.
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

    /// Replace the top scene without touching the scene below.
    /// Fires: old-top::onExit, new::onEnter.
    /// onPause / onResume are NOT fired on the scene below.
    /// @param scene  Must not be null.
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
    // Per-frame (hot path — zero allocation)
    // -----------------------------------------------------------------------

    /// Tick the top scene. No-op if the stack is empty.
    /// @param dt  Frame delta time in seconds.
    void update(float dt) {
        if (m_stack.empty()) {
            return;
        }
        m_stack.back()->onUpdate(dt);
    }

    // -----------------------------------------------------------------------
    // Query (hot path — zero allocation)
    // -----------------------------------------------------------------------

    /// Return a raw pointer to the top scene, or nullptr if empty.
    [[nodiscard]] IScene* current() const noexcept {
        if (m_stack.empty()) {
            return nullptr;
        }
        return m_stack.back().get();
    }

    /// Return the number of scenes currently on the stack.
    [[nodiscard]] std::size_t depth() const noexcept {
        return m_stack.size();
    }

    /// Return true when the stack has no scenes.
    [[nodiscard]] bool empty() const noexcept {
        return m_stack.empty();
    }

private:
    std::vector<std::unique_ptr<IScene>> m_stack;
};

} // namespace mitiru::scene
