#pragma once

/// @file StoryInspect.hpp
/// @brief story のツール窓 (--page story) に、GameMemory の中のカットシーン・会話・旗・クエストを名前で見せる (読み取り専用)。
/// @details ゲーム DLL は StoryInspect に「GameMemory のどこに何があるか」(offsetof) と台本を渡し、出来た JSON を
///          MITIRU_INSPECT_ASSETS の kind "story" で host へ渡す。host は JSON の slots に書いた範囲の bytes を
///          GameMemory から写してツール窓へ送るだけで、中身を知らない。読み解くのはツール窓 (decodeStory) で、旗は hash でなく
///          台本に出てくる名前で出る。asset.reloaded で台本が変わったら、host が同じ export を呼んで写し替える (ABI v50)。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/BoundaryTypes.hpp>
#include <mitiru/narrative/DialogueRunner.hpp>
#include <mitiru/narrative/QuestLog.hpp>
#include <mitiru/narrative/SequencePlayer.hpp>

namespace mitiru::narrative
{

inline constexpr const char* kStoryInspectKind = "story";

/// @brief GameMemory の中の物語の状態の置き場と、鍵の名前の表を JSON にする (DLL が読み込みの時に作る)
class StoryInspect
{
public:
	StoryInspect& vars(std::size_t offset, std::string_view label = "vars")
	{
		return slot("vars", label, offset, sizeof(NarrativeVars));
	}
	StoryInspect& playback(std::size_t offset, std::string_view label = "cutscene")
	{
		return slot("playback", label, offset, sizeof(SequencePlayback));
	}
	StoryInspect& dialogue(std::size_t offset, std::string_view label = "talk")
	{
		return slot("dialogue", label, offset, sizeof(DialogueState));
	}
	StoryInspect& quests(std::size_t offset, const QuestTable& table, std::string_view label = "quests")
	{
		for (const QuestDef& q : table.quests)
		{
			nlohmann::json objectives = nlohmann::json::array();
			for (const QuestObjective& o : q.objectives) { objectives.push_back(o.name); }
			m_root["quests"][std::to_string(q.id)] = {{"name", q.name}, {"objectives", std::move(objectives)}};
		}
		names(table.names);
		return slot("quests", label, offset, sizeof(QuestLog));
	}
	StoryInspect& add(const Sequence& seq)
	{
		m_root["sequences"][std::to_string(seq.id)] = {{"name", seq.name}, {"duration", seq.duration}};
		return *this;
	}
	StoryInspect& add(const DialogueScript& script)
	{
		m_root["scripts"][std::to_string(script.id)] = script.name;
		return names(script.names);
	}
	/// @brief 台本に出てこない旗の名前 (ゲームが C++ で書く旗) を足す
	StoryInspect& names(const std::vector<std::string>& list)
	{
		for (const std::string& n : list) { m_root["names"][std::to_string(nameHash(n))] = n; }
		return *this;
	}

	/// @brief 今までに渡した物の JSON。返した文字列は次にこの関数を呼ぶまで変わらない (InspectAsset の data に使う)
	[[nodiscard]] const std::string& text()
	{
		m_text = m_root.dump();
		return m_text;
	}

	/// @brief MITIRU_INSPECT_ASSETS の関数で out に書く 1 件。text() を呼んだ後に使う
	[[nodiscard]] module::InspectAsset asset() const noexcept
	{
		module::InspectAsset a{};
		std::memcpy(a.kind, kStoryInspectKind, std::strlen(kStoryInspectKind));
		std::memcpy(a.name, "story", 5);
		a.data = m_text.data();
		a.size = m_text.size();
		return a;
	}

private:
	StoryInspect& slot(std::string_view kind, std::string_view label, std::size_t offset, std::size_t size)
	{
		m_root["slots"].push_back({{"kind", kind}, {"label", label}, {"offset", offset}, {"size", size}});
		return *this;
	}

	nlohmann::json m_root = {{"slots", nlohmann::json::array()}, {"names", nlohmann::json::object()}};
	std::string    m_text;
};

namespace story_detail
{

template <class T>
[[nodiscard]] bool readPod(const std::vector<std::uint8_t>& bytes, T& out) noexcept
{
	if (bytes.size() != sizeof(T)) { return false; }
	std::memcpy(&out, bytes.data(), sizeof(T));
	return true;
}

[[nodiscard]] inline std::string nameOf(const nlohmann::json& table, std::uint32_t key)
{
	const std::string k = std::to_string(key);
	if (table.is_object() && table.contains(k))
	{
		const nlohmann::json& v = table[k];
		if (v.is_string()) { return v.get<std::string>(); }
		if (v.is_object() && v.contains("name") && v["name"].is_string()) { return v["name"].get<std::string>(); }
	}
	return "#" + k;
}

[[nodiscard]] inline nlohmann::json decodeVars(const NarrativeVars& v, const nlohmann::json& layout)
{
	nlohmann::json out = nlohmann::json::array();
	const nlohmann::json names = layout.value("names", nlohmann::json::object());
	const std::uint32_t n = v.count < static_cast<std::uint32_t>(NarrativeVars::kCapacity) ? v.count : NarrativeVars::kCapacity;
	for (std::uint32_t i = 0; i < n; ++i) { out.push_back({{"name", nameOf(names, v.key[i])}, {"value", v.value[i]}}); }
	return out;
}

[[nodiscard]] inline nlohmann::json decodePlayback(const SequencePlayback& pb, const nlohmann::json& layout)
{
	static constexpr const char* kState[] = {"stopped", "playing", "finished"};
	const nlohmann::json seqs = layout.value("sequences", nlohmann::json::object());
	const std::string key = std::to_string(pb.sequence);
	const double duration = seqs.contains(key) ? seqs[key].value("duration", 0.0) : 0.0;
	return {{"sequence", pb.sequence == 0 ? std::string() : nameOf(seqs, pb.sequence)}, {"state", kState[pb.state < 3 ? pb.state : 0]},
	        {"time", pb.time}, {"duration", duration}, {"skipped", pb.skipped != 0}};
}

[[nodiscard]] inline nlohmann::json decodeDialogue(const DialogueState& st, const nlohmann::json& layout)
{
	static constexpr const char* kMode[] = {"none", "line", "choice"};
	return {{"script", st.script == 0 ? std::string() : nameOf(layout.value("scripts", nlohmann::json::object()), st.script)},
	        {"mode", kMode[st.mode < 3 ? st.mode : 0]}, {"pc", st.pc}, {"depth", st.depth},
	        {"choices", st.choiceCount}, {"serial", st.serial}};
}

[[nodiscard]] inline nlohmann::json decodeQuests(const QuestLog& log, const nlohmann::json& layout)
{
	static constexpr const char* kStatus[] = {"hidden", "active", "completed", "failed"};
	const nlohmann::json defs = layout.value("quests", nlohmann::json::object());
	nlohmann::json out = nlohmann::json::array();
	const std::uint32_t n = log.count < static_cast<std::uint32_t>(QuestLog::kCapacity) ? log.count : QuestLog::kCapacity;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		const std::string key = std::to_string(log.quest[i]);
		const nlohmann::json names = defs.contains(key) ? defs[key].value("objectives", nlohmann::json::array()) : nlohmann::json::array();
		nlohmann::json objectives = nlohmann::json::array();
		for (std::size_t k = 0; k < names.size() && k < static_cast<std::size_t>(QuestLog::kMaxObjectives); ++k)
		{
			objectives.push_back({{"name", names[k]}, {"done", ((log.objectivesDone[i] >> k) & 1u) != 0}});
		}
		out.push_back({{"name", nameOf(defs, log.quest[i])}, {"status", kStatus[log.status[i] & 3u]},
		               {"objectives", std::move(objectives)}});
	}
	return out;
}

[[nodiscard]] inline nlohmann::json decodeSlot(const std::string& kind, const std::vector<std::uint8_t>& bytes,
                                               const nlohmann::json& layout)
{
	NarrativeVars vars;
	SequencePlayback pb;
	DialogueState st;
	QuestLog log;
	if (kind == "vars" && readPod(bytes, vars)) { return decodeVars(vars, layout); }
	if (kind == "playback" && readPod(bytes, pb)) { return decodePlayback(pb, layout); }
	if (kind == "dialogue" && readPod(bytes, st)) { return decodeDialogue(st, layout); }
	if (kind == "quests" && readPod(bytes, log)) { return decodeQuests(log, layout); }
	return nullptr;
}

}  // namespace story_detail

/// @brief layout (StoryInspect の JSON) と、slots の順に写した bytes から、名前つきの今の状態を作る (ツール窓が使う)。
///        種類か大きさの合わない slot には "error" を付ける (DLL とツール窓のビルドが違う)
[[nodiscard]] inline nlohmann::json decodeStory(const nlohmann::json& layout, const std::vector<std::vector<std::uint8_t>>& slots)
{
	nlohmann::json out = nlohmann::json::array();
	const nlohmann::json list = layout.value("slots", nlohmann::json::array());
	for (std::size_t i = 0; i < list.size() && i < slots.size(); ++i)
	{
		const std::string kind = list[i].value("kind", std::string());
		nlohmann::json row = {{"kind", kind}, {"label", list[i].value("label", std::string())}};
		nlohmann::json value = story_detail::decodeSlot(kind, slots[i], layout);
		if (value.is_null()) { row["error"] = "種類か大きさが合わない (DLL とツール窓のビルドが違う)"; }
		else { row["value"] = std::move(value); }
		out.push_back(std::move(row));
	}
	return out;
}

}  // namespace mitiru::narrative
