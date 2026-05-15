#pragma once

/// @file SystemRunner.hpp
/// @brief ECSシステム実行管理
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
};

/// @brief システムごとのプロファイル情報
struct SystemProfile
{
	std::string name;          ///< システム名
	double lastUpdateMs{0.0};  ///< 最後の更新にかかった時間（ミリ秒）
	bool enabled{true};        ///< 有効フラグ
};

/// @brief 優先度ベースのシステム実行管理
class SystemRunner
{
public:
	/// @brief システムを追加する
	/// @param system システム（所有権を移譲）
	/// @param priority 優先度（小さいほど先に実行）
	void addSystem(std::unique_ptr<ISystem> system, int32_t priority = 0)
	{
		if (!system) return;

		SystemEntry entry;
		entry.name = system->name();
		entry.system = std::move(system);
		entry.priority = priority;
		entry.enabled = true;
		entry.lastUpdateMs = 0.0;

		m_systems.push_back(std::move(entry));

		// 優先度でソート（安定ソートで追加順を維持）
		std::stable_sort(m_systems.begin(), m_systems.end(),
			[](const SystemEntry& a, const SystemEntry& b)
			{
				return a.priority < b.priority;
			}
		);
	}

	/// @brief 名前でシステムを削除する
	/// @param name システム名
	/// @return 削除成功ならtrue
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
	/// @return システムが見つかればtrue
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
	/// @return 有効ならtrue（見つからなければfalse）
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
			result.push_back({entry.name, entry.lastUpdateMs, entry.enabled});
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
		int32_t priority{0};                ///< 優先度
		bool enabled{true};                 ///< 有効フラグ
		double lastUpdateMs{0.0};           ///< 最後の更新時間（ms）
	};

	std::vector<SystemEntry> m_systems;  ///< システムリスト（優先度順）
};

} // namespace mitiru::scene
