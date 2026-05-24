#pragma once

/// @file Invariant.hpp
/// @brief 不変条件 (invariant) の宣言・毎フレーム check・dual-readable な違反通知
/// @details
/// game が「常に真であるべき条件」を宣言し、毎フレーム check する。違反したら:
///   (a) EventLog に `type:"invariant_violation"` で emit  → 機械可読 (AI が tail)
///   (b) 直近違反リストを内部保持                          → 窓 / inspector 表示用
///
/// ADR 0005 (GameMemory が唯一の state) との整合:
/// - predicate は GameMemory の値を閉じ込めた lambda。GameMemory が唯一の
///   真実なので、check は決定的 (deterministic) であり、replay でも同じ違反が
///   同じ frame で再現する。
///
/// 使い方:
/// @code
///   mitiru::observe::InvariantSet inv;
///   inv.add("hp_non_negative",
///           [&mem]{ return mem.hp >= 0; },
///           [&mem]{ return "hp=" + std::to_string(mem.hp); });
///   // each frame:
///   inv.check(frame, mem.eventLog);
///   // draw side:
///   if (!inv.recent().empty()) { /* 赤帯を描く */ }
/// @endcode

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "EventLog.hpp"

namespace mitiru::observe
{

/// @brief 違反 1 件の記録。frame / 名前 / 詳細文字列。
struct Violation
{
	std::uint32_t frame{0};
	std::string   name;
	std::string   detail;
};

/// @brief 宣言された不変条件の集合。毎フレーム check して違反を通知する。
/// @details predicate が false を返したら違反。EventLog に emit し、直近違反を
///          内部リングに保持する (窓 / inspector が getter で読む)。
class InvariantSet
{
public:
	/// @brief 不変条件を 1 つ宣言する。
	/// @param name      条件名 (event の data.name に出る)
	/// @param predicate 真であるべき条件。false で違反。
	/// @param detail    違反時に呼ぶ詳細文字列生成 (現在の値など)。任意。
	void add(std::string name,
	         std::function<bool()> predicate,
	         std::function<std::string()> detail = {})
	{
		m_rules.push_back(Rule{std::move(name), std::move(predicate), std::move(detail)});
	}

	/// @brief 全条件を check する。違反を EventLog に emit + 直近リストに記録。
	/// @param frame この check が走った frame
	/// @param log   違反を append する EventLog
	/// @details 同じ条件が連続違反しても毎フレーム emit はしない (前 frame と
	///          同じ違反状態なら EventLog への spam を避ける)。状態が
	///          「健全→違反」へ遷移した瞬間と、違反中の最新値だけを recent に残す。
	void check(std::uint32_t frame, EventLog& log)
	{
		m_recent.clear();
		for (auto& rule : m_rules)
		{
			bool ok = true;
			try { ok = rule.predicate ? rule.predicate() : true; }
			catch (...) { ok = true; }  // predicate 自体の例外は違反扱いしない

			if (ok)
			{
				rule.wasViolating = false;
				continue;
			}

			std::string detail;
			if (rule.detail)
			{
				try { detail = rule.detail(); } catch (...) {}
			}

			m_recent.push_back(Violation{frame, rule.name, detail});

			// rising edge (健全→違反) のときだけ EventLog に emit。
			// 違反が続く間 60fps で spam するのを防ぐ。
			if (!rule.wasViolating)
			{
				log.emit(frame, "invariant_violation", {
					{"name",   rule.name},
					{"detail", detail},
				});
			}
			rule.wasViolating = true;
		}
	}

	/// @brief 直近 check で違反していた条件のリスト (窓 / inspector 表示用)。空なら健全。
	[[nodiscard]] const std::vector<Violation>& recent() const noexcept { return m_recent; }

	/// @brief 宣言済み条件の数
	[[nodiscard]] std::size_t size() const noexcept { return m_rules.size(); }

private:
	struct Rule
	{
		std::string                   name;
		std::function<bool()>         predicate;
		std::function<std::string()>  detail;
		bool                          wasViolating{false};
	};

	std::vector<Rule>      m_rules;
	std::vector<Violation> m_recent;
};

}  // namespace mitiru::observe
