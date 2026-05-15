#pragma once

/// @file BridgeInputAdapter.hpp
/// @brief CEF bridge signal を InputMapper アクションに橋渡しするアダプタ
/// @details BridgeActionRouter に handler を登録し、signal が dispatch されると
///          InputMapper::triggerActionFromBridge を呼び出す。
///          gameplay 側は入力ソース (native / DOM) を意識せず
///          InputMapper::isActionPressed だけで判断できる。
///
/// @code
/// mitiru::input::BridgeActionRouter  router;
/// mitiru::InputMapper                mapper;
/// mitiru::input::BridgeInputAdapter  adapter(router, mapper);
///
/// adapter.mapSignalToAction("ui.button.fire", "Fire");
/// mapper.bindKey("Fire", mitiru::KeyCode::Space);
///
/// // CEF bridge から signal が届いたとき:
/// router.dispatch("ui.button.fire");
///
/// // 同フレーム内で native key または bridge どちらでも true になる:
/// bool fired = mapper.isActionPressed("Fire");
///
/// mapper.endFrame();  // フレーム末に bridge trigger をクリア
/// @endcode

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/input/BridgeActionRouter.hpp>
#include <mitiru/input/InputMapper.hpp>

namespace mitiru::input {

/// @brief CEF bridge signal を InputMapper アクションに橋渡しするアダプタ
/// @details signal が dispatch されると対応する action 名で
///          InputMapper::triggerActionFromBridge を呼び出す。
///          Non-copyable, non-movable (内部で参照を保持するため)。
///
///          Lifetime: this object must not outlive m_router or m_mapper.
///          On destruction, all signals registered through mapSignalToAction
///          are automatically unregistered from the router so that subsequent
///          dispatches do not invoke a dangling `this`.
class BridgeInputAdapter {
public:
    /// @param router  signal の dispatch 元となる BridgeActionRouter
    /// @param mapper  action trigger を受け取る InputMapper
    BridgeInputAdapter(BridgeActionRouter&  router,
                       mitiru::InputMapper& mapper) noexcept;

    /// @brief Automatically unregisters every signal this adapter installed.
    /// @details Prevents dangling `this` captures inside the router if the
    ///          adapter is destroyed while the router stays alive.
    ~BridgeInputAdapter();

    BridgeInputAdapter(const BridgeInputAdapter&)            = delete;
    BridgeInputAdapter& operator=(const BridgeInputAdapter&) = delete;
    BridgeInputAdapter(BridgeInputAdapter&&)                 = delete;
    BridgeInputAdapter& operator=(BridgeInputAdapter&&)      = delete;

    /// @brief bridge signal をアクション名に対応付ける
    /// @details signal が dispatch されると InputMapper::triggerActionFromBridge が
    ///          actionName で呼ばれる。同一 signal 名への再登録は上書き (last-write-wins)。
    /// @param signalName 監視する signal の識別子 (例: "ui.button.fire")
    /// @param actionName トリガするアクション名 (例: "Fire")
    void mapSignalToAction(std::string signalName, std::string actionName);

    /// @brief 登録済み signal のマッピングを解除する
    /// @details 未登録の signal 名を指定しても no-op。
    /// @param signalName 解除する signal の識別子
    void unmapSignal(std::string_view signalName);

private:
    BridgeActionRouter&  m_router;
    mitiru::InputMapper& m_mapper;
    /// @brief signal names that this adapter has registered with the router.
    /// @details Used by the destructor to undo every registration so that the
    ///          router cannot later invoke a handler whose captured `this` is
    ///          a dangling pointer.
    std::vector<std::string> m_registered;
};

inline BridgeInputAdapter::BridgeInputAdapter(BridgeActionRouter&  router,
                                              mitiru::InputMapper& mapper) noexcept
    : m_router(router), m_mapper(mapper)
{}

inline BridgeInputAdapter::~BridgeInputAdapter()
{
    for (const auto& signalName : m_registered) {
        m_router.unregisterHandler(signalName);
    }
}

inline void BridgeInputAdapter::mapSignalToAction(std::string signalName,
                                                  std::string actionName)
{
    // Track the signal name BEFORE moving it into registerHandler so we can
    // unregister on destruction. Avoid duplicate entries when re-mapping the
    // same signal so the destructor does not call unregisterHandler twice.
    const auto already = std::find(m_registered.begin(), m_registered.end(), signalName);
    if (already == m_registered.end()) {
        m_registered.push_back(signalName);
    }

    m_router.registerHandler(std::move(signalName),
        [this, action = std::move(actionName)](std::string_view /*payload*/) {
            m_mapper.triggerActionFromBridge(action);
        });
}

inline void BridgeInputAdapter::unmapSignal(std::string_view signalName)
{
    m_router.unregisterHandler(signalName);
    const auto it = std::find(m_registered.begin(), m_registered.end(), signalName);
    if (it != m_registered.end()) {
        m_registered.erase(it);
    }
}

} // namespace mitiru::input
