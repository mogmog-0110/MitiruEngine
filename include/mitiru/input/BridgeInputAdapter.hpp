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
///          Lifetime: 本 object は m_router / m_mapper より長生きしてはならない。
///          破棄時に mapSignalToAction で登録した全 signal を router から
///          自動的に解除するので、以降の dispatch が dangling な `this` を
///          呼び出すことはない。
class BridgeInputAdapter {
public:
    /// @param router  signal の dispatch 元となる BridgeActionRouter
    /// @param mapper  action trigger を受け取る InputMapper
    BridgeInputAdapter(BridgeActionRouter&  router,
                       mitiru::InputMapper& mapper) noexcept;

    /// @brief このアダプタが登録した全 signal を自動的に解除する
    /// @details router が生存したまま adapter だけ破棄された場合に、router 内に
    ///          残る dangling な `this` キャプチャを防ぐ。
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
    /// @brief このアダプタが router に登録した signal 名の一覧
    /// @details destructor が全登録を取り消すために使う。これにより router が
    ///          後で dangling pointer の `this` をキャプチャした handler を
    ///          呼び出せないようにする。
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
    // registerHandler に move する前に signal 名を記録しておき、破棄時に
    // 解除できるようにする。同一 signal を再マップしたときに重複登録しないよう
    // にして、destructor が unregisterHandler を二重に呼ばないようにする。
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
