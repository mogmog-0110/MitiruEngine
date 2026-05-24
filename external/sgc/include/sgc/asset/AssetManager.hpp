#pragma once

/// @file AssetManager.hpp
/// @brief テンプレートベースの中央アセットレジストリ
///
/// 型安全なアセット管理。参照カウント付きshared_ptrで保持し、
/// AssetHandle<T>による軽量参照で安全にアクセスする。
///
/// @code
/// struct Texture { int width; int height; };
///
/// // カスタムローダー
/// struct TextureAssetLoader : sgc::asset::IAssetLoader<Texture>
/// {
///     sgc::Result<std::shared_ptr<Texture>> load(const std::string& path) override
///     {
///         return std::make_shared<Texture>(Texture{256, 256});
///     }
/// };
///
/// sgc::asset::AssetManager<Texture> manager;
/// manager.setLoader(std::make_unique<TextureAssetLoader>());
/// auto handle = manager.load("player.png");
/// auto* tex = manager.get(handle);
/// @endcode

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/asset/AssetHandle.hpp"
#include "sgc/types/Result.hpp"

namespace sgc::asset
{

/// @brief アセットの状態
enum class AssetState
{
	Unloaded,  ///< 未ロード
	Loading,   ///< ロード中
	Loaded,    ///< ロード完了
	Error      ///< ロード失敗
};

/// @brief アセットローダーインターフェース
/// @tparam T アセットの型
///
/// カスタムローダーはこのインターフェースを実装する。
template <typename T>
class IAssetLoader
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAssetLoader() = default;

	/// @brief 指定パスからアセットをロードする
	/// @param path ファイルパス
	/// @return ロード結果（成功時はshared_ptr、失敗時はエラー）
	[[nodiscard]] virtual Result<std::shared_ptr<T>> load(const std::string& path) = 0;
};

/// @brief テンプレートベースの中央アセットレジストリ
/// @tparam T 管理するアセットの型
///
/// shared_ptrベースの参照カウント管理。
/// AssetHandle<T>による軽量参照で安全にアクセスする。
/// shared_mutexによるスレッドセーフなアクセスを提供する。
template <typename T>
class AssetManager
{
public:
	/// @brief アセットスロット（内部データ）
	struct Slot
	{
		std::string path;                   ///< アセットパス
		std::shared_ptr<T> asset;           ///< アセット実体
		AssetState state{AssetState::Unloaded};  ///< 状態
		typename AssetHandle<T>::GenerationType generation{1};  ///< 世代番号
	};

	/// @brief デフォルトコンストラクタ
	AssetManager() = default;

	/// @brief カスタムローダーを設定する
	/// @param loader ローダーのunique_ptr（所有権を移譲）
	void setLoader(std::unique_ptr<IAssetLoader<T>> loader)
	{
		std::unique_lock lock(m_mutex);
		m_loader = std::move(loader);
	}

	/// @brief アセットをロードしてハンドルを返す
	/// @param path アセットのファイルパス
	/// @return ロードされたアセットのハンドル（失敗時は無効ハンドル）
	///
	/// 同じパスのアセットが既にロード済みの場合は既存ハンドルを返す。
	[[nodiscard]] AssetHandle<T> load(const std::string& path)
	{
		// 既存エントリを検索（読み取りロック）
		{
			std::shared_lock lock(m_mutex);
			auto it = m_pathIndex.find(path);
			if (it != m_pathIndex.end())
			{
				const auto idx = it->second;
				if (m_slots[idx].state == AssetState::Loaded)
				{
					return AssetHandle<T>{idx, m_slots[idx].generation};
				}
			}
		}

		// 新規ロード（書き込みロック）
		std::unique_lock lock(m_mutex);

		// ダブルチェック
		auto it = m_pathIndex.find(path);
		if (it != m_pathIndex.end())
		{
			const auto idx = it->second;
			if (m_slots[idx].state == AssetState::Loaded)
			{
				return AssetHandle<T>{idx, m_slots[idx].generation};
			}
		}

		if (!m_loader)
		{
			return AssetHandle<T>::null();
		}

		// スロットを確保
		const auto idx = static_cast<typename AssetHandle<T>::IndexType>(m_slots.size());
		m_slots.push_back(Slot{path, nullptr, AssetState::Loading, 1});
		m_pathIndex[path] = idx;

		// ロード実行（ロック保持中だが、ヘッダーオンリーのため簡易実装）
		auto result = m_loader->load(path);
		if (result)
		{
			m_slots[idx].asset = std::move(result.value());
			m_slots[idx].state = AssetState::Loaded;
			return AssetHandle<T>{idx, m_slots[idx].generation};
		}

		m_slots[idx].state = AssetState::Error;
		return AssetHandle<T>::null();
	}

	/// @brief ハンドルからアセットを取得する
	/// @param handle アセットハンドル
	/// @return アセットポインタ（無効な場合はnullptr）
	[[nodiscard]] T* get(const AssetHandle<T>& handle) const
	{
		std::shared_lock lock(m_mutex);
		if (!handle.isValid()) return nullptr;
		if (handle.index >= m_slots.size()) return nullptr;

		const auto& slot = m_slots[handle.index];
		if (slot.generation != handle.generation) return nullptr;
		if (slot.state != AssetState::Loaded) return nullptr;

		return slot.asset.get();
	}

	/// @brief ハンドルからshared_ptrを取得する
	/// @param handle アセットハンドル
	/// @return shared_ptr（無効な場合はnullptr）
	[[nodiscard]] std::shared_ptr<T> getShared(const AssetHandle<T>& handle) const
	{
		std::shared_lock lock(m_mutex);
		if (!handle.isValid()) return nullptr;
		if (handle.index >= m_slots.size()) return nullptr;

		const auto& slot = m_slots[handle.index];
		if (slot.generation != handle.generation) return nullptr;
		if (slot.state != AssetState::Loaded) return nullptr;

		return slot.asset;
	}

	/// @brief アセットの状態を取得する
	/// @param handle アセットハンドル
	/// @return アセット状態
	[[nodiscard]] AssetState state(const AssetHandle<T>& handle) const
	{
		std::shared_lock lock(m_mutex);
		if (!handle.isValid() || handle.index >= m_slots.size()) return AssetState::Unloaded;
		if (m_slots[handle.index].generation != handle.generation) return AssetState::Unloaded;
		return m_slots[handle.index].state;
	}

	/// @brief パスでアセットが存在するか判定する
	/// @param path アセットパス
	/// @return 存在すればtrue
	[[nodiscard]] bool has(const std::string& path) const
	{
		std::shared_lock lock(m_mutex);
		auto it = m_pathIndex.find(path);
		if (it == m_pathIndex.end()) return false;
		return m_slots[it->second].state == AssetState::Loaded;
	}

	/// @brief 指定パスのアセットをアンロードする
	/// @param path アセットパス
	void unload(const std::string& path)
	{
		std::unique_lock lock(m_mutex);
		auto it = m_pathIndex.find(path);
		if (it == m_pathIndex.end()) return;

		auto& slot = m_slots[it->second];
		slot.asset.reset();
		slot.state = AssetState::Unloaded;
		++slot.generation;  // 世代を進めて旧ハンドルを無効化
		m_pathIndex.erase(it);
	}

	/// @brief 全アセットをクリアする
	void clear()
	{
		std::unique_lock lock(m_mutex);
		m_slots.clear();
		m_pathIndex.clear();
	}

	/// @brief ロード済みアセット数を取得する
	/// @return ロード済みアセット数
	[[nodiscard]] std::size_t loadedCount() const
	{
		std::shared_lock lock(m_mutex);
		std::size_t count = 0;
		for (const auto& slot : m_slots)
		{
			if (slot.state == AssetState::Loaded) ++count;
		}
		return count;
	}

	/// @brief 全スロット数を取得する
	/// @return スロット数
	[[nodiscard]] std::size_t slotCount() const
	{
		std::shared_lock lock(m_mutex);
		return m_slots.size();
	}

private:
	std::vector<Slot> m_slots;                                    ///< アセットスロット配列
	std::unordered_map<std::string, typename AssetHandle<T>::IndexType> m_pathIndex;  ///< パス→インデックス
	std::unique_ptr<IAssetLoader<T>> m_loader;                    ///< ローダー
	mutable std::shared_mutex m_mutex;                            ///< 読み書きロック
};

} // namespace sgc::asset
