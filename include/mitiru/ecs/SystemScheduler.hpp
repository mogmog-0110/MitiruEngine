#pragma once

/// @file SystemScheduler.hpp
/// @brief 決定論的システム実行スケジューラー
/// @details 優先度付きでシステム（更新関数）を登録し、
///          決定論的な順序で毎フレーム実行する。

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::ecs
{

/// @brief システムエントリ
/// @details 名前・優先度・更新関数をまとめた構造体。
struct SystemEntry
{
	std::string name;                        ///< システム名
	int priority = 0;                        ///< 実行優先度（小さい値が先に実行）
	std::function<void(float)> updateFn;     ///< 更新関数（dt を受け取る）
};

/// @brief 決定論的システムスケジューラー
/// @details 優先度順にシステムを実行し、同一フレーム内の実行順序を保証する。
///
/// @code
/// mitiru::ecs::SystemScheduler scheduler;
/// scheduler.addSystem({"physics", 10, [](float dt) { updatePhysics(dt); }});
/// scheduler.addSystem({"render", 100, [](float dt) { render(dt); }});
/// scheduler.update(0.016f); // physics -> render の順で実行
/// @endcode
class SystemScheduler
{
public:
	/// @brief システムを追加する
	/// @param entry システムエントリ
	void addSystem(SystemEntry entry)
	{
		m_systems.push_back(std::move(entry));
		m_sorted = false;
	}

	/// @brief 名前でシステムを削除する
	/// @param name 削除するシステム名
	void removeSystem(const std::string& name)
	{
		m_systems.erase(
			std::remove_if(m_systems.begin(), m_systems.end(),
				[&name](const SystemEntry& e) { return e.name == name; }),
			m_systems.end());
	}

	/// @brief 全システムを優先度順に実行する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		ensureSorted();
		for (auto& system : m_systems)
		{
			if (system.updateFn)
			{
				system.updateFn(dt);
			}
		}
	}

	/// @brief 登録されているシステム名を実行順で取得する
	/// @return システム名のベクタ（優先度順）
	[[nodiscard]] std::vector<std::string> systemNames() const
	{
		/// ソート済みコピーを作成
		auto sorted = m_systems;
		std::sort(sorted.begin(), sorted.end(),
			[](const SystemEntry& a, const SystemEntry& b)
			{
				return a.priority < b.priority;
			});

		std::vector<std::string> names;
		names.reserve(sorted.size());
		for (const auto& system : sorted)
		{
			names.push_back(system.name);
		}
		return names;
	}

	/// @brief 実行順をJSON文字列として返す
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		const auto names = systemNames();
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < names.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += "\"" + names[i] + "\"";
		}
		json += "]";
		return json;
	}

	/// @brief 登録システム数を取得する
	/// @return システム数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_systems.size();
	}

	/// @brief 全システムをクリアする
	void clear() noexcept
	{
		m_systems.clear();
		m_sorted = false;
	}

private:
	/// @brief ソート状態を保証する
	void ensureSorted()
	{
		if (!m_sorted)
		{
			std::sort(m_systems.begin(), m_systems.end(),
				[](const SystemEntry& a, const SystemEntry& b)
				{
					return a.priority < b.priority;
				});
			m_sorted = true;
		}
	}

	std::vector<SystemEntry> m_systems;  ///< 登録済みシステム
	bool m_sorted = false;               ///< ソート済みフラグ
};

} // namespace mitiru::ecs
