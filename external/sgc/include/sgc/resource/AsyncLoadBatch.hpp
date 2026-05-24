
#pragma once

/// @file AsyncLoadBatch.hpp
/// @brief 非同期ロードバッチ
///
/// 複数のリソースロードをグループ化し、進捗追跡とコールバックを提供する。
/// メインスレッドからprocessPendingCallbacks()を呼ぶことで、
/// ワーカースレッドで発生したイベントを安全にコールバックする。
///
/// @code
/// auto batch = std::make_shared<sgc::AsyncLoadBatch>();
/// batch->onProgress([](const sgc::BatchProgress& p) {
///     std::cout << "Progress: " << p.ratio() * 100 << "%\n";
/// });
/// batch->onComplete([](const sgc::BatchProgress& p) {
///     std::cout << "Done! Loaded: " << p.loaded << ", Failed: " << p.failed << "\n";
/// });
///
/// // リソースを追加
/// batch->addPending();
/// batch->addPending();
///
/// // ワーカースレッドからの通知
/// batch->notifyLoaded();
/// batch->notifyFailed();
///
/// // メインスレッドでコールバック処理
/// batch->processPendingCallbacks();
/// @endcode

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

namespace sgc
{

/// @brief バッチロードの進捗情報
struct BatchProgress
{
	std::size_t total = 0;   ///< 合計リソース数
	std::size_t loaded = 0;  ///< ロード成功数
	std::size_t failed = 0;  ///< ロード失敗数

	/// @brief 進捗率を取得する
	/// @return 0.0f〜1.0f の進捗率（totalが0の場合は1.0f）
	[[nodiscard]] float ratio() const noexcept
	{
		return total == 0 ? 1.0f : static_cast<float>(loaded + failed) / static_cast<float>(total);
	}

	/// @brief 全リソースの処理が完了したか
	/// @return 完了していればtrue
	[[nodiscard]] bool isComplete() const noexcept
	{
		return (loaded + failed) >= total;
	}
};

/// @brief 非同期ロードバッチ
///
/// 複数のリソースロードをグループ化し、進捗追跡とコールバックを提供する。
/// addPending()で予約し、notifyLoaded()/notifyFailed()で完了を通知する。
/// processPendingCallbacks()をメインスレッドから呼ぶことで、
/// スレッドセーフにコールバックを実行する。
class AsyncLoadBatch
{
public:
	/// @brief 進捗コールバックを設定する
	/// @param callback 進捗が変化するたびに呼ばれるコールバック
	void onProgress(std::function<void(const BatchProgress&)> callback)
	{
		std::scoped_lock lock(m_callbackMutex);
		m_onProgress = std::move(callback);
	}

	/// @brief 完了コールバックを設定する
	/// @param callback 全リソースの処理完了時に呼ばれるコールバック
	void onComplete(std::function<void(const BatchProgress&)> callback)
	{
		std::scoped_lock lock(m_callbackMutex);
		m_onComplete = std::move(callback);
	}

	/// @brief 現在の進捗を取得する
	/// @return 進捗情報
	[[nodiscard]] BatchProgress progress() const
	{
		return BatchProgress{
			m_total.load(std::memory_order_acquire),
			m_loaded.load(std::memory_order_acquire),
			m_failed.load(std::memory_order_acquire)
		};
	}

	/// @brief バッチをキャンセルする
	void cancel()
	{
		m_cancelled.store(true, std::memory_order_release);
	}

	/// @brief キャンセルされたか
	/// @return キャンセル済みならtrue
	[[nodiscard]] bool isCancelled() const noexcept
	{
		return m_cancelled.load(std::memory_order_acquire);
	}

	/// @brief ロード成功を通知する（ワーカースレッドから呼ぶ）
	void notifyLoaded()
	{
		m_loaded.fetch_add(1, std::memory_order_acq_rel);
		enqueueCallbacks();
	}

	/// @brief ロード失敗を通知する（ワーカースレッドから呼ぶ）
	void notifyFailed()
	{
		m_failed.fetch_add(1, std::memory_order_acq_rel);
		enqueueCallbacks();
	}

	/// @brief バッチにリソースを追加予約する（内部用）
	void addPending()
	{
		m_total.fetch_add(1, std::memory_order_acq_rel);
	}

	/// @brief 保留中のコールバックを処理する（メインスレッドから呼ぶ）
	void processPendingCallbacks()
	{
		std::vector<std::function<void()>> callbacks;
		{
			std::scoped_lock lock(m_callbackMutex);
			callbacks.swap(m_pendingCallbacks);
		}
		for (const auto& cb : callbacks)
		{
			cb();
		}
	}

private:
	/// @brief 進捗/完了コールバックをキューに追加する
	void enqueueCallbacks()
	{
		const auto prog = progress();
		std::scoped_lock lock(m_callbackMutex);

		if (m_onProgress)
		{
			auto progressCb = m_onProgress;
			m_pendingCallbacks.push_back([progressCb, prog]() { progressCb(prog); });
		}

		if (prog.isComplete() && m_onComplete)
		{
			auto completeCb = m_onComplete;
			m_pendingCallbacks.push_back([completeCb, prog]() { completeCb(prog); });
		}
	}

	std::atomic<std::size_t> m_total{0};    ///< 合計リソース数
	std::atomic<std::size_t> m_loaded{0};   ///< ロード成功数
	std::atomic<std::size_t> m_failed{0};   ///< ロード失敗数
	std::atomic<bool> m_cancelled{false};   ///< キャンセルフラグ

	std::function<void(const BatchProgress&)> m_onProgress;   ///< 進捗コールバック
	std::function<void(const BatchProgress&)> m_onComplete;   ///< 完了コールバック
	std::mutex m_callbackMutex;                                ///< コールバック保護用ミューテックス
	std::vector<std::function<void()>> m_pendingCallbacks;     ///< 保留中のコールバックキュー
};

} // namespace sgc
