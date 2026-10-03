#pragma once

/// @file QuestLog.hpp
/// @brief クエストの表 (JSON、読み取り専用) と、進み具合の QuestLog (flat POD、GameMemory に置く)。書式は docs/DIALOGUE.md。
/// @details クエストは旗 (NarrativeVars) の条件で進む。会話が `@set` で旗を立て、updateQuests が表の順に条件を見て
///          始まり・目標の達成・完了・失敗を QuestLog に書き、変わった所を onEvent へ渡す。達成した目標は旗が戻っても戻らない。

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/core/Localization.hpp>
#include <mitiru/narrative/NarrativeExpr.hpp>

namespace mitiru::narrative
{

/// @brief 訳せる文言 (表示する text、書かれたままの source、訳の鍵 key)
struct LocalText
{
	std::string text;
	std::string source;
	std::string key;

	void localize(const LocalizationManager& loc, const std::string& language)
	{
		text = key.empty() ? source : loc.find(key, language).value_or(source);
	}
};

struct QuestObjective
{
	std::string name;
	LocalText   text;
	ExprRef     done{};
};

struct QuestDef
{
	std::string                 name;
	std::uint32_t               id = 0;
	LocalText                   title;
	ExprRef                     start{};
	ExprRef                     complete{};   ///< 書かなければ「目標が全部済んだら」
	ExprRef                     fail{};       ///< 書かなければ失敗しない
	std::vector<QuestObjective> objectives;
};

class QuestTable
{
public:
	std::vector<QuestDef> quests;
	ExprPool              exprs;
	std::vector<std::string> names;   ///< 条件に出てきた旗とクエストの名前 (story のツール窓が鍵を名前で出す)

	void localize(const LocalizationManager& loc, const std::string& language)
	{
		for (QuestDef& q : quests)
		{
			q.title.localize(loc, language);
			for (QuestObjective& o : q.objectives) { o.text.localize(loc, language); }
		}
	}
};

enum class QuestStatus : std::uint8_t { Hidden = 0, Active, Completed, Failed };
enum class QuestEvent : std::uint8_t { Started, ObjectiveDone, Completed, Failed };

/// @brief 進み具合。始まったクエストだけが始まった順に並ぶ
struct QuestLog
{
	static constexpr int kCapacity = 32;
	static constexpr int kMaxObjectives = 32;

	std::uint32_t quest[kCapacity]{};       ///< QuestDef::id
	std::uint32_t objectivesDone[kCapacity]{};  ///< 済んだ目標の bit
	std::uint8_t  status[kCapacity]{};      ///< QuestStatus
	std::uint32_t count = 0;

	[[nodiscard]] int find(std::uint32_t id) const noexcept
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (quest[i] == id) { return static_cast<int>(i); }
		}
		return -1;
	}
	[[nodiscard]] QuestStatus statusOf(std::uint32_t id) const noexcept
	{
		const int i = find(id);
		return i < 0 ? QuestStatus::Hidden : static_cast<QuestStatus>(status[i]);
	}
	[[nodiscard]] bool objectiveDone(std::uint32_t id, int objective) const noexcept
	{
		const int i = find(id);
		return i >= 0 && objective >= 0 && objective < kMaxObjectives && ((objectivesDone[i] >> objective) & 1u) != 0;
	}
};
static_assert(std::is_trivially_copyable_v<QuestLog>);

namespace quest_detail
{

template <class OnEvent>
void advanceActive(QuestLog& log, int slot, const QuestDef& q, const QuestTable& t, const NarrativeVars& vars, OnEvent& onEvent)
{
	std::uint32_t& done = log.objectivesDone[slot];
	const int n = static_cast<int>(q.objectives.size());
	for (int i = 0; i < n; ++i)
	{
		const std::uint32_t bit = 1u << i;
		if ((done & bit) == 0 && t.exprs.test(q.objectives[static_cast<std::size_t>(i)].done, vars))
		{
			done |= bit;
			onEvent(q, QuestEvent::ObjectiveDone, i);
		}
	}
	const std::uint32_t all = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
	if (q.fail.count != 0 && t.exprs.test(q.fail, vars))
	{
		log.status[slot] = static_cast<std::uint8_t>(QuestStatus::Failed);
		onEvent(q, QuestEvent::Failed, -1);
		return;
	}
	const bool complete = q.complete.count != 0 ? t.exprs.test(q.complete, vars) : (done & all) == all;
	if (complete)
	{
		log.status[slot] = static_cast<std::uint8_t>(QuestStatus::Completed);
		onEvent(q, QuestEvent::Completed, -1);
	}
}

}  // namespace quest_detail

/// @brief 表の順に条件を見て進める。変わった所を onEvent(const QuestDef&, QuestEvent, int objective) へ渡す。
/// 1 回の呼び出しで、始まったクエストの目標と完了まで見る (同じフレームに旗が全部立てば完了まで進む)
template <class OnEvent>
void updateQuests(QuestLog& log, const QuestTable& table, const NarrativeVars& vars, OnEvent&& onEvent)
{
	for (const QuestDef& q : table.quests)
	{
		int slot = log.find(q.id);
		if (slot < 0)
		{
			if (q.start.count == 0 || !table.exprs.test(q.start, vars) || log.count >= static_cast<std::uint32_t>(QuestLog::kCapacity)) { continue; }
			slot = static_cast<int>(log.count++);
			log.quest[slot] = q.id;
			log.status[slot] = static_cast<std::uint8_t>(QuestStatus::Active);
			onEvent(q, QuestEvent::Started, -1);
		}
		if (log.status[slot] == static_cast<std::uint8_t>(QuestStatus::Active))
		{
			quest_detail::advanceActive(log, slot, q, table, vars, onEvent);
		}
	}
}

}  // namespace mitiru::narrative

#include <mitiru/narrative/detail/QuestLoad.hpp>
