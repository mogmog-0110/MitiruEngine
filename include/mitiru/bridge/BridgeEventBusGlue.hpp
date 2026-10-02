#pragma once

/// @file BridgeEventBusGlue.hpp
/// @brief UI から届く signal を typed な EventBus event に流す glue class。
/// @details BridgeActionRouter と EventBus を一緒に wrap し、signal-to-event の
///          mapping 登録が 1 回の呼び出しで済むようにする。
///          この class は header-only かつ copy 不可 / move 不可。両 collaborator
///          への参照を保持するため。
///
/// @code
/// mitiru::EventBus bus;
/// mitiru::input::BridgeActionRouter router;
/// mitiru::bridge::BridgeEventBusGlue glue{router, bus};
///
/// struct FireEvent { std::string slot; };
///
/// glue.mapSignal<FireEvent>("ui.button.fire", [](std::string_view payload) {
///     return FireEvent{ std::string(payload) };
/// });
///
/// // payload 無し (trivial) variant:
/// struct PauseEvent {};
/// glue.mapSignalToTrivial<PauseEvent>("ui.menu.pause");
///
/// // UI の signal を受け取ったところで:
/// router.dispatch("ui.button.fire", "slot=3");  // FireEvent{"slot=3"} を publish
/// @endcode

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/core/EventBus.hpp>
#include <mitiru/input/BridgeActionRouter.hpp>

namespace mitiru::bridge {

/// @brief UI の signal を型付きの EventBus event に流す。
/// @details 登録された各 mapping は BridgeActionRouter に handler を設置し、
///          dispatch 時に builder 関数を呼び出して生成した event を EventBus に
///          publish する。
///
///          Lifetime: router と bus は、この object より長く存続する必要がある。
///          破棄時には、この glue が登録したすべての signal を router から削除するため、
///          以後の dispatch で、captured `this` が dangling になった handler が
///          呼び出されることはない。
class BridgeEventBusGlue
{
public:
    /// @brief glue を構築し、既存の router と bus に束縛する。
    /// @param router 生の signal を受け取る BridgeActionRouter。
    /// @param bus    typed な gameplay event を受け取る EventBus。
    BridgeEventBusGlue(mitiru::input::BridgeActionRouter& router,
                       mitiru::EventBus& bus) noexcept
        : m_router(router)
        , m_bus(bus)
    {
    }

    /// @brief この glue が設置したすべての signal を自動的に登録解除する。
    /// @details m_registered を走査し、各 entry を router から削除する。
    ///          captured `this` (または m_bus 参照) が dangling になった handler に
    ///          router が後から dispatch するのを防ぐ。
    ~BridgeEventBusGlue()
    {
        for (const auto& signalName : m_registered) {
            m_router.unregisterHandler(signalName);
        }
    }

    BridgeEventBusGlue(const BridgeEventBusGlue&)            = delete;
    BridgeEventBusGlue& operator=(const BridgeEventBusGlue&) = delete;
    BridgeEventBusGlue(BridgeEventBusGlue&&)                 = delete;
    BridgeEventBusGlue& operator=(BridgeEventBusGlue&&)      = delete;

    /// @brief signal 名を builder 関数経由で typed な EventBus event に map する。
    /// @details builder は生の payload string_view を受け取り Event の instance を
    ///          返す。それが bus に publish される。
    ///          同じ signal 名を 2 回登録すると以前の mapping を上書きする
    ///          (last-write-wins。BridgeActionRouter の semantics に倣う)。
    /// @tparam Event publish する event 型。copy-constructible である必要がある。
    /// @param signalName signal 名 (例: "ui.button.fire")。
    /// @param builder    payload を Event の instance に変換する callable。
    template <typename Event>
    void mapSignal(std::string signalName,
                   std::function<Event(std::string_view)> builder)
    {
        trackSignal(signalName);
        m_router.registerHandler(std::move(signalName),
            [this, builder = std::move(builder)](std::string_view payload) {
                m_bus.publish(builder(payload));
            });
    }

    /// @brief payload を無視し、default 構築した event に signal を map する。
    /// @details 意味のあるデータを持たない signal 用の便利な overload。
    ///          dispatch ごとに value 初期化した Event{} を publish する。
    /// @tparam Event publish する event 型。default-constructible である必要がある。
    /// @param signalName signal 名 (例: "ui.menu.pause")。
    template <typename Event>
    void mapSignalToTrivial(std::string signalName)
    {
        trackSignal(signalName);
        m_router.registerHandler(std::move(signalName),
            [this](std::string_view /*payload*/) {
                m_bus.publish(Event{});
            });
    }

    /// @brief 以前に登録した signal mapping を削除する。
    /// @details その signal 名が一度も登録されていなければ、何もしない。
    /// @param signalName 削除する signal 名。
    void unmap(std::string_view signalName)
    {
        m_router.unregisterHandler(signalName);
        const auto it = std::find(m_registered.begin(), m_registered.end(), signalName);
        if (it != m_registered.end()) {
            m_registered.erase(it);
        }
    }

private:
    /// @brief signal 名がまだ無ければ、m_registered に追加する。
    /// @details 重複を取り除く。同じ signal を再度 map しても、destructor が
    ///          unregisterHandler を 2 回呼ばないようにするため。
    void trackSignal(const std::string& signalName)
    {
        const auto it = std::find(m_registered.begin(), m_registered.end(), signalName);
        if (it == m_registered.end()) {
            m_registered.push_back(signalName);
        }
    }

    mitiru::input::BridgeActionRouter& m_router;
    mitiru::EventBus&                  m_bus;
    /// @brief この glue が router に登録した signal 名。
    /// @details destructor がすべての登録を取り消すために使う。router が後から
    ///          dangling になった captured `this` に dispatch できないようにするため。
    std::vector<std::string>           m_registered;
};

} // namespace mitiru::bridge
