#pragma once

/// @file StateMachine.hpp
/// @brief 汎用有限状態機械テンプレート
/// @details 任意の enum / enum class 型をステート型として使用できる。
///          Guard コールバックで遷移を条件付きで制御し、
///          onTransition / onRejected コールバックで副作用を分離する。
///
/// @code
/// enum class Phase { Idle, Running, Dead };
///
/// mitiru::fsm::StateMachine<Phase> sm(Phase::Idle);
///
/// sm.setGuard([](Phase from, Phase to) {
///     return !(from == Phase::Dead);   // Dead からは遷移不可
/// });
/// sm.setOnTransition([](Phase from, Phase to) {
///     // ログ / サウンド等
/// });
///
/// sm.transition(Phase::Running);  // true
/// sm.transition(Phase::Dead);     // true
/// sm.transition(Phase::Idle);     // false (Dead からは Guard で弾かれる)
/// @endcode

#include <functional>

namespace mitiru::fsm
{

/// @brief 汎用有限状態機械
/// @tparam StateT ステートを表す型。enum / enum class を推奨。
///                コピーコンストラクト可能かつ等値比較可能であること。
template <typename StateT>
class StateMachine
{
public:
    /// @brief 遷移コールバック型。from → to が確定したときに呼ばれる。
    using Callback = std::function<void(StateT from, StateT to)>;

    /// @brief ガード型。false を返すと遷移を阻止する。
    using Guard = std::function<bool(StateT from, StateT to)>;

    /// @brief 初期ステートを指定して構築する。
    /// @param initial 初期ステート値
    explicit StateMachine(StateT initial) : m_state(initial) {}

    // コピー / ムーブはデフォルト動作で十分
    StateMachine(const StateMachine&)            = default;
    StateMachine& operator=(const StateMachine&) = default;
    StateMachine(StateMachine&&)                 = default;
    StateMachine& operator=(StateMachine&&)      = default;

    /// @brief 現在のステートを返す。
    /// @return 現在の StateT 値
    [[nodiscard]] StateT state() const noexcept { return m_state; }

    /// @brief ステートを next へ遷移させる。
    ///
    /// Guard が設定されており false を返した場合は遷移しない。
    /// その際 onRejected コールバックが設定されていれば呼ばれる。
    /// Guard がない、または true を返した場合は遷移し、
    /// onTransition コールバックが設定されていれば呼ばれる。
    ///
    /// この関数内でヒープ確保は行わない。
    ///
    /// @param next 遷移先ステート
    /// @return 遷移が行われた場合 true、Guard で阻止された場合 false
    bool transition(StateT next)
    {
        const StateT prev = m_state;

        if (m_guard && !m_guard(prev, next))
        {
            if (m_onRejected)
            {
                m_onRejected(prev, next);
            }
            return false;
        }

        m_state = next;

        if (m_onTransition)
        {
            m_onTransition(prev, next);
        }

        return true;
    }

    /// @brief 遷移ガードを設定する。
    ///
    /// ガードは transition() が呼ばれるたびに評価される。
    /// nullptr を渡すとガードなし（常に通過）になる。
    ///
    /// @param guard ガード関数。nullptr 可。
    void setGuard(Guard guard)
    {
        m_guard = std::move(guard);
    }

    /// @brief 遷移成功時コールバックを設定する。
    ///
    /// Guard を通過して実際に状態が変化した直後に呼ばれる。
    /// nullptr を渡すとコールバックなしになる。
    ///
    /// @param cb コールバック関数。nullptr 可。
    void setOnTransition(Callback cb)
    {
        m_onTransition = std::move(cb);
    }

    /// @brief Guard に阻止されたときのコールバックを設定する。
    ///
    /// Guard が false を返した場合に呼ばれる。状態は変化しない。
    /// nullptr を渡すとコールバックなしになる。
    ///
    /// @param cb コールバック関数。nullptr 可。
    void setOnRejected(Callback cb)
    {
        m_onRejected = std::move(cb);
    }

private:
    StateT   m_state;
    Guard    m_guard;
    Callback m_onTransition;
    Callback m_onRejected;
};

} // namespace mitiru::fsm
