#pragma once

/// @file AssetCache.hpp
/// @brief LRU キャッシュによるアセット管理
/// @details テンプレートベースの LRU キャッシュ。メモリバジェット制約と
///          スレッドセーフなアクセス（shared_mutex）を提供する。

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace mitiru::resource
{

/// @brief キャッシュ統計情報
struct CacheStatistics
{
	std::size_t hits = 0;          ///< キャッシュヒット数
	std::size_t misses = 0;        ///< キャッシュミス数
	std::size_t evictions = 0;     ///< エビクション（追い出し）回数
	std::size_t currentEntries = 0; ///< 現在のエントリ数
	std::size_t currentBytes = 0;   ///< 現在の使用バイト数
	std::size_t budgetBytes = 0;    ///< メモリバジェット

	/// @brief キャッシュヒット率を取得する
	/// @return ヒット率（0.0 〜 1.0、アクセスなしの場合は 0.0）
	[[nodiscard]] double hitRate() const noexcept
	{
		const auto total = hits + misses;
		return (total > 0) ? (static_cast<double>(hits) / static_cast<double>(total)) : 0.0;
	}
};

/// @brief アセットサイズ計算用コンセプト
/// @tparam F サイズ計算関数型
/// @tparam T アセット型
template <typename F, typename T>
concept SizeEstimator = requires(F estimator, const T& asset)
{
	{ estimator(asset) } -> std::convertible_to<std::size_t>;
};

/// @brief LRU アセットキャッシュ
/// @tparam T アセット型
/// @details shared_ptr で管理されたアセットを LRU 方式でキャッシュする。
///          メモリバジェットを超過した場合、最も使われていないエントリを自動削除する。
///          shared_mutex によりリーダー並行・ライター排他のスレッドセーフを実現する。
///
/// @code
/// mitiru::resource::AssetCache<Texture> cache(64 * 1024 * 1024); // 64MB
/// cache.setSizeEstimator([](const Texture& t) -> std::size_t {
///     return t.width() * t.height() * 4;
/// });
/// cache.put("player", playerTexture);
/// auto tex = cache.get("player");  // optional<shared_ptr<Texture>>
/// @endcode
template <typename T>
class AssetCache
{
public:
	/// @brief コンストラクタ
	/// @param budgetBytes メモリバジェット（バイト単位、0 = 無制限）
	explicit AssetCache(std::size_t budgetBytes = 0) noexcept
		: m_budgetBytes(budgetBytes)
	{
	}

	/// コピー禁止
	AssetCache(const AssetCache&) = delete;
	AssetCache& operator=(const AssetCache&) = delete;

	/// ムーブ禁止（shared_mutex は非ムーブ）
	AssetCache(AssetCache&&) = delete;
	AssetCache& operator=(AssetCache&&) = delete;

	/// @brief サイズ推定関数を設定する
	/// @param estimator アセットからバイトサイズを推定する関数
	void setSizeEstimator(std::function<std::size_t(const T&)> estimator)
	{
		const std::unique_lock lock(m_mutex);
		m_sizeEstimator = std::move(estimator);
	}

	/// @brief アセットをキャッシュに格納する
	/// @param key キャッシュキー
	/// @param asset アセットの shared_ptr
	void put(const std::string& key, std::shared_ptr<T> asset)
	{
		if (!asset)
		{
			return;
		}

		const std::size_t assetSize = estimateSize(*asset);

		const std::unique_lock lock(m_mutex);

		/// 既存エントリがあれば削除する
		auto mapIt = m_lookup.find(key);
		if (mapIt != m_lookup.end())
		{
			m_currentBytes -= mapIt->second->sizeBytes;
			m_lruList.erase(mapIt->second);
			m_lookup.erase(mapIt);
		}

		/// バジェット超過分をエビクションする
		evictUntilFits(assetSize);

		/// 新エントリを先頭に挿入する
		m_lruList.push_front(CacheEntry{key, std::move(asset), assetSize});
		m_lookup[key] = m_lruList.begin();
		m_currentBytes += assetSize;
	}

	/// @brief キャッシュからアセットを取得する
	/// @param key キャッシュキー
	/// @return アセット（キャッシュミスの場合は nullopt）
	[[nodiscard]] std::optional<std::shared_ptr<T>> get(const std::string& key)
	{
		const std::unique_lock lock(m_mutex);

		auto mapIt = m_lookup.find(key);
		if (mapIt == m_lookup.end())
		{
			++m_stats.misses;
			return std::nullopt;
		}

		++m_stats.hits;

		/// LRU リストの先頭に移動する（最近使用済みとしてマーク）
		m_lruList.splice(m_lruList.begin(), m_lruList, mapIt->second);
		return mapIt->second->asset;
	}

	/// @brief 指定キーのアセットが存在するか判定する（LRU 順序は変更しない）
	/// @param key キャッシュキー
	/// @return 存在すれば true
	[[nodiscard]] bool contains(const std::string& key) const
	{
		const std::shared_lock lock(m_mutex);
		return m_lookup.find(key) != m_lookup.end();
	}

	/// @brief 指定キーのアセットを削除する
	/// @param key キャッシュキー
	/// @return 削除に成功した場合 true
	bool erase(const std::string& key)
	{
		const std::unique_lock lock(m_mutex);

		auto mapIt = m_lookup.find(key);
		if (mapIt == m_lookup.end())
		{
			return false;
		}

		m_currentBytes -= mapIt->second->sizeBytes;
		m_lruList.erase(mapIt->second);
		m_lookup.erase(mapIt);
		return true;
	}

	/// @brief キャッシュを全消去する
	void clear()
	{
		const std::unique_lock lock(m_mutex);
		m_lruList.clear();
		m_lookup.clear();
		m_currentBytes = 0;
	}

	/// @brief メモリバジェットを変更する
	/// @param bytes 新しいバジェット（バイト単位、0 = 無制限）
	void setBudget(std::size_t bytes)
	{
		const std::unique_lock lock(m_mutex);
		m_budgetBytes = bytes;
		if (m_budgetBytes > 0)
		{
			evictUntilFits(0);
		}
	}

	/// @brief キャッシュ統計情報を取得する
	/// @return 統計情報のスナップショット
	[[nodiscard]] CacheStatistics statistics() const
	{
		const std::shared_lock lock(m_mutex);
		CacheStatistics result = m_stats;
		result.currentEntries = m_lookup.size();
		result.currentBytes = m_currentBytes;
		result.budgetBytes = m_budgetBytes;
		return result;
	}

	/// @brief キャッシュ内のエントリ数を取得する
	/// @return エントリ数
	[[nodiscard]] std::size_t size() const
	{
		const std::shared_lock lock(m_mutex);
		return m_lookup.size();
	}

	/// @brief 現在の使用メモリを取得する
	/// @return バイト数
	[[nodiscard]] std::size_t currentBytes() const
	{
		const std::shared_lock lock(m_mutex);
		return m_currentBytes;
	}

	/// @brief メモリバジェットを取得する
	/// @return バイト数（0 = 無制限）
	[[nodiscard]] std::size_t budgetBytes() const noexcept
	{
		return m_budgetBytes;
	}

private:
	/// @brief キャッシュエントリ
	struct CacheEntry
	{
		std::string key;              ///< キャッシュキー
		std::shared_ptr<T> asset;     ///< アセット本体
		std::size_t sizeBytes = 0;    ///< 推定サイズ（バイト）
	};

	/// @brief アセットサイズを推定する
	/// @param asset アセット参照
	/// @return 推定バイトサイズ
	[[nodiscard]] std::size_t estimateSize(const T& asset) const
	{
		if (m_sizeEstimator)
		{
			return m_sizeEstimator(asset);
		}
		return sizeof(T);
	}

	/// @brief 指定サイズが収まるまで LRU 末尾からエビクションする
	/// @param requiredBytes 追加で必要なバイト数
	void evictUntilFits(std::size_t requiredBytes)
	{
		if (m_budgetBytes == 0)
		{
			return;  ///< 無制限モード
		}

		while (!m_lruList.empty() && (m_currentBytes + requiredBytes > m_budgetBytes))
		{
			auto& victim = m_lruList.back();
			m_currentBytes -= victim.sizeBytes;
			m_lookup.erase(victim.key);
			m_lruList.pop_back();
			++m_stats.evictions;
		}
	}

	mutable std::shared_mutex m_mutex;           ///< リーダーライターロック

	using LruList = std::list<CacheEntry>;
	LruList m_lruList;                                              ///< LRU リスト（先頭が最新）
	std::unordered_map<std::string, typename LruList::iterator> m_lookup; ///< キー → リスト位置

	std::size_t m_budgetBytes = 0;               ///< メモリバジェット
	std::size_t m_currentBytes = 0;              ///< 現在の使用バイト数
	CacheStatistics m_stats;                     ///< 統計情報

	std::function<std::size_t(const T&)> m_sizeEstimator; ///< サイズ推定関数
};

} // namespace mitiru::resource
