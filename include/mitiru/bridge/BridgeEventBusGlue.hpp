#pragma once

/// @file BridgeEventBusGlue.hpp
/// @brief Glue class that routes CEF bridge signals into typed EventBus events.
/// @details Wraps a BridgeActionRouter and an EventBus together so that
///          registering a signal-to-event mapping requires only one call.
///          The class is header-only, non-copyable, and non-movable because it
///          stores references to both collaborators.
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
/// // Trivial (no-payload) variant:
/// struct PauseEvent {};
/// glue.mapSignalToTrivial<PauseEvent>("ui.menu.pause");
///
/// // CEF bridge callback:
/// router.dispatch("ui.button.fire", "slot=3");  // publishes FireEvent{"slot=3"}
/// @endcode

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/core/EventBus.hpp>
#include <mitiru/input/BridgeActionRouter.hpp>

namespace mitiru::bridge {

/// @brief Routes CEF bridge signals to typed EventBus events.
/// @details Each registered mapping installs a handler in the BridgeActionRouter
///          that, when dispatched, invokes a builder function and publishes the
///          resulting event to the EventBus.
///
///          Lifetime: both the router and the bus must outlive this object.
///          On destruction, every signal this glue registered is removed from
///          the router so that subsequent dispatches cannot invoke a handler
///          whose captured `this` is dangling.
class BridgeEventBusGlue
{
public:
    /// @brief Constructs the glue, binding it to an existing router and bus.
    /// @param router The BridgeActionRouter that receives raw CEF signals.
    /// @param bus    The EventBus that receives typed gameplay events.
    BridgeEventBusGlue(mitiru::input::BridgeActionRouter& router,
                       mitiru::EventBus& bus) noexcept
        : m_router(router)
        , m_bus(bus)
    {
    }

    /// @brief Automatically unregisters every signal this glue installed.
    /// @details Walks m_registered and removes each entry from the router.
    ///          Prevents the router from later dispatching to a handler whose
    ///          captured `this` (or m_bus reference) is dangling.
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

    /// @brief Map a signal name to a typed EventBus event via a builder function.
    /// @details The builder receives the raw payload string_view and returns an
    ///          instance of Event, which is then published to the bus.
    ///          Registering the same signal name twice overwrites the prior mapping
    ///          (last-write-wins — mirrors BridgeActionRouter semantics).
    /// @tparam Event The event type to publish. Must be copy-constructible.
    /// @param signalName Name of the CEF signal (e.g. "ui.button.fire").
    /// @param builder    Callable that converts payload to an Event instance.
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

    /// @brief Map a signal to a default-constructed event, ignoring the payload.
    /// @details Convenience overload for signals that carry no meaningful data.
    ///          Publishes a value-initialized Event{} on every dispatch.
    /// @tparam Event The event type to publish. Must be default-constructible.
    /// @param signalName Name of the CEF signal (e.g. "ui.menu.pause").
    template <typename Event>
    void mapSignalToTrivial(std::string signalName)
    {
        trackSignal(signalName);
        m_router.registerHandler(std::move(signalName),
            [this](std::string_view /*payload*/) {
                m_bus.publish(Event{});
            });
    }

    /// @brief Remove a previously registered signal mapping.
    /// @details No-op if the signal name was never registered.
    /// @param signalName The signal name to remove.
    void unmap(std::string_view signalName)
    {
        m_router.unregisterHandler(signalName);
        const auto it = std::find(m_registered.begin(), m_registered.end(), signalName);
        if (it != m_registered.end()) {
            m_registered.erase(it);
        }
    }

private:
    /// @brief Add a signal name to m_registered if not already present.
    /// @details De-duplicates so that re-mapping the same signal does not
    ///          cause the destructor to call unregisterHandler twice.
    void trackSignal(const std::string& signalName)
    {
        const auto it = std::find(m_registered.begin(), m_registered.end(), signalName);
        if (it == m_registered.end()) {
            m_registered.push_back(signalName);
        }
    }

    mitiru::input::BridgeActionRouter& m_router;
    mitiru::EventBus&                  m_bus;
    /// @brief Signal names this glue registered with the router.
    /// @details Used by the destructor to undo every registration so that the
    ///          router cannot later dispatch to a dangling captured `this`.
    std::vector<std::string>           m_registered;
};

} // namespace mitiru::bridge
