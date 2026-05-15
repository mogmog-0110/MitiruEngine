/// @file cpp_gameplay_minimal/main.cpp
/// @brief Headless mini-game demonstrating the P0 C++ gameplay APIs.
///
/// Covered APIs (all five):
///   - SceneRouter + IScene   — TitleScene / CookingScene stack management
///   - StateMachine<enum>     — Idle → Cooking → Done within CookingScene
///   - Timer                  — 2-second bake countdown
///   - Cooldown               — 1-second order-arrival gate
///   - Sequence               — TitleScene intro timeline
///   - BridgeActionRouter     — "ui.button.start" dispatched to trigger transition
///   - BridgeViewPush         — cooking state changes pushed to a printf stub sink
///
/// Build:
///   cmake --preset default
///   cmake --build build --config Debug --target mitiru_cpp_gameplay_minimal
#include <cstdio>
#include <memory>
#include <string_view>

#include "mitiru/scene/IScene.hpp"
#include "mitiru/scene/SceneRouter.hpp"
#include "mitiru/fsm/StateMachine.hpp"
#include "mitiru/time/Timer.hpp"
#include "mitiru/time/Cooldown.hpp"
#include "mitiru/time/Sequence.hpp"
#include "mitiru/input/BridgeActionRouter.hpp"
#include "mitiru/bridge/BridgeViewPush.hpp"

// ---------------------------------------------------------------------------
// Shared singletons (injected by main, owned by main)
// ---------------------------------------------------------------------------

using Router     = mitiru::scene::SceneRouter;
using ActionRtr  = mitiru::input::BridgeActionRouter;
using ViewPush   = mitiru::bridge::BridgeViewPush;

// ---------------------------------------------------------------------------
// CookingScene
// ---------------------------------------------------------------------------

enum class CookState { Idle, Cooking, Done };

class CookingScene final : public mitiru::scene::IScene {
public:
    explicit CookingScene(Router& router, ActionRtr& actions, ViewPush& view)
        : m_router(router)
        , m_actions(actions)
        , m_view(view)
        , m_fsm(CookState::Idle)              // StateMachine constructed here
        , m_bakeTimer(2.0f)                   // Timer: 2-second bake window
        , m_orderCooldown(1.0f)               // Cooldown: 1s between orders
    {
        // Guard: Done is terminal — no transitions out of Done
        m_fsm.setGuard([](CookState from, CookState /*to*/) {
            return from != CookState::Done;
        });

        // Push cooking state to view layer on every transition (StateMachine)
        m_fsm.setOnTransition([this](CookState /*from*/, CookState to) {
            const char* label = stateLabel(to);
            std::printf("[CookingScene] state -> %s\n", label);
            // BridgeViewPush: notify view layer
            m_view.set("state", label);
        });
    }

    void onEnter() override {
        std::printf("[CookingScene] enter, state=Idle\n");
        m_view.set("state", stateLabel(m_fsm.state()));
    }

    void onUpdate(float dt) override {
        // Cooldown tick: wait 1s before first order arrives
        m_orderCooldown.tick(dt);

        if (m_fsm.state() == CookState::Idle && m_orderCooldown.ready()) {
            m_orderCooldown.trigger();
            std::printf("[CookingScene] order arrived\n");
            m_fsm.transition(CookState::Cooking);  // StateMachine transition
            m_bakeTimer.reset();
        }

        if (m_fsm.state() == CookState::Cooking) {
            m_bakeTimer.tick(dt);                  // Timer tick
            if (!m_bakeTimer.expired()) {
                std::printf("[Timer] %.1fs remaining...\n",
                            m_bakeTimer.remaining());
            } else {
                m_fsm.transition(CookState::Done); // StateMachine transition
            }
        }

        if (m_fsm.state() == CookState::Done) {
            std::printf("[CookingScene] done! popping scene\n");
            m_router.pop();                        // SceneRouter::pop
        }
    }

    void onExit() override {
        std::printf("[CookingScene] exit\n");
    }

private:
    static const char* stateLabel(CookState s) noexcept {
        switch (s) {
            case CookState::Idle:    return "Idle";
            case CookState::Cooking: return "Cooking";
            case CookState::Done:    return "Done";
        }
        return "Unknown";
    }

    Router&                          m_router;
    ActionRtr&                       m_actions;
    ViewPush&                        m_view;
    mitiru::fsm::StateMachine<CookState> m_fsm;   // StateMachine
    mitiru::time::Timer                  m_bakeTimer;
    mitiru::time::Cooldown               m_orderCooldown;
};

// ---------------------------------------------------------------------------
// TitleScene
// ---------------------------------------------------------------------------

class TitleScene final : public mitiru::scene::IScene {
public:
    explicit TitleScene(Router& router, ActionRtr& actions, ViewPush& view)
        : m_router(router)
        , m_actions(actions)
        , m_view(view)
    {
        // Sequence: intro timeline — wait 0.5s, print welcome, wait 0.3s, start
        m_intro
            .wait(0.5f)
            .action([] { std::printf("[Title] Welcome to KaeruCrape demo!\n"); })
            .wait(0.3f)
            .action([this] {
                std::printf("[Title] Press start...\n");
                m_startReady = true;
            });

        // BridgeActionRouter: register handler for "ui.button.start" signal
        m_actions.registerHandler("ui.button.start",
            [this](std::string_view /*payload*/) {
                if (!m_startReady) { return; }
                std::printf("[Bridge] ui.button.start dispatched\n");
                m_router.push(
                    std::make_unique<CookingScene>(m_router, m_actions, m_view)
                );
            });
    }

    void onUpdate(float dt) override {
        m_intro.tick(dt);   // Sequence tick
    }

    void onExit() override {
        m_actions.unregisterHandler("ui.button.start");
    }

private:
    Router&                    m_router;
    ActionRtr&                 m_actions;
    ViewPush&                  m_view;
    mitiru::time::Sequence     m_intro;    // Sequence
    bool                       m_startReady = false;
};

// ---------------------------------------------------------------------------
// main — headless game loop (dt = 0.1s per frame, max 50 frames)
// ---------------------------------------------------------------------------

int main() {
    // BridgeViewPush: stub sinks that printf instead of touching StateStore
    ViewPush view(
        "cooking",
        [](std::string_view key, std::string_view val) {
            std::printf("[Bridge.view] %.*s = %.*s\n",
                static_cast<int>(key.size()), key.data(),
                static_cast<int>(val.size()), val.data());
        },
        [](std::string_view key, std::string_view payload) {
            std::printf("[Bridge.emit] %.*s  payload=%.*s\n",
                static_cast<int>(key.size()), key.data(),
                static_cast<int>(payload.size()), payload.data());
        }
    );

    ActionRtr actions;
    Router    router;   // SceneRouter

    // SceneRouter::push — TitleScene is the first scene
    router.push(std::make_unique<TitleScene>(router, actions, view));

    constexpr float dt        = 0.1f;
    constexpr int   maxFrames = 50;
    int             startFrame = -1;   // frame index when we dispatch start signal

    for (int frame = 0; frame < maxFrames; ++frame) {
        if (router.empty()) { break; }

        router.update(dt);   // SceneRouter::update dispatches to top scene

        // After 1.0 s (frame 10) simulate a button press from the UI bridge
        if (frame == 10 && startFrame < 0) {
            startFrame = frame;
            // BridgeActionRouter::dispatch — fake CEF bridge signal
            actions.dispatch("ui.button.start", "");
        }
    }

    std::printf("[main] loop finished\n");
    return 0;
}
