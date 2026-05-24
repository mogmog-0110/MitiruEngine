#pragma once

/// @file Coroutine.hpp
/// @brief C++20コルーチンによる非同期タスクとジェネレータ
///
/// ゲームのフレームベース非同期処理に使用する。
/// Task（co_await/co_return対応）、Generator（co_yield対応）、
/// CoroutineSchedulerを提供する。

#include <coroutine>
#include <functional>
#include <optional>
#include <vector>

namespace sgc
{

// ── Awaitable ────────────────────────────────────────────────────

/// @brief 指定秒数待機するAwaitable
struct WaitForSeconds
{
	float seconds;  ///< 待機秒数
};

/// @brief 1フレーム待機するAwaitable
struct WaitForNextFrame {};

/// @brief 条件が真になるまで待機するAwaitable
struct WaitUntil
{
	std::function<bool()> predicate;  ///< 判定関数
};

// ── Task ─────────────────────────────────────────────────────────

/// @brief コルーチンタスク戻り値型
///
/// @code
/// sgc::Task myCoroutine()
/// {
///     co_await sgc::WaitForSeconds{1.0f};
///     co_await sgc::WaitForNextFrame{};
///     co_return;
/// }
/// @endcode
class Task
{
public:
	/// @brief コルーチンの待機状態
	enum class WaitState
	{
		Ready,     ///< 即座に再開可能
		Seconds,   ///< 秒数待機中
		NextFrame, ///< 次フレーム待機中
		Until      ///< 条件待機中
	};

	struct promise_type;
	using Handle = std::coroutine_handle<promise_type>;

	/// @brief Promise型
	struct promise_type
	{
		WaitState waitState{WaitState::Ready};
		float waitRemaining{0.0f};
		std::function<bool()> waitPredicate;

		[[nodiscard]] Task get_return_object()
		{
			return Task{Handle::from_promise(*this)};
		}

		[[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
		[[nodiscard]] std::suspend_always final_suspend() const noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() noexcept {}

		/// @brief WaitForSeconds の co_await 対応
		auto await_transform(WaitForSeconds w)
		{
			struct Awaiter
			{
				promise_type& promise;
				float seconds;

				[[nodiscard]] bool await_ready() const noexcept { return false; }
				void await_suspend(std::coroutine_handle<>) noexcept
				{
					promise.waitState = WaitState::Seconds;
					promise.waitRemaining = seconds;
				}
				void await_resume() noexcept {}
			};
			return Awaiter{*this, w.seconds};
		}

		/// @brief WaitForNextFrame の co_await 対応
		auto await_transform(WaitForNextFrame)
		{
			struct Awaiter
			{
				promise_type& promise;

				[[nodiscard]] bool await_ready() const noexcept { return false; }
				void await_suspend(std::coroutine_handle<>) noexcept
				{
					promise.waitState = WaitState::NextFrame;
				}
				void await_resume() noexcept {}
			};
			return Awaiter{*this};
		}

		/// @brief WaitUntil の co_await 対応
		auto await_transform(WaitUntil w)
		{
			struct Awaiter
			{
				promise_type& promise;
				std::function<bool()> pred;

				[[nodiscard]] bool await_ready() const noexcept { return false; }
				void await_suspend(std::coroutine_handle<>) noexcept
				{
					promise.waitState = WaitState::Until;
					promise.waitPredicate = std::move(pred);
				}
				void await_resume() noexcept {}
			};
			return Awaiter{*this, std::move(w.predicate)};
		}
	};

	Task() = default;

	explicit Task(Handle handle) : m_handle(handle) {}

	~Task()
	{
		if (m_handle) m_handle.destroy();
	}

	Task(const Task&) = delete;
	Task& operator=(const Task&) = delete;

	Task(Task&& other) noexcept : m_handle(other.m_handle)
	{
		other.m_handle = nullptr;
	}

	Task& operator=(Task&& other) noexcept
	{
		if (this != &other)
		{
			if (m_handle) m_handle.destroy();
			m_handle = other.m_handle;
			other.m_handle = nullptr;
		}
		return *this;
	}

	/// @brief コルーチンを1ステップ再開する
	void resume()
	{
		if (m_handle && !m_handle.done())
		{
			m_handle.resume();
		}
	}

	/// @brief コルーチンが完了しているか
	[[nodiscard]] bool done() const noexcept
	{
		return !m_handle || m_handle.done();
	}

	/// @brief コルーチンをキャンセルする
	void cancel()
	{
		if (m_handle)
		{
			m_handle.destroy();
			m_handle = nullptr;
		}
	}

	/// @brief 内部ハンドルを取得する（SchedulerAPI用）
	[[nodiscard]] Handle handle() const noexcept { return m_handle; }

private:
	Handle m_handle{nullptr};
};

// ── Generator ────────────────────────────────────────────────────

/// @brief 遅延シーケンス生成用ジェネレータ
/// @tparam T 生成する値の型
///
/// @code
/// sgc::Generator<int> range(int start, int end)
/// {
///     for (int i = start; i < end; ++i)
///         co_yield i;
/// }
///
/// for (int v : range(0, 10))
///     // ...
/// @endcode
template <typename T>
class Generator
{
public:
	struct promise_type;
	using Handle = std::coroutine_handle<promise_type>;

	/// @brief Promise型
	struct promise_type
	{
		std::optional<T> currentValue;

		[[nodiscard]] Generator get_return_object()
		{
			return Generator{Handle::from_promise(*this)};
		}

		[[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
		[[nodiscard]] std::suspend_always final_suspend() const noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() noexcept {}

		std::suspend_always yield_value(T value)
		{
			currentValue = std::move(value);
			return {};
		}
	};

	Generator() = default;
	explicit Generator(Handle handle) : m_handle(handle) {}

	~Generator()
	{
		if (m_handle) m_handle.destroy();
	}

	Generator(const Generator&) = delete;
	Generator& operator=(const Generator&) = delete;

	Generator(Generator&& other) noexcept : m_handle(other.m_handle)
	{
		other.m_handle = nullptr;
	}

	Generator& operator=(Generator&& other) noexcept
	{
		if (this != &other)
		{
			if (m_handle) m_handle.destroy();
			m_handle = other.m_handle;
			other.m_handle = nullptr;
		}
		return *this;
	}

	/// @brief range-for対応のイテレータ
	struct Iterator
	{
		Handle handle;

		Iterator& operator++()
		{
			handle.resume();
			return *this;
		}

		[[nodiscard]] const T& operator*() const
		{
			return *handle.promise().currentValue;
		}

		[[nodiscard]] bool operator==(std::default_sentinel_t) const
		{
			return !handle || handle.done();
		}
	};

	/// @brief range-forの開始
	[[nodiscard]] Iterator begin()
	{
		if (m_handle) m_handle.resume();
		return Iterator{m_handle};
	}

	/// @brief range-forの終端
	[[nodiscard]] std::default_sentinel_t end() const noexcept
	{
		return {};
	}

private:
	Handle m_handle{nullptr};
};

// ── CoroutineScheduler ──────────────────────────────────────────

/// @brief コルーチンID型
using CoroutineId = std::size_t;

/// @brief コルーチンスケジューラ
///
/// 毎フレームtick()を呼び出すことで、登録されたコルーチンを管理する。
///
/// @code
/// sgc::CoroutineScheduler scheduler;
/// auto id = scheduler.start(myCoroutine());
/// // 毎フレーム
/// scheduler.tick(deltaTime);
/// @endcode
class CoroutineScheduler
{
public:
	/// @brief コルーチンを開始する
	/// @param task 実行するタスク
	/// @return コルーチンID
	CoroutineId start(Task task)
	{
		const CoroutineId id = m_nextId++;
		m_tasks.push_back({id, std::move(task)});
		// 初回resume（initial_suspendから起動）
		m_tasks.back().task.resume();
		return id;
	}

	/// @brief 毎フレーム呼び出す
	/// @param deltaTime フレーム経過時間（秒）
	void tick(float deltaTime)
	{
		for (auto it = m_tasks.begin(); it != m_tasks.end();)
		{
			if (it->task.done())
			{
				it = m_tasks.erase(it);
				continue;
			}

			const auto handle = it->task.handle();
			auto& promise = handle.promise();

			bool shouldResume = false;

			switch (promise.waitState)
			{
			case Task::WaitState::Ready:
				shouldResume = true;
				break;

			case Task::WaitState::Seconds:
				promise.waitRemaining -= deltaTime;
				if (promise.waitRemaining <= 0.0f)
				{
					promise.waitState = Task::WaitState::Ready;
					shouldResume = true;
				}
				break;

			case Task::WaitState::NextFrame:
				promise.waitState = Task::WaitState::Ready;
				shouldResume = true;
				break;

			case Task::WaitState::Until:
				if (promise.waitPredicate && promise.waitPredicate())
				{
					promise.waitState = Task::WaitState::Ready;
					shouldResume = true;
				}
				break;
			}

			if (shouldResume)
			{
				it->task.resume();
			}

			++it;
		}
	}

	/// @brief コルーチンをキャンセルする
	/// @param id キャンセルするコルーチンのID
	void cancel(CoroutineId id)
	{
		for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it)
		{
			if (it->id == id)
			{
				m_tasks.erase(it);
				return;
			}
		}
	}

	/// @brief 全コルーチンをキャンセルする
	void cancelAll()
	{
		m_tasks.clear();
	}

	/// @brief アクティブなコルーチン数を返す
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		return m_tasks.size();
	}

private:
	struct Entry
	{
		CoroutineId id;
		Task task;
	};

	std::vector<Entry> m_tasks;
	CoroutineId m_nextId{0};
};

} // namespace sgc
