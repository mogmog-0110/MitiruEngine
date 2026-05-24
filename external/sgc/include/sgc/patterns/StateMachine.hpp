#pragma once

/// @file StateMachine.hpp
/// @brief std::variant + std::visit ベースの有限状態マシン
///
/// コンパイル時に状態の網羅性をチェックする。
/// 新しい状態を追加してハンドリングを忘れるとコンパイルエラーになる。

#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace sgc
{

/// @brief std::variantベースの有限状態マシン
///
/// 各状態を構造体で定義し、variantで管理する。
/// 状態遷移はupdate()内のvisitorで処理する。
/// onEnter/onExitコールバックで遷移時の処理を追加できる。
///
/// @tparam States 状態型のパック
///
/// @code
/// struct Idle {};
/// struct Walking { float speed; };
/// struct Jumping { float velocity; };
///
/// using PlayerFSM = sgc::StateMachine<Idle, Walking, Jumping>;
///
/// PlayerFSM fsm{Idle{}};
///
/// // onEnter/onExitコールバックを設定
/// fsm.setOnEnter([](const auto& state) {
///     // 新しい状態に入った時の処理
/// });
/// fsm.setOnExit([](const auto& state) {
///     // 現在の状態を出る時の処理
/// });
///
/// fsm.update([](auto& state) -> std::optional<PlayerFSM::StateVariant> {
///     using T = std::decay_t<decltype(state)>;
///     if constexpr (std::is_same_v<T, Idle>) {
///         if (inputPressed) return Walking{5.0f};
///     }
///     return std::nullopt;  // 状態変更なし
/// });
/// @endcode
template <typename... States>
class StateMachine
{
public:
	/// @brief 状態のvariant型
	using StateVariant = std::variant<States...>;

	/// @brief 状態遷移コールバック型
	using TransitionCallback = std::function<void(const StateVariant&)>;

	/// @brief 遷移コールバック型（遷移元と遷移先を受け取る）
	using OnTransitionCallback = std::function<void(const StateVariant&, const StateVariant&)>;

	/// @brief ガード関数型（遷移元と遷移先を受け取り、遷移を許可するかを返す）
	using GuardFunc = std::function<bool(const StateVariant&, const StateVariant&)>;

	/// @brief 初期状態を指定して構築する
	/// @param initialState 初期状態
	template <typename S>
		requires (std::is_same_v<S, States> || ...)
	explicit StateMachine(S initialState)
		: m_current(std::move(initialState))
	{
	}

	/// @brief 状態に入る時のコールバックを設定する
	/// @param callback 新しい状態が設定された直後に呼ばれるコールバック
	void setOnEnter(TransitionCallback callback)
	{
		m_onEnter = std::move(callback);
	}

	/// @brief 状態を出る時のコールバックを設定する
	/// @param callback 古い状態から遷移する直前に呼ばれるコールバック
	void setOnExit(TransitionCallback callback)
	{
		m_onExit = std::move(callback);
	}

	/// @brief 遷移発生時のコールバックを設定する（遷移元と遷移先を受け取る）
	/// @param callback 遷移コールバック
	void setOnTransition(OnTransitionCallback callback)
	{
		m_onTransition = std::move(callback);
	}

	/// @brief ガード関数を追加する
	///
	/// ガード関数がfalseを返すと遷移はキャンセルされる。
	/// 複数のガードが登録されている場合、すべてがtrueを返す必要がある。
	///
	/// @param guard ガード関数
	void addGuard(GuardFunc guard)
	{
		m_guards.push_back(std::move(guard));
	}

	/// @brief 現在の状態を更新する
	///
	/// visitorは各状態を受け取り、遷移先をstd::optional<StateVariant>で返す。
	/// std::nulloptを返すと状態は変化しない。
	/// ガードチェック後、onExit→状態変更→onEnter→onTransitionの順で処理される。
	///
	/// @tparam Visitor visitableな型
	/// @param visitor 状態ごとの処理を定義するvisitor
	template <typename Visitor>
	void update(Visitor&& visitor)
	{
		auto next = std::visit(std::forward<Visitor>(visitor), m_current);
		if (next.has_value())
		{
			// ガードチェック
			for (const auto& guard : m_guards)
			{
				if (!guard(m_current, *next))
				{
					return;  // 遷移キャンセル
				}
			}

			const auto from = m_current;
			if (m_onExit)
			{
				m_onExit(m_current);
			}
			m_previous = m_current;
			m_current = std::move(*next);
			if (m_onEnter)
			{
				m_onEnter(m_current);
			}
			if (m_onTransition)
			{
				m_onTransition(from, m_current);
			}
		}
	}

	/// @brief 現在の状態に対してvisitorを適用する（読み取り専用）
	/// @tparam Visitor visitableな型
	/// @param visitor 各状態を処理するvisitor
	/// @return visitorの戻り値
	template <typename Visitor>
	[[nodiscard]] auto visit(Visitor&& visitor) const
	{
		return std::visit(std::forward<Visitor>(visitor), m_current);
	}

	/// @brief 現在の状態が指定型かどうか判定する
	/// @tparam S 判定する状態型
	/// @return 指定型の状態であれば true
	template <typename S>
	[[nodiscard]] bool isIn() const
	{
		return std::holds_alternative<S>(m_current);
	}

	/// @brief 現在の状態を指定型として取得する
	/// @tparam S 取得する状態型
	/// @return 状態への参照
	/// @throws std::bad_variant_access 指定型でない場合
	template <typename S>
	[[nodiscard]] const S& getState() const
	{
		return std::get<S>(m_current);
	}

	/// @brief 強制的に状態を設定する（onExit/onEnterコールバックも発火する）
	/// @tparam S 設定する状態型
	/// @param state 新しい状態
	template <typename S>
		requires (std::is_same_v<S, States> || ...)
	void setState(S state)
	{
		if (m_onExit)
		{
			m_onExit(m_current);
		}
		m_previous = m_current;
		m_current = std::move(state);
		if (m_onEnter)
		{
			m_onEnter(m_current);
		}
	}

	/// @brief 現在の状態variantへの参照を返す
	[[nodiscard]] const StateVariant& current() const noexcept { return m_current; }

	/// @brief 前の状態を返す（遷移が一度も発生していない場合はnullopt）
	[[nodiscard]] const std::optional<StateVariant>& previousState() const noexcept { return m_previous; }

private:
	StateVariant m_current;
	std::optional<StateVariant> m_previous;  ///< 前の状態
	TransitionCallback m_onEnter;   ///< 状態に入る時のコールバック
	TransitionCallback m_onExit;    ///< 状態を出る時のコールバック
	OnTransitionCallback m_onTransition;  ///< 遷移発生時のコールバック
	std::vector<GuardFunc> m_guards;  ///< ガード関数リスト
};

} // namespace sgc
