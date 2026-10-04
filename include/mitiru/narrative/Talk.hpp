#pragma once

/// @file Talk.hpp
/// @brief 会話 1 本を「始める」「毎フレーム回す」の 2 つで動かす手助け。表示はエンジン同梱の mitiru:talk.rml。
/// @details GameMemory に置く。中身は会話の位置 (DialogueState) と選択肢のカーソルだけの flat POD なので、
///          巻き戻すと会話の位置も戻り、同じ update をもう一度回しても同じ結果になる。update は決定の入力で
///          台詞を送り、上下とクリックで選択肢を選び、合図 (音・BGM・印) を鳴らし、UI へ view.talk_* を送る。
///          @set と @if で旗を使う台本なら、NarrativeVars を GameMemory に置いて渡す。使い方は docs/DIALOGUE.md。

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/Game.hpp>
#include <mitiru/narrative/DialogueRunner.hpp>
#include <mitiru/narrative/NarrativeHud.hpp>
#include <mitiru/narrative/NarrativeVars.hpp>

namespace mitiru::narrative
{

struct Talk
{
	DialogueState state{};
	std::int32_t  cursor = 0;        ///< 選んでいる選択肢
	std::uint8_t  startedNow = 0;    ///< 始めたフレームは、始めた決定の入力で台詞を送らない
	std::uint8_t  _pad[3]{};

	/// @brief 台本 s の label から話し始める。もう話している・label が無いときは false
	bool start(Hud hud, const DialogueScript& s, std::string_view label, NarrativeVars* vars = nullptr)
	{
		if (state.talking()) { return false; }
		NarrativeVars scratch{};
		NarrativeVars& v = varsFor(s, vars, scratch);
		cursor = 0;
		if (!startDialogue(state, v, s, label, [&](const Cue& c) { playCue(c, hud); })) { return false; }
		startedNow = 1;
		return true;
	}

	/// @brief 毎フレーム 1 回呼ぶ。話していない間は UI へ「閉じている」を送るだけ
	void update(Input in, Hud hud, const DialogueScript& s, NarrativeVars* vars = nullptr)
	{
		if (state.inScript(s) && startedNow == 0)
		{
			NarrativeVars scratch{};
			step(in, hud, s, varsFor(s, vars, scratch));
		}
		startedNow = 0;
		pushDialogue(hud, state, s);
		hud.set("view.talk_cursor", cursor);
	}

	[[nodiscard]] bool active(const DialogueScript& s) const noexcept { return state.inScript(s); }

private:
	void step(Input in, Hud hud, const DialogueScript& s, NarrativeVars& vars)
	{
		const auto onCue = [&](const Cue& c) { playCue(c, hud); };
		const int count = state.mode == 2 ? state.choiceCount : 0;
		if (count > 0 && in.pressed(Key::Up) && cursor > 0) { --cursor; }
		if (count > 0 && in.pressed(Key::Down) && cursor + 1 < count) { ++cursor; }
		if (in.action("talk.choose"))
		{
			if (chooseDialogue(state, vars, s, in.actionPayloadInt("talk.choose", "i", -1), onCue)) { cursor = 0; }
		}
		else if (in.confirmPressed() && count > 0)
		{
			if (chooseDialogue(state, vars, s, cursor, onCue)) { cursor = 0; }
		}
		else if (in.confirmPressed()) { advanceDialogue(state, vars, s, onCue); }
	}

	/// 旗を使う台本で vars を渡し忘れると、@set が毎回消えて @if が常に偽になる。黙って進めないよう 1 回知らせる
	static NarrativeVars& varsFor(const DialogueScript& s, NarrativeVars* vars, NarrativeVars& scratch)
	{
		if (vars != nullptr) { return *vars; }
		if (!s.names.empty())
		{
			debug::warnOnce("narrative.talk.vars",
				"会話 " + s.name + " は旗を使いますが、Talk に NarrativeVars が渡されていないので、@set した値は残りません。"
				"NarrativeVars を GameMemory に置き、start と update の最後の引数に渡してください。");
		}
		return scratch;
	}
};

static_assert(std::is_trivially_copyable_v<Talk>);

}  // namespace mitiru::narrative
