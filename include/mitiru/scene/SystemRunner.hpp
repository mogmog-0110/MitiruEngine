#pragma once

/// @file SystemRunner.hpp
/// @brief ECS システム実行管理
///
/// 優先度ベースのシステム順序制御と、個別システムの有効/無効切替、
/// プロファイリング用のタイミング計測を提供する。
///
/// @code
/// struct MovementSystem : mitiru::scene::ISystem
/// {
///     std::string name() const override { return "Movement"; }
///     void update(mitiru::scene::GameWorld& world, float dt) override
///     {
///         world.forEach<TransformComponent>([dt](auto id, auto& t) {
///             t.position.y += 1.0f * dt;
///         });
///     }
/// };
///
/// mitiru::scene::SystemRunner runner;
/// runner.addSystem(std::make_unique<MovementSystem>(), 0);
/// runner.updateAll(world, 1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mitiru/debug/WarnOnce.hpp"
#include "mitiru/scene/UpdatePhase.hpp"

namespace mitiru::scene
{

// 前方宣言
class GameWorld;

/// @brief システムインターフェース
class ISystem
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ISystem() = default;

	/// @brief システム名を返す
	/// @return システムの名前
	[[nodiscard]] virtual std::string name() const = 0;

	/// @brief システムを更新する
	/// @param world ゲームワールド
	/// @param dt デルタタイム（秒）
	virtual void update(GameWorld& world, float dt) = 0;

	/// @brief addSystem で phase を省略したときに使うフェーズ
	/// @details 既存呼び出し (phase 省略) の挙動を変えないためのデフォルトは Sim。
	[[nodiscard]] virtual UpdatePhase defaultPhase() const noexcept { return UpdatePhase::Sim; }
};

/// @brief システムごとのプロファイル情報
struct SystemProfile
{
	std::string name;          ///< システム名
	UpdatePhase phase{UpdatePhase::Sim};  ///< 更新フェーズ
	double lastUpdateMs{0.0};  ///< 最後の更新にかかった時間（ミリ秒）
	bool enabled{true};        ///< 有効フラグ
};

/// @brief 優先度ベースのシステム実行管理
class SystemRunner
{
public:
	/// @brief システムを追加する（phase は system->defaultPhase() に従う）
	/// @param system システム（所有権を移譲）
	/// @param priority 優先度（同一 phase 内で小さいほど先に実行）
	void addSystem(std::unique_ptr<ISystem> system, int32_t priority = 0)
	{
		if (!system) return;
		const UpdatePhase phase = system->defaultPhase();
		addSystem(std::move(system), phase, priority);
	}

	/// @brief システムを phase 指定で追加する
	/// @param system システム（所有権を移譲）
	/// @param phase 更新フェーズ（小さい値が先）
	/// @param priority 優先度（同一 phase 内で小さいほど先に実行）
	void addSystem(std::unique_ptr<ISystem> system, UpdatePhase phase, int32_t priority = 0)
	{
		addSystem(std::move(system), phase, priority, nullptr);
	}

	/// @brief システムを phase 指定 + 個別依存 (`after`) 付きで追加する (§3-1)。
	/// @param after 「このシステムより後に実行してほしい」システム名。空/nullptr なら依存なし。
	/// @details priority の数字だけでは「A は B の後」という関係が B の priority 変更で気づかないうちに
	/// 成り立たなくなる。`after` は宣言であって自動でソート順を変えるものではない。実行順を自動で直すと
	/// 「なぜこの順で走っているか」が priority から読めなくなり `mitiru why` の読み手 (AI) に不利
	/// になるため、満たされていなければ `addSystem` の時点で `warnOnce` するだけに留める。
	void addSystem(std::unique_ptr<ISystem> system, UpdatePhase phase, int32_t priority, const char* after)
	{
		if (!system) return;

		SystemEntry entry;
		entry.name = system->name();
		entry.system = std::move(system);
		entry.phase = phase;
		entry.priority = priority;
		entry.after = (after != nullptr) ? after : "";
		entry.enabled = true;
		entry.lastUpdateMs = 0.0;

		m_systems.push_back(std::move(entry));
		resortAndValidate();
	}

	/// @brief 名前でシステムを削除する
	/// @param name システム名
	/// @return 削除成功なら true
	bool removeSystem(const std::string& name)
	{
		auto it = std::find_if(m_systems.begin(), m_systems.end(),
			[&name](const SystemEntry& e) { return e.name == name; }
		);
		if (it == m_systems.end()) return false;
		m_systems.erase(it);
		return true;
	}

	/// @brief 全システムを優先度順に実行する
	/// @param world ゲームワールド
	/// @param dt デルタタイム（秒）
	void updateAll(GameWorld& world, float dt)
	{
		for (auto& entry : m_systems)
		{
			if (!entry.enabled) continue;

			const auto start = std::chrono::high_resolution_clock::now();
			entry.system->update(world, dt);
			const auto end = std::chrono::high_resolution_clock::now();

			const std::chrono::duration<double, std::milli> elapsed = end - start;
			entry.lastUpdateMs = elapsed.count();
		}
	}

	/// @brief システムの有効/無効を切り替える
	/// @param name システム名
	/// @param enabled 有効にするかどうか
	/// @return システムが見つかれば true
	bool setEnabled(const std::string& name, bool enabled)
	{
		auto it = std::find_if(m_systems.begin(), m_systems.end(),
			[&name](const SystemEntry& e) { return e.name == name; }
		);
		if (it == m_systems.end()) return false;
		it->enabled = enabled;
		return true;
	}

	/// @brief システムが有効かどうか
	/// @param name システム名
	/// @return 有効なら true（見つからなければ false）
	[[nodiscard]] bool isEnabled(const std::string& name) const noexcept
	{
		auto it = std::find_if(m_systems.begin(), m_systems.end(),
			[&name](const SystemEntry& e) { return e.name == name; }
		);
		if (it == m_systems.end()) return false;
		return it->enabled;
	}

	/// @brief 全システムのプロファイル情報を取得する
	/// @return プロファイル情報のリスト
	[[nodiscard]] std::vector<SystemProfile> profiles() const
	{
		std::vector<SystemProfile> result;
		result.reserve(m_systems.size());
		for (const auto& entry : m_systems)
		{
			result.push_back({entry.name, entry.phase, entry.lastUpdateMs, entry.enabled});
		}
		return result;
	}

	/// @brief 登録システム数を返す
	[[nodiscard]] size_t systemCount() const noexcept { return m_systems.size(); }

private:
	/// @brief システム登録エントリ
	struct SystemEntry
	{
		std::string name;                   ///< システム名
		std::unique_ptr<ISystem> system;    ///< システム本体
		UpdatePhase phase{UpdatePhase::Sim};///< 更新フェーズ
		int32_t priority{0};                ///< 優先度
		std::string after;                  ///< §3-1: このシステムより後に実行してほしい相手の名前（空なら無し）
		bool enabled{true};                 ///< 有効フラグ
		double lastUpdateMs{0.0};           ///< 最後の更新時間（ms）
	};

	/// @brief (phase, priority) で安定ソートしたのち `after` 制約を検証する。
	void resortAndValidate()
	{
		std::stable_sort(m_systems.begin(), m_systems.end(),
			[](const SystemEntry& a, const SystemEntry& b)
			{
				if (a.phase != b.phase) return a.phase < b.phase;
				return a.priority < b.priority;
			}
		);
		validateAfterConstraints();
	}

	/// @brief `after` で名指しした相手が実際に自分より後ろに来ている（=満たされていない）場合に
	/// 1 回だけ警告する。名前が見つからない場合は何もしない（相手が未登録のことがあるため）。
	void validateAfterConstraints() const
	{
		for (std::size_t i = 0; i < m_systems.size(); ++i)
		{
			const SystemEntry& entry = m_systems[i];
			if (entry.after.empty()) continue;

			for (std::size_t j = 0; j < m_systems.size(); ++j)
			{
				if (j == i || m_systems[j].name != entry.after) continue;
				if (j > i)
				{
					debug::warnOnce(
						"systemrunner.after." + entry.name + "." + entry.after,
						"システム " + entry.name + " は " + entry.after + " の後に走る指定ですが、実際は先に走ります。"
						"addSystem の phase か priority を直してください (実行順は自動では直しません)。");
				}
				break;
			}
		}
	}

	std::vector<SystemEntry> m_systems;  ///< システムリスト（優先度順）
};

} // namespace mitiru::scene
