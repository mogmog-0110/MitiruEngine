#pragma once

/// @file IScene.hpp
/// @brief SceneRouter 向けの scene lifecycle interface
/// @details IScene を継承すると、SceneRouter stack に push / pop できる scene を
///          作れる。各 scene は push / pause / resume / pop される過程で
///          lifecycle callback を受け取る。
///
/// @par 使用例
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

/// @brief SceneRouter が管理する game scene の純粋 interface。
///
/// lifecycle の順序保証:
///  - push:    onEnter  は最初の onUpdate の前に 1 度だけ発火。
///  - pop:     onExit   は最後の onUpdate の後に 1 度だけ発火。
///  - 覆われた時: onPause  は別の scene が上に push された時に発火。
///  - 露出した時: onResume は覆っていた scene が pop された時に発火。
///
/// 全 callback は default で no-op なので、subclass は必要なものだけ override すればよい。
/// onUpdate は pure-virtual — 全 scene は時間を扱う必要がある。
class IScene {
public:
    virtual ~IScene() = default;

    /// scene が push された後、最初の onUpdate の前に 1 度だけ呼ばれる。
    virtual void onEnter() {}

    /// この scene が stack の top である間、毎フレーム呼ばれる。
    /// @param dt  秒単位の frame delta time。
    virtual void onUpdate(float dt) = 0;

    /// scene が pop された後、最後の onUpdate の後に 1 度だけ呼ばれる。
    virtual void onExit() {}

    /// 別の scene がこの scene の上に push された時に呼ばれる。
    /// onResume が呼ばれるまで、この scene は onUpdate を受け取らない。
    virtual void onPause() {}

    /// この scene の上の scene が pop された時に呼ばれる。
    /// 次フレームから onUpdate の受け取りを再開する。
    virtual void onResume() {}
};

} // namespace mitiru::scene
