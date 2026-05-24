#pragma once

/// @file AsyncLoader.hpp
/// @brief 非同期ロードパイプライン
///
/// 優先度付きタスクキューでリソースをロードする。
/// ヘッダーオンリーのため実際のスレッドは使用せず、
/// processNext()で1件ずつ同期的に処理するシミュレーション方式。
///
/// @code
/// sgc::AsyncLoader loader;
/// loader.enqueue({"player.png", sgc::LoadPriority::High,
///     [](const std::string& path) { /* ロード処理 */ }});
/// loader.enqueue({"bg.png", sgc::LoadPriority::Low,
///     [](const std::string& path) { /* ロード処理 */ }});
///
/// // 1件ずつ処理（高優先度から）
/// while (loader.hasPending())
/// {
///     loader.processNext();
/// }
/// @endcode

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "sgc/resource/LoadPriority.hpp"

namespace sgc
{

/// @brief 非同期ロードリクエスト
struct LoadRequest
{
	std::string path;                               ///< ファイルパス
	LoadPriority priority{LoadPriority::Normal};    ///< 優先度
	std::function<void(const std::string&)> callback;  ///< ロード完了コールバック
	std::size_t id{0};                              ///< リクエストID（キャンセル用）
};

/// @brief ロード進捗情報
struct LoadProgress
{
	std::size_t total{0};      ///< 合計リクエスト数
	std::size_t processed{0};  ///< 処理済み数
	std::size_t cancelled{0};  ///< キャンセル数

	/// @brief 進捗率を取得する
	/// @return 0.0f〜1.0f の進捗率
	[[nodiscard]] float ratio() const noexcept
	{
		return total == 0 ? 1.0f : static_cast<float>(processed) / static_cast<float>(total);
	}

	/// @brief 全リクエストが完了したか
	/// @return 完了していればtrue
	[[nodiscard]] bool isComplete() const noexcept
	{
		return (processed + cancelled) >= total;
	}
};

/// @brief 非同期ロードパイプライン
///
/// 優先度付きキューでリソースロードを管理する。
/// processNext()で1件ずつ処理し、processAll()で全件処理する。
/// cancel()でリクエストIDによるキャンセルが可能。
class AsyncLoader
{
public:
	/// @brief リクエストをキューに追加する
	/// @param request ロードリクエスト
	/// @return リクエストID（キャンセル用）
	std::size_t enqueue(LoadRequest request)
	{
		const auto id = m_nextId++;
		request.id = id;
		++m_totalEnqueued;
		m_queue.push_back(std::move(request));

		// 優先度降順でソート（高優先度が先頭）
		std::stable_sort(m_queue.begin(), m_queue.end(),
			[](const LoadRequest& a, const LoadRequest& b)
			{
				return static_cast<int>(a.priority) > static_cast<int>(b.priority);
			});

		return id;
	}

	/// @brief キューの先頭1件を処理する
	/// @return 処理した場合true、キューが空ならfalse
	bool processNext()
	{
		if (m_queue.empty()) return false;

		auto request = std::move(m_queue.front());
		m_queue.erase(m_queue.begin());

		// キャンセルされていなければコールバック実行
		if (m_cancelledIds.end() == std::find(m_cancelledIds.begin(), m_cancelledIds.end(), request.id))
		{
			if (request.callback)
			{
				request.callback(request.path);
			}
			++m_processed;
		}
		else
		{
			++m_cancelledCount;
		}

		return true;
	}

	/// @brief キュー内の全リクエストを処理する
	void processAll()
	{
		while (processNext()) {}
	}

	/// @brief 指定IDのリクエストをキャンセルする
	/// @param requestId キャンセルするリクエストID
	/// @return キャンセルに成功した場合true
	bool cancel(std::size_t requestId)
	{
		// キューから直接削除を試行
		auto it = std::find_if(m_queue.begin(), m_queue.end(),
			[requestId](const LoadRequest& r) { return r.id == requestId; });

		if (it != m_queue.end())
		{
			m_queue.erase(it);
			++m_cancelledCount;
			return true;
		}

		// キュー内になければキャンセルリストに登録
		m_cancelledIds.push_back(requestId);
		return false;
	}

	/// @brief 全リクエストをキャンセルしてキューをクリアする
	void cancelAll()
	{
		m_cancelledCount += m_queue.size();
		m_queue.clear();
		m_cancelledIds.clear();
	}

	/// @brief 保留中のリクエストがあるか
	/// @return キューが空でなければtrue
	[[nodiscard]] bool hasPending() const noexcept
	{
		return !m_queue.empty();
	}

	/// @brief キュー内のリクエスト数を取得する
	/// @return 保留中のリクエスト数
	[[nodiscard]] std::size_t pendingCount() const noexcept
	{
		return m_queue.size();
	}

	/// @brief ロード進捗を取得する
	/// @return 進捗情報
	[[nodiscard]] LoadProgress progress() const noexcept
	{
		return LoadProgress{m_totalEnqueued, m_processed, m_cancelledCount};
	}

private:
	std::vector<LoadRequest> m_queue;           ///< リクエストキュー
	std::vector<std::size_t> m_cancelledIds;    ///< キャンセル済みID
	std::size_t m_nextId{1};                    ///< 次のリクエストID
	std::size_t m_totalEnqueued{0};             ///< 合計キュー追加数
	std::size_t m_processed{0};                 ///< 処理済み数
	std::size_t m_cancelledCount{0};            ///< キャンセル数
};

} // namespace sgc
