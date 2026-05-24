#pragma once

/// @file Observer.hpp
/// @brief 軽量Signal/Slotパターン（1:1シグナル通知）
///
/// Qt風のシグナル/スロット機構。EventDispatcherより軽量で、
/// 特定のイベントに特化した1対1の通知に適している。
///
/// @note EventDispatcherとの使い分け:
///   - Observer (Signal/Slot): 1:1の直接通知。コンポーネント間の直接的な依存関係に使用。
///   - EventDispatcher: 1:Nのイベントバス（パブリッシュ/サブスクライブ）。
///     グローバルイベントや疎結合な通知に使用。

#include <cstdint>
#include <functional>
#include <vector>

namespace sgc
{

/// @brief シグナル接続ID
using ConnectionId = std::uint64_t;

/// @brief 軽量シグナル — 任意の引数でスロット（コールバック）を通知する
///
/// @tparam Args スロットが受け取る引数の型
///
/// @code
/// sgc::Signal<int> onHealthChanged;
///
/// auto id = onHealthChanged.connect([](int hp) {
///     updateHPBar(hp);
/// });
///
/// onHealthChanged.emit(75);   // 全スロットに通知
/// onHealthChanged.disconnect(id);
/// @endcode
template <typename... Args>
class Signal
{
public:
	/// @brief スロットを接続する
	/// @param slot コールバック関数
	/// @return 接続ID（disconnect用）
	ConnectionId connect(std::function<void(Args...)> slot)
	{
		const auto id = m_nextId++;
		m_slots.push_back({id, std::move(slot)});
		return id;
	}

	/// @brief スロットを切断する
	/// @param id 接続ID
	void disconnect(ConnectionId id)
	{
		auto it = std::remove_if(m_slots.begin(), m_slots.end(),
			[id](const SlotEntry& entry) { return entry.id == id; });
		m_slots.erase(it, m_slots.end());
	}

	/// @brief 全スロットにシグナルを発火する
	/// @param args スロットに渡す引数
	void emit(Args... args) const
	{
		for (const auto& entry : m_slots)
		{
			entry.callback(args...);
		}
	}

	/// @brief 全スロットを切断する
	void disconnectAll()
	{
		m_slots.clear();
	}

	/// @brief 接続中のスロット数を返す
	[[nodiscard]] std::size_t connectionCount() const noexcept
	{
		return m_slots.size();
	}

private:
	struct SlotEntry
	{
		ConnectionId id;
		std::function<void(Args...)> callback;
	};

	std::vector<SlotEntry> m_slots;
	ConnectionId m_nextId = 1;
};

/// @brief RAII型のシグナル接続管理
///
/// スコープを抜けると自動的にdisconnectする。
/// ムーブのみ可能（コピー不可）。
///
/// @tparam Args シグナルの引数型
///
/// @code
/// sgc::Signal<int> onDamage;
/// {
///     sgc::ScopedConnection conn(onDamage, [](int d) { /* ... */ });
///     onDamage.emit(10);  // コールバック呼ばれる
/// }
/// onDamage.emit(20);  // もう呼ばれない
/// @endcode
template <typename... Args>
class ScopedConnection
{
public:
	/// @brief シグナルにスロットを接続し、自動管理する
	/// @param signal 接続先のシグナル
	/// @param slot コールバック関数
	ScopedConnection(Signal<Args...>& signal, std::function<void(Args...)> slot)
		: m_signal(&signal)
		, m_id(signal.connect(std::move(slot)))
	{
	}

	/// @brief デストラクタ — 自動的にdisconnectする
	~ScopedConnection()
	{
		disconnect();
	}

	/// @brief コピー禁止
	ScopedConnection(const ScopedConnection&) = delete;
	ScopedConnection& operator=(const ScopedConnection&) = delete;

	/// @brief ムーブコンストラクタ
	ScopedConnection(ScopedConnection&& other) noexcept
		: m_signal(other.m_signal)
		, m_id(other.m_id)
	{
		other.m_signal = nullptr;
		other.m_id = 0;
	}

	/// @brief ムーブ代入演算子
	ScopedConnection& operator=(ScopedConnection&& other) noexcept
	{
		if (this != &other)
		{
			disconnect();
			m_signal = other.m_signal;
			m_id = other.m_id;
			other.m_signal = nullptr;
			other.m_id = 0;
		}
		return *this;
	}

	/// @brief 手動でdisconnectする
	void disconnect()
	{
		if (m_signal && m_id != 0)
		{
			m_signal->disconnect(m_id);
			m_signal = nullptr;
			m_id = 0;
		}
	}

	/// @brief 接続IDを返す
	[[nodiscard]] ConnectionId id() const noexcept { return m_id; }

private:
	Signal<Args...>* m_signal = nullptr;
	ConnectionId m_id = 0;
};

} // namespace sgc
