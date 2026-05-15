#pragma once

/// @file NetworkSync.hpp
/// @brief ゲーム状態同期フレームワーク
/// @details SyncableComponentインターフェースを通じて、
///          エンティティの差分同期・補間・補外を提供する。
///          ホスト権限モデルをデフォルトとし、StateSync上に構築する。
///
/// @code
/// using namespace mitiru::network;
///
/// // 同期対象のコンポーネントを定義
/// struct PlayerTransform : SyncableComponent {
///     float x = 0, y = 0, rotation = 0;
///     nlohmann::json serialize() const override { ... }
///     void deserialize(const nlohmann::json& j) override { ... }
///     std::uint32_t syncId() const override { return m_id; }
/// };
///
/// // SyncManagerで管理
/// SyncManager mgr;
/// mgr.registerComponent(transform);
/// auto delta = mgr.collectDirtyStates();
/// mgr.applyRemoteStates(remoteJson, currentTime);
/// mgr.interpolate(renderTime);
/// @endcode

#include <mitiru/network/NetworkTypes.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::network
{

/// @brief 権限モデル
enum class AuthorityMode : std::uint8_t
{
	HostAuthoritative = 0,  ///< ホストが全状態の権限を持つ
	OwnerAuthoritative = 1, ///< 各オーナーが自身のエンティティの権限を持つ
	Shared = 2,             ///< 特定プロパティ毎にオーナーが異なる
};

/// @brief 同期可能コンポーネントインターフェース
/// @details ネットワーク同期対象のコンポーネントはこのインターフェースを実装する。
class SyncableComponent
{
public:
	virtual ~SyncableComponent() = default;

	/// @brief ネットワーク上の一意IDを返す
	[[nodiscard]] virtual std::uint32_t syncId() const = 0;

	/// @brief 現在の状態をJSONにシリアライズする
	[[nodiscard]] virtual nlohmann::json serialize() const = 0;

	/// @brief JSONから状態をデシリアライズする
	virtual void deserialize(const nlohmann::json& j) = 0;

	/// @brief 状態が変更されたかを返す
	[[nodiscard]] virtual bool isDirty() const { return m_dirty; }

	/// @brief ダーティフラグをクリアする
	virtual void clearDirty() { m_dirty = false; }

	/// @brief ダーティフラグを設定する
	void markDirty() { m_dirty = true; }

	/// @brief オーナーのピアIDを取得する
	[[nodiscard]] ConnectionId ownerId() const noexcept { return m_ownerId; }

	/// @brief オーナーのピアIDを設定する
	void setOwnerId(ConnectionId id) noexcept { m_ownerId = id; }

protected:
	bool m_dirty = false;
	ConnectionId m_ownerId = INVALID_CONNECTION;
};

/// @brief 状態差分データ
/// @details 変更があったコンポーネントのみを含む増分更新。
struct StateDelta
{
	std::uint64_t version = 0;           ///< 差分バージョン
	std::uint64_t timestampMs = 0;       ///< 生成タイムスタンプ
	nlohmann::json states;               ///< syncId -> 状態JSON のマップ

	/// @brief JSON にシリアライズする
	[[nodiscard]] nlohmann::json toJson() const
	{
		return {
			{"version", version},
			{"timestamp", timestampMs},
			{"states", states}
		};
	}

	/// @brief JSON からデシリアライズする
	[[nodiscard]] static std::optional<StateDelta> fromJson(const nlohmann::json& j)
	{
		if (!j.contains("version") || !j.contains("states"))
		{
			return std::nullopt;
		}

		StateDelta delta;
		delta.version = j["version"].get<std::uint64_t>();
		delta.timestampMs = j.value("timestamp", std::uint64_t{0});
		delta.states = j["states"];
		return delta;
	}
};

/// @brief 補間用スナップショット
/// @details 受信した状態を時間情報付きで保持し、補間に使用する。
struct SyncSnapshot
{
	std::uint64_t timestampMs = 0;       ///< タイムスタンプ
	nlohmann::json state;                ///< 状態データ
};

/// @brief 同期マネージャー
/// @details SyncableComponent群の差分収集・リモート状態の適用・
///          補間/補外を管理する。
class SyncManager
{
public:
	/// @brief 補間関数型
	/// @details (from状態, to状態, alpha [0..1]) -> 補間結果
	using InterpolateFunc = std::function<nlohmann::json(
		const nlohmann::json&, const nlohmann::json&, float)>;

	/// @brief コンポーネントを登録する
	/// @param component 同期対象コンポーネント（非所有ポインタ）
	void registerComponent(SyncableComponent* component)
	{
		if (component == nullptr) return;
		m_components[component->syncId()] = component;
	}

	/// @brief コンポーネントの登録を解除する
	/// @param syncId 同期ID
	void unregisterComponent(std::uint32_t syncId)
	{
		m_components.erase(syncId);
		m_snapshots.erase(syncId);
	}

	/// @brief 権限モデルを設定する
	void setAuthorityMode(AuthorityMode mode) noexcept
	{
		m_authorityMode = mode;
	}

	/// @brief ローカルピアIDを設定する
	void setLocalPeerId(ConnectionId id) noexcept
	{
		m_localPeerId = id;
	}

	/// @brief 指定コンポーネントに権限があるかを判定する
	/// @param syncId 同期ID
	/// @return ローカルピアが権限を持つなら true
	[[nodiscard]] bool hasAuthority(std::uint32_t syncId) const
	{
		switch (m_authorityMode)
		{
		case AuthorityMode::HostAuthoritative:
			return m_isHost;
		case AuthorityMode::OwnerAuthoritative:
		case AuthorityMode::Shared:
		{
			auto it = m_components.find(syncId);
			if (it == m_components.end()) return false;
			return it->second->ownerId() == m_localPeerId;
		}
		}
		return false;
	}

	/// @brief ホストフラグを設定する
	void setIsHost(bool isHost) noexcept { m_isHost = isHost; }

	/// @brief ダーティなコンポーネントの差分を収集する
	/// @return 差分データ
	[[nodiscard]] StateDelta collectDirtyStates()
	{
		StateDelta delta;
		delta.version = ++m_version;
		delta.timestampMs = currentTimeMs();

		for (auto& [syncId, comp] : m_components)
		{
			if (!comp->isDirty()) continue;
			if (!hasAuthority(syncId)) continue;

			delta.states[std::to_string(syncId)] = comp->serialize();
			comp->clearDirty();
		}

		return delta;
	}

	/// @brief リモートから受信した差分を適用する
	/// @param delta 差分データ
	void applyRemoteDelta(const StateDelta& delta)
	{
		for (auto& [key, stateJson] : delta.states.items())
		{
			const std::uint32_t syncId = std::stoul(key);

			// 権限があるコンポーネントはリモートからの更新を無視
			if (hasAuthority(syncId)) continue;

			// スナップショットを保存（補間用）
			auto& snapshots = m_snapshots[syncId];
			snapshots.push_back({delta.timestampMs, stateJson});

			// 履歴が溢れたら古いものを削除
			while (snapshots.size() > m_maxSnapshotHistory)
			{
				snapshots.erase(snapshots.begin());
			}

			// コンポーネントに直接適用
			auto compIt = m_components.find(syncId);
			if (compIt != m_components.end())
			{
				compIt->second->deserialize(stateJson);
			}
		}
	}

	/// @brief 補間を実行する
	/// @param renderTimeMs 描画時刻（ミリ秒、通常は現在時刻 - 補間遅延）
	/// @details 登録済みの InterpolateFunc を使って、2つのスナップショット間を
	///          補間し、コンポーネントに適用する。
	void interpolate(std::uint64_t renderTimeMs)
	{
		if (!m_interpolateFunc) return;

		for (auto& [syncId, snapshots] : m_snapshots)
		{
			if (hasAuthority(syncId)) continue;
			if (snapshots.size() < 2) continue;

			// renderTimeMs を挟む2つのスナップショットを探す
			const SyncSnapshot* before = nullptr;
			const SyncSnapshot* after = nullptr;

			for (std::size_t i = 0; i + 1 < snapshots.size(); ++i)
			{
				if (snapshots[i].timestampMs <= renderTimeMs &&
				    snapshots[i + 1].timestampMs >= renderTimeMs)
				{
					before = &snapshots[i];
					after = &snapshots[i + 1];
					break;
				}
			}

			if (before == nullptr || after == nullptr) continue;

			const float duration = static_cast<float>(
				after->timestampMs - before->timestampMs);
			if (duration <= 0.0f) continue;

			const float alpha = static_cast<float>(
				renderTimeMs - before->timestampMs) / duration;
			const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);

			const auto interpolated = m_interpolateFunc(
				before->state, after->state, clampedAlpha);

			auto compIt = m_components.find(syncId);
			if (compIt != m_components.end())
			{
				compIt->second->deserialize(interpolated);
			}
		}
	}

	/// @brief 補外（extrapolation）を実行する
	/// @param currentTimeMs 現在時刻（ミリ秒）
	/// @param maxExtrapolateMs 最大補外時間（ミリ秒）
	/// @details 最新のスナップショットから経過時間分だけ状態を外挿する。
	///          InterpolateFuncを使い、alpha > 1.0 で補外する。
	void extrapolate(std::uint64_t currentTimeMs, std::uint64_t maxExtrapolateMs = 200)
	{
		if (!m_interpolateFunc) return;

		for (auto& [syncId, snapshots] : m_snapshots)
		{
			if (hasAuthority(syncId)) continue;
			if (snapshots.size() < 2) continue;

			const auto& prev = snapshots[snapshots.size() - 2];
			const auto& latest = snapshots[snapshots.size() - 1];

			if (currentTimeMs <= latest.timestampMs) continue;

			const std::uint64_t overdue = currentTimeMs - latest.timestampMs;
			if (overdue > maxExtrapolateMs) continue;

			const float duration = static_cast<float>(
				latest.timestampMs - prev.timestampMs);
			if (duration <= 0.0f) continue;

			const float alpha = 1.0f + static_cast<float>(overdue) / duration;

			const auto extrapolated = m_interpolateFunc(
				prev.state, latest.state, alpha);

			auto compIt = m_components.find(syncId);
			if (compIt != m_components.end())
			{
				compIt->second->deserialize(extrapolated);
			}
		}
	}

	/// @brief 補間関数を設定する
	void setInterpolateFunc(InterpolateFunc func)
	{
		m_interpolateFunc = std::move(func);
	}

	/// @brief 登録済みコンポーネント数を返す
	[[nodiscard]] std::size_t componentCount() const noexcept
	{
		return m_components.size();
	}

	/// @brief 全状態をフルスナップショットとして取得する
	/// @return 全コンポーネントの状態JSON
	[[nodiscard]] nlohmann::json fullSnapshot() const
	{
		nlohmann::json result;
		for (const auto& [syncId, comp] : m_components)
		{
			result[std::to_string(syncId)] = comp->serialize();
		}
		return result;
	}

	/// @brief フルスナップショットから状態を復元する
	/// @param snapshot スナップショットJSON
	void applyFullSnapshot(const nlohmann::json& snapshot)
	{
		for (auto& [key, stateJson] : snapshot.items())
		{
			const std::uint32_t syncId = std::stoul(key);
			auto it = m_components.find(syncId);
			if (it != m_components.end())
			{
				it->second->deserialize(stateJson);
			}
		}
	}

	/// @brief スナップショット履歴をクリアする
	void clearSnapshots() { m_snapshots.clear(); }

private:
	/// @brief 現在時刻をミリ秒で取得する
	[[nodiscard]] static std::uint64_t currentTimeMs()
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}

	std::unordered_map<std::uint32_t, SyncableComponent*> m_components;
	std::unordered_map<std::uint32_t, std::vector<SyncSnapshot>> m_snapshots;

	AuthorityMode m_authorityMode = AuthorityMode::HostAuthoritative;
	ConnectionId m_localPeerId = INVALID_CONNECTION;
	bool m_isHost = false;

	std::uint64_t m_version = 0;
	InterpolateFunc m_interpolateFunc;
	std::size_t m_maxSnapshotHistory = 30;
};

/// @brief 数値プロパティの線形補間ヘルパー
/// @details SyncManager::setInterpolateFunc() に渡す汎用的な補間関数。
///          JSONオブジェクトの全数値フィールドを線形補間する。
[[nodiscard]] inline nlohmann::json lerpJsonNumeric(
	const nlohmann::json& a, const nlohmann::json& b, float alpha)
{
	if (!a.is_object() || !b.is_object()) return b;

	nlohmann::json result = a;
	for (auto& [key, bVal] : b.items())
	{
		if (bVal.is_number() && a.contains(key) && a[key].is_number())
		{
			const float va = a[key].get<float>();
			const float vb = bVal.get<float>();
			result[key] = va + (vb - va) * alpha;
		}
		else
		{
			result[key] = bVal;
		}
	}
	return result;
}

} // namespace mitiru::network
