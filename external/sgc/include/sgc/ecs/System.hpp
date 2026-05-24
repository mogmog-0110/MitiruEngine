#pragma once

/// @file System.hpp
/// @brief ECSシステムスケジューラ
///
/// Systemコンセプトに基づくシステムの登録・実行を管理する。
/// World に対して毎フレーム update() を呼ぶシンプルな実行モデル。
///
/// @code
/// struct MovementSystem {
///     void update(sgc::ecs::World& world, float dt) {
///         world.forEach<Position, Velocity>([dt](Position& pos, Velocity& vel) {
///             pos.x += vel.dx * dt;
///         });
///     }
/// };
///
/// sgc::ecs::SystemScheduler scheduler;
/// scheduler.addSystem(MovementSystem{});
/// scheduler.update(world, deltaTime);
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "sgc/ecs/SystemTraits.hpp"

namespace sgc::ecs
{

// 前方宣言
class World;

/// @brief システム実行フェーズ
///
/// システムの実行順序を制御する。フェーズ順（小→大）で実行される。
enum class Phase : std::int32_t
{
	Update = 0,       ///< 通常更新
	FixedUpdate = 1,  ///< 固定フレーム更新（物理など）
	LateUpdate = 2,   ///< 遅延更新（カメラ追従など）
	Render = 3        ///< 描画
};

/// @brief システム優先度（同フェーズ内で小さいほど先に実行）
using SystemPriority = std::int32_t;

/// @brief Systemコンセプト — update(World&, float)を持つ型
template <typename T>
concept System = requires(T sys, World& world, float dt) {
	{ sys.update(world, dt) };
};

/// @brief システムスケジューラ
///
/// 登録されたシステムを順番に実行する。
/// 登録順序が実行順序となる。
class SystemScheduler
{
public:
	/// @brief システムを登録する
	/// @tparam S System コンセプトを満たす型
	/// @param system システムインスタンス
	/// @param phase 実行フェーズ（デフォルト: Update）
	/// @param priority 優先度（デフォルト: 0、小さいほど先に実行）
	template <System S>
	void addSystem(S system, Phase phase = Phase::Update, SystemPriority priority = 0)
	{
		// requires_components が定義されている場合、TypeListであることを検証する
		if constexpr (HasRequiredComponents<S>)
		{
			static_assert(
				IsTypeList<typename S::requires_components>,
				"requires_components must be a sgc::TypeList<...>");
		}

		m_systems.push_back(
			std::make_unique<SystemWrapper<S>>(std::move(system), phase, priority));
		m_sorted = false;
	}

	/// @brief 全システムをフェーズ→優先度順で実行する
	/// @param world ECSワールド
	/// @param deltaTime デルタタイム
	void update(World& world, float deltaTime)
	{
		sortIfNeeded();
		for (auto& sys : m_systems)
		{
			sys->update(world, deltaTime);
		}
	}

	/// @brief 指定フェーズのシステムのみ実行する
	/// @param phase 実行するフェーズ
	/// @param world ECSワールド
	/// @param deltaTime デルタタイム
	void updatePhase(Phase phase, World& world, float deltaTime)
	{
		sortIfNeeded();
		for (auto& sys : m_systems)
		{
			if (sys->phase() == phase)
			{
				sys->update(world, deltaTime);
			}
		}
	}

	/// @brief 指定インデックスのシステムを削除する
	/// @param index 削除するインデックス
	void removeSystem(std::size_t index)
	{
		if (index < m_systems.size())
		{
			m_systems.erase(m_systems.begin()
				+ static_cast<std::ptrdiff_t>(index));
		}
	}

	/// @brief 登録されているシステム数
	/// @return システム数
	[[nodiscard]] std::size_t systemCount() const noexcept
	{
		return m_systems.size();
	}

	/// @brief 全システムを削除する
	void clear()
	{
		m_systems.clear();
	}

private:
	/// @brief システム型消去インターフェース
	struct ISystem
	{
		virtual ~ISystem() = default;
		virtual void update(World& world, float dt) = 0;

		/// @brief このシステムの実行フェーズ
		[[nodiscard]] virtual Phase phase() const noexcept = 0;

		/// @brief このシステムの優先度
		[[nodiscard]] virtual SystemPriority priority() const noexcept = 0;
	};

	/// @brief 型付きシステムラッパー
	template <typename S>
	struct SystemWrapper : ISystem
	{
		S m_system;
		Phase m_phase;
		SystemPriority m_priority;

		SystemWrapper(S sys, Phase phase, SystemPriority priority)
			: m_system(std::move(sys))
			, m_phase(phase)
			, m_priority(priority)
		{
		}

		void update(World& world, float dt) override
		{
			m_system.update(world, dt);
		}

		[[nodiscard]] Phase phase() const noexcept override { return m_phase; }
		[[nodiscard]] SystemPriority priority() const noexcept override { return m_priority; }
	};

	/// @brief フェーズ→優先度順でソートする（必要な場合のみ）
	void sortIfNeeded()
	{
		if (!m_sorted)
		{
			std::stable_sort(m_systems.begin(), m_systems.end(),
				[](const auto& a, const auto& b)
				{
					if (a->phase() != b->phase())
					{
						return static_cast<std::int32_t>(a->phase())
							 < static_cast<std::int32_t>(b->phase());
					}
					return a->priority() < b->priority();
				});
			m_sorted = true;
		}
	}

	std::vector<std::unique_ptr<ISystem>> m_systems;  ///< 登録システム
	bool m_sorted{true};                              ///< ソート済みフラグ
};

} // namespace sgc::ecs
