#pragma once

/// @file AssetHandle.hpp
/// @brief 参照カウント付きアセットハンドル
/// @details shared_ptr ベースのアセットハンドル。
///          アセットの有効性確認とIDによる識別を提供する。
///          同一 id から発行された AssetHandle は内部スロットを共有するため、
///          ホットリロードでスロットの中身を差し替えると全コピーに同時に反映される。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mitiru::resource
{

template <typename T>
class AssetHandle;

/// @brief アセットのライフサイクル状態
/// @details AssetManager の同期 load / AsyncAssetLoader / StreamingManager / hot reload
///          いずれの経路でも、この状態遷移を経由してから onChanged が呼ばれる。
enum class AssetState : std::uint8_t
{
	Unloaded,   ///< 未ロード
	Loading,    ///< 初回ロード中
	Ready,      ///< ロード済み・使用可能
	Failed,     ///< ロード失敗
	Reloading,  ///< ホットリロード中（差し替え前の内容がまだ有効）
};

namespace detail
{

/// @brief AssetHandle<T> の実体。同一 id の全コピーがこれを共有する。
/// @details ホットリロードは asset を差し替えて state を戻すだけで済み、
///          既存の AssetHandle を保持している側は再取得なしに新しい中身を見る。
template <typename T>
struct AssetSlot
{
	std::mutex mutex;
	std::shared_ptr<T> asset;
	AssetState state = AssetState::Unloaded;
	std::size_t nextToken = 1;
	std::vector<std::pair<std::size_t, std::function<void(const AssetHandle<T>&)>>> callbacks;
};

} // namespace detail

/// @brief 参照カウント付きアセットハンドル
/// @tparam T アセット型
/// @details shared_ptr で管理されたアセットへのハンドル。
///          IDで識別し、ロード状態を確認できる。
template <typename T>
class AssetHandle
{
public:
	/// @brief 状態変化コールバック型
	using ChangeCallback = std::function<void(const AssetHandle<T>&)>;

	/// @brief デフォルトコンストラクタ（未ロード状態）
	AssetHandle() = default;

	/// @brief コンストラクタ
	/// @param id アセットID
	/// @param asset アセットの shared_ptr
	AssetHandle(std::string id, std::shared_ptr<T> asset)
		: m_id(std::move(id))
		, m_slot(std::make_shared<detail::AssetSlot<T>>())
	{
		m_slot->asset = std::move(asset);
		m_slot->state = m_slot->asset ? AssetState::Ready : AssetState::Unloaded;
	}

	/// @brief 既存スロットを共有するハンドルを構築する
	/// @details AssetManager がキャッシュ済みスロットから同じ実体を指す
	///          ハンドルを発行するために使う。
	AssetHandle(std::string id, std::shared_ptr<detail::AssetSlot<T>> slot) noexcept
		: m_id(std::move(id))
		, m_slot(std::move(slot))
	{
	}

	/// @brief アセットへの生ポインタを取得する
	/// @return アセットへのポインタ（未ロードの場合は nullptr）
	[[nodiscard]] T* get() const noexcept
	{
		if (!m_slot) { return nullptr; }
		// setContent() 等がワーカースレッドから m_slot->mutex 下で asset を書き換えるため、
		// ここも同じ mutex を経由しないと shared_ptr 自体への同時読み書きが UB になる。
		const std::lock_guard lock(m_slot->mutex);
		return m_slot->asset.get();
	}

	/// @brief アロー演算子
	/// @return アセットへのポインタ
	[[nodiscard]] T* operator->() const noexcept
	{
		return get();
	}

	/// @brief 間接参照演算子
	/// @return アセットへの参照
	[[nodiscard]] T& operator*() const noexcept
	{
		return *get();
	}

	/// @brief アセットがロード済みか判定する
	/// @return ロード済みなら true
	[[nodiscard]] bool isLoaded() const noexcept
	{
		return get() != nullptr;
	}

	/// @brief bool変換（isLoaded()と同義）
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return isLoaded();
	}

	/// @brief アセットIDを取得する
	/// @return アセットID
	[[nodiscard]] const std::string& id() const noexcept
	{
		return m_id;
	}

	/// @brief 参照カウントを取得する
	/// @return shared_ptr の参照カウント
	[[nodiscard]] long useCount() const noexcept
	{
		if (!m_slot) { return 0; }
		const std::lock_guard lock(m_slot->mutex);
		return m_slot->asset.use_count();
	}

	/// @brief アセットをリセットする（未ロード状態に戻す）
	void reset() noexcept
	{
		m_slot.reset();
	}

	/// @brief ライフサイクル状態を取得する
	/// @return 現在の AssetState
	[[nodiscard]] AssetState state() const noexcept
	{
		if (!m_slot)
		{
			return AssetState::Unloaded;
		}
		const std::lock_guard lock(m_slot->mutex);
		return m_slot->state;
	}

	/// @brief 状態変化（ロード完了・失敗・ホットリロード）を購読する
	/// @param callback 変化時に呼ばれるコールバック
	/// @return 購読解除トークン（0 = スロット未確立のため購読失敗）
	/// @note 呼び出しは AssetManager::pump() からのみ行われる（ワーカースレッド直呼びなし）。
	[[nodiscard]] std::size_t onChanged(ChangeCallback callback)
	{
		if (!m_slot || !callback)
		{
			return 0;
		}
		const std::lock_guard lock(m_slot->mutex);
		const auto token = m_slot->nextToken++;
		m_slot->callbacks.emplace_back(token, std::move(callback));
		return token;
	}

	/// @brief onChanged の購読を解除する
	/// @param token onChanged が返したトークン
	void offChanged(std::size_t token)
	{
		if (!m_slot || token == 0)
		{
			return;
		}
		const std::lock_guard lock(m_slot->mutex);
		auto& callbacks = m_slot->callbacks;
		callbacks.erase(
			std::remove_if(callbacks.begin(), callbacks.end(),
				[token](const auto& entry) { return entry.first == token; }),
			callbacks.end());
	}

	/// @brief 内部スロットを取得する（AssetManager 連携用）
	/// @return スロットの shared_ptr（未確立なら nullptr）
	[[nodiscard]] const std::shared_ptr<detail::AssetSlot<T>>& slot() const noexcept
	{
		return m_slot;
	}

	/// @brief 既存スロットを共有するハンドルを構築する（AssetManager 用）
	/// @details コンストラクタでは shared_ptr<T> と shared_ptr<AssetSlot<T>> の
	///          どちらも nullptr を受け付けてオーバーロードが曖昧になるため、
	///          スロット共有側はこの静的関数からのみ構築する。
	[[nodiscard]] static AssetHandle<T> fromSlot(std::string id, std::shared_ptr<detail::AssetSlot<T>> slot) noexcept
	{
		AssetHandle<T> handle;
		handle.m_id = std::move(id);
		handle.m_slot = std::move(slot);
		return handle;
	}

private:
	std::string m_id;                                  ///< アセットID
	std::shared_ptr<detail::AssetSlot<T>> m_slot;      ///< 共有スロット（実体）
};

} // namespace mitiru::resource
