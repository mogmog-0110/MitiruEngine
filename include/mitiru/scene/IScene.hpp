#pragma once

/// @file IScene.hpp
/// @brief Scene lifecycle interface for SceneRouter
/// @details Derive from IScene to create a scene that can be pushed onto or
///          popped off a SceneRouter stack. Each scene receives lifecycle
///          callbacks as it is pushed, paused, resumed, and popped.
///
/// @par Usage example
/// @code
/// class TitleScene : public mitiru::scene::IScene {
/// public:
///     void onEnter() override { /* load title assets */ }
///     void onUpdate(float dt) override { /* tick title logic */ }
///     void onExit() override { /* unload title assets */ }
/// };
///
/// SceneRouter router;
/// router.push(std::make_unique<TitleScene>());
/// router.update(0.016f);
/// @endcode

namespace mitiru::scene {

/// @brief Pure interface for a game scene managed by SceneRouter.
///
/// Lifecycle ordering guarantees:
///  - push:    onEnter  fires once, before the first onUpdate call.
///  - pop:     onExit   fires once, after  the last  onUpdate call.
///  - covered: onPause  fires when another scene is pushed on top.
///  - uncovered: onResume fires when the covering scene is popped.
///
/// All callbacks default to no-ops so subclasses only override what they need.
/// onUpdate is pure-virtual — every scene must handle time.
class IScene {
public:
    virtual ~IScene() = default;

    /// Called once after the scene is pushed, before the first onUpdate.
    virtual void onEnter() {}

    /// Called every frame while this scene is the top of the stack.
    /// @param dt  Frame delta time in seconds.
    virtual void onUpdate(float dt) = 0;

    /// Called once after the scene is popped, after the last onUpdate.
    virtual void onExit() {}

    /// Called when another scene is pushed on top of this one.
    /// The scene will not receive onUpdate until onResume is called.
    virtual void onPause() {}

    /// Called when the scene on top of this one is popped.
    /// The scene resumes receiving onUpdate from the next frame.
    virtual void onResume() {}
};

} // namespace mitiru::scene
