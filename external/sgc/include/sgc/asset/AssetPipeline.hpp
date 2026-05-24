#pragma once

/// @file AssetPipeline.hpp
/// @brief アセット読み込み・キャッシュパイプライン
///
/// 文字列ベースのアセットIDでリソースを管理し、
/// オンデマンド読み込みとキャッシュ機能を提供する。
///
/// @code
/// sgc::asset::AssetManifest manifest;
/// manifest.add(sgc::asset::AssetId{"player"}, "textures/player.png");
///
/// auto loader = [](const std::string& path) -> std::optional<std::string> {
///     return "loaded:" + path;  // 実際にはファイル読み込み
/// };
///
/// sgc::asset::AssetCache<std::string> cache(loader, manifest);
/// auto result = cache.get(sgc::asset::AssetId{"player"});
/// @endcode

#include <concepts>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::asset
{

/// @brief アセット識別子（文字列ベースの強い型付け）
struct AssetId
{
	std::string value;  ///< ID文字列

	[[nodiscard]] bool operator==(const AssetId&) const = default;
};

} // namespace sgc::asset

/// @brief AssetIdのハッシュ特殊化
template <>
struct std::hash<sgc::asset::AssetId>
{
	[[nodiscard]] size_t operator()(const sgc::asset::AssetId& id) const noexcept
	{
		return std::hash<std::string>{}(id.value);
	}
};

namespace sgc::asset
{

/// @brief アセットの状態
enum class AssetState : uint8_t
{
	NotLoaded, ///< 未読み込み
	Loading,   ///< 読み込み中
	Loaded,    ///< 読み込み完了
	Error,     ///< エラー
};

/// @brief アセットローダーのコンセプト
/// @tparam L ローダー型
/// @tparam T アセット型
template <typename L, typename T>
concept IAssetLoader = requires(L loader, const std::string& path)
{
	{ loader(path) } -> std::same_as<std::optional<T>>;
};

/// @brief アセットマニフェスト（IDとファイルパスの対応表）
class AssetManifest
{
public:
	/// @brief アセットを登録する
	/// @param id アセットID
	/// @param path ファイルパス
	void add(const AssetId& id, const std::string& path)
	{
		m_entries[id] = path;
	}

	/// @brief アセットを削除する
	/// @param id アセットID
	void remove(const AssetId& id)
	{
		m_entries.erase(id);
	}

	/// @brief ファイルパスを取得する
	/// @param id アセットID
	/// @return パス（未登録時はnullopt）
	[[nodiscard]] std::optional<std::string> getPath(const AssetId& id) const
	{
		const auto it = m_entries.find(id);
		if (it != m_entries.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief 登録済みか判定する
	/// @param id アセットID
	[[nodiscard]] bool contains(const AssetId& id) const
	{
		return m_entries.contains(id);
	}

	/// @brief 登録数を取得する
	[[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

	/// @brief 全エントリをクリアする
	void clear() { m_entries.clear(); }

	/// @brief 全エントリを取得する
	[[nodiscard]] const std::unordered_map<AssetId, std::string>& entries() const noexcept
	{
		return m_entries;
	}

private:
	std::unordered_map<AssetId, std::string> m_entries;
};

/// @brief アセットキャッシュ
///
/// ローダー関数とマニフェストを使ってアセットをオンデマンドで読み込み、キャッシュする。
///
/// @tparam T アセット型
template <typename T>
class AssetCache
{
public:
	/// @brief コンストラクタ
	/// @param loader ローダー関数
	/// @param manifest アセットマニフェスト
	AssetCache(std::function<std::optional<T>(const std::string&)> loader,
	           const AssetManifest& manifest)
		: m_loader(std::move(loader))
		, m_manifest(manifest)
	{
	}

	/// @brief アセットを取得する（未読み込みなら自動ロード）
	/// @param id アセットID
	/// @return アセットへのポインタ（失敗時はnullptr）
	[[nodiscard]] const T* get(const AssetId& id)
	{
		// キャッシュにあればそれを返す
		const auto cacheIt = m_cache.find(id);
		if (cacheIt != m_cache.end())
		{
			return &cacheIt->second;
		}

		// マニフェストからパスを取得してロード
		const auto path = m_manifest.getPath(id);
		if (!path.has_value())
		{
			m_states[id] = AssetState::Error;
			return nullptr;
		}

		m_states[id] = AssetState::Loading;
		auto result = m_loader(path.value());
		if (result.has_value())
		{
			m_cache[id] = std::move(result.value());
			m_states[id] = AssetState::Loaded;
			return &m_cache[id];
		}

		m_states[id] = AssetState::Error;
		return nullptr;
	}

	/// @brief アセットを事前読み込みする
	/// @param id アセットID
	/// @return 成功時true
	bool preload(const AssetId& id)
	{
		return get(id) != nullptr;
	}

	/// @brief アセットをアンロードする
	/// @param id アセットID
	void unload(const AssetId& id)
	{
		m_cache.erase(id);
		m_states[id] = AssetState::NotLoaded;
	}

	/// @brief 全アセットをクリアする
	void clear()
	{
		m_cache.clear();
		m_states.clear();
	}

	/// @brief アセットの状態を取得する
	/// @param id アセットID
	/// @return アセット状態
	[[nodiscard]] AssetState state(const AssetId& id) const
	{
		const auto it = m_states.find(id);
		if (it != m_states.end())
		{
			return it->second;
		}
		return AssetState::NotLoaded;
	}

	/// @brief キャッシュ内のアセット数を取得する
	[[nodiscard]] size_t cachedCount() const noexcept { return m_cache.size(); }

private:
	std::function<std::optional<T>(const std::string&)> m_loader;
	AssetManifest m_manifest;
	std::unordered_map<AssetId, T> m_cache;
	std::unordered_map<AssetId, AssetState> m_states;
};

} // namespace sgc::asset
