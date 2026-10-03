#pragma once

/// @file NarrativeHud.hpp
/// @brief カットシーン・会話・クエストを UI (RmlUi の data model "view") へ写す関数と、合図の決まった受け方。
/// @details 値は毎フレーム GameMemory から作り直して送る (UI は何も覚えない。巻き戻した後も次のフレームで合う)。
///          文字列はスタックの固定長の buffer で JSON にするので、毎フレーム呼んでもヒープを使わない。
///          送る名前は docs/DIALOGUE.md の表のとおり (`view.talk_text` なら RML では `{{ talk_text }}`)。

#include <cstddef>
#include <cstdio>
#include <string_view>

#include <mitiru/module/Game.hpp>
#include <mitiru/narrative/DialogueRunner.hpp>
#include <mitiru/narrative/QuestLog.hpp>
#include <mitiru/narrative/SequencePlayer.hpp>

namespace mitiru::narrative
{

/// @brief hud.set に渡す JSON を固定長の buffer に書く。入りきらなければ c_str は空の配列 "[]" を返す (壊れた JSON を送らない)
template <std::size_t N>
class FixedJson
{
public:
	FixedJson& raw(std::string_view s) noexcept
	{
		for (const char c : s) { put(c); }
		return *this;
	}
	FixedJson& str(std::string_view s) noexcept
	{
		put('"');
		for (const char c : s)
		{
			if (c == '"' || c == '\\') { put('\\'); put(c); }
			else if (c == '\n') { put('\\'); put('n'); }
			else if (static_cast<unsigned char>(c) >= 0x20) { put(c); }
		}
		put('"');
		return *this;
	}
	FixedJson& num(int v) noexcept
	{
		char b[16];
		const int n = std::snprintf(b, sizeof(b), "%d", v);
		return raw(std::string_view(b, n > 0 ? static_cast<std::size_t>(n) : 0));
	}
	[[nodiscard]] const char* c_str() noexcept
	{
		if (m_overflow) { return "[]"; }
		m_buf[m_len] = '\0';
		return m_buf;
	}

private:
	void put(char c) noexcept
	{
		if (m_len + 1 < N) { m_buf[m_len++] = c; }
		else { m_overflow = true; }
	}

	char        m_buf[N]{};
	std::size_t m_len = 0;
	bool        m_overflow = false;
};

/// @brief 音・BGM・印の合図を hud で鳴らす。vfx / call / anim はゲームが受けるので false を返す
inline bool playCue(const Cue& cue, Hud& hud) noexcept
{
	switch (cue.kind)
	{
	case CueKind::Sound: hud.play(cue.name.c_str(), cue.value); return true;
	case CueKind::Music:
		if (cue.name.empty()) { hud.stopMusic(1.0f); }
		else { hud.music(cue.name.c_str(), true, cue.value, 1.0f); }
		return true;
	case CueKind::Mark: hud.mark(cue.name.c_str()); return true;
	default: return false;
	}
}

/// @brief カットシーンの字幕と再生中かを送る (view.cut_on / sub_on / sub_speaker / sub_text)。
///        再生中は host にカットシーン中だと伝え、Rewind 窓のバーに区間を出す (ABI v50)
inline void pushSequence(Hud& hud, const SequencePlayback& pb, const Sequence& seq) noexcept
{
	const bool on = pb.showing(seq);
	if (on)
	{
		hud.cinematic();
		hud.timelineMarker(seq.name.c_str(), pb.time, seq.duration);
	}
	const Subtitle* sub = on ? subtitleAt(seq, pb.time) : nullptr;
	hud.set("view.cut_on", on);
	hud.set("view.cut_skippable", on && seq.skippable);
	hud.set("view.sub_on", sub != nullptr);
	hud.set("view.sub_speaker", sub != nullptr ? sub->speaker.c_str() : "");
	hud.set("view.sub_text", sub != nullptr ? sub->text.c_str() : "");
}

/// @brief 会話の今の台詞と選択肢を送る (view.talk_on / talk_speaker / talk_text / talk_serial / talk_choosing / talk_choices)。
/// talk_choices は [{"i": 番号, "text": 文言}] で、RML は data-for で並べ、押されたら dispatch('talk.choose', 'i', c.i) を返す
inline void pushDialogue(Hud& hud, const DialogueState& st, const DialogueScript& s) noexcept
{
	const DialogueOp* line = currentLine(st, s);
	hud.set("view.talk_on", st.inScript(s));
	hud.set("view.talk_speaker", line != nullptr ? line->speaker.c_str() : "");
	hud.set("view.talk_text", line != nullptr ? line->text.c_str() : "");
	hud.set("view.talk_serial", static_cast<int>(st.serial));
	hud.set("view.talk_choosing", st.inScript(s) && st.mode == 2);
	FixedJson<2048> json;
	json.raw("[");
	for (int i = 0; const DialogueOp* c = choiceAt(st, s, i); ++i)
	{
		json.raw(i > 0 ? ",{\"i\":" : "{\"i\":").num(i).raw(",\"text\":").str(c->text).raw("}");
	}
	hud.set("view.talk_choices", json.raw("]").c_str());
}

/// @brief 始まったクエストを送る (view.quests = [{"title", "state", "objectives": [{"text", "done"}]}])。
/// state は "active" / "completed" / "failed"
inline void pushQuests(Hud& hud, const QuestLog& log, const QuestTable& table) noexcept
{
	static constexpr const char* kState[] = {"hidden", "active", "completed", "failed"};
	FixedJson<3584> json;
	json.raw("[");
	int written = 0;
	for (const QuestDef& q : table.quests)
	{
		const int slot = log.find(q.id);
		if (slot < 0) { continue; }
		json.raw(written++ > 0 ? ",{\"title\":" : "{\"title\":").str(q.title.text);
		json.raw(",\"state\":").str(kState[log.status[slot] & 3u]).raw(",\"objectives\":[");
		for (std::size_t i = 0; i < q.objectives.size(); ++i)
		{
			json.raw(i > 0 ? ",{\"text\":" : "{\"text\":").str(q.objectives[i].text.text);
			json.raw(",\"done\":").num(log.objectiveDone(q.id, static_cast<int>(i)) ? 1 : 0).raw("}");
		}
		json.raw("]}");
	}
	hud.set("view.quests", json.raw("]").c_str());
	hud.set("view.quest_count", written);
}

}  // namespace mitiru::narrative
