#pragma once

/// @file DialogueRunner.hpp
/// @brief 会話を進める。進み具合は DialogueState (flat POD、GameMemory に置く) だけで、旗は NarrativeVars に書く。
/// @details 台詞か選択肢に着くまで命令を進めて止まる。ゲームは「進める」「選ぶ」を入力から呼び、表示は
///          currentLine / choiceAt を毎フレーム読んで UI へ写す。巻き戻せば会話の位置と旗が一緒に戻る。

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/narrative/Dialogue.hpp>

namespace mitiru::narrative
{

struct DialogueState
{
	static constexpr int kMaxDepth = 8;
	static constexpr int kMaxChoices = 6;

	std::uint32_t script = 0;   ///< 話している DialogueScript::id。0 は話していない
	std::uint16_t pc = 0;
	std::uint8_t  mode = 0;     ///< 0 = 話していない、1 = 台詞、2 = 選択肢
	std::uint8_t  depth = 0;
	std::uint16_t stack[kMaxDepth]{};
	std::uint16_t choices[kMaxChoices]{};   ///< 見えている選択肢の命令の添字
	std::uint8_t  choiceCount = 0;
	std::uint8_t  pad[3]{};
	std::uint32_t serial = 0;   ///< 台詞か選択肢が替わるたびに 1 増える (UI の文字送りを頭から始める目安)

	[[nodiscard]] bool talking() const noexcept { return mode != 0; }
	[[nodiscard]] bool inScript(const DialogueScript& s) const noexcept { return mode != 0 && script == s.id; }
};
static_assert(std::is_trivially_copyable_v<DialogueState>);

namespace dialogue_detail
{

constexpr int kMaxStepsPerRun = 4096;

/// @brief pc から続く Choice の組を集め、条件の通るものを choices に入れる。1 つも無ければ組の後ろへ進める
inline bool collectChoices(DialogueState& st, const NarrativeVars& vars, const DialogueScript& s) noexcept
{
	st.choiceCount = 0;
	std::size_t i = st.pc;
	for (; i < s.ops.size() && s.ops[i].kind == DialogueOpKind::Choice; ++i)
	{
		if (st.choiceCount < DialogueState::kMaxChoices && s.exprs.test(s.ops[i].expr, vars))
		{
			st.choices[st.choiceCount++] = static_cast<std::uint16_t>(i);
		}
	}
	if (st.choiceCount > 0) { return true; }
	st.pc = static_cast<std::uint16_t>(i);
	return false;
}

inline void stop(DialogueState& st) noexcept
{
	st.mode = 0;
	st.script = 0;
	st.depth = 0;
	st.choiceCount = 0;
}

/// @brief 1 命令を実行する。台詞か選択肢で止まるなら true
template <class OnCue>
bool step(DialogueState& st, NarrativeVars& vars, const DialogueScript& s, OnCue& onCue)
{
	const DialogueOp& op = s.ops[st.pc];
	switch (op.kind)
	{
	case DialogueOpKind::Line:      st.mode = 1; return true;
	case DialogueOpKind::Choice:    if (collectChoices(st, vars, s)) { st.mode = 2; return true; } return false;
	case DialogueOpKind::Jump:      st.pc = op.target; return false;
	case DialogueOpKind::JumpIfNot: st.pc = s.exprs.test(op.expr, vars) ? st.pc + 1 : op.target; return false;
	case DialogueOpKind::Set:       (void)vars.set(op.var, s.exprs.eval(op.expr, vars)); ++st.pc; return false;
	case DialogueOpKind::CueOp:     onCue(op.cue); ++st.pc; return false;
	case DialogueOpKind::Call:
		if (st.depth >= DialogueState::kMaxDepth) { stop(st); return true; }
		st.stack[st.depth++] = static_cast<std::uint16_t>(st.pc + 1);
		st.pc = op.target;
		return false;
	case DialogueOpKind::Return:
		if (st.depth == 0) { stop(st); return true; }
		st.pc = st.stack[--st.depth];
		return false;
	default: stop(st); return true;
	}
}

/// @brief 台詞か選択肢か終わりに着くまで進める
template <class OnCue>
void run(DialogueState& st, NarrativeVars& vars, const DialogueScript& s, OnCue& onCue)
{
	for (int n = 0; n < kMaxStepsPerRun; ++n)
	{
		if (st.pc >= s.ops.size()) { stop(st); return; }
		if (step(st, vars, s, onCue))
		{
			if (st.mode != 0) { ++st.serial; }
			return;
		}
	}
	debug::warnOnce("narrative.dialogue.loop",
		"会話 " + s.name + " が " + std::to_string(kMaxStepsPerRun) + " 命令進んでも台詞に着かないので、止めました。"
		"@jump か @call が台詞を挟まずに輪になっていないか確かめ、輪の中に台詞か @end を置いてください。");
	stop(st);
}

}  // namespace dialogue_detail

/// @brief label から話し始める。label が無ければ何もせず false
template <class OnCue>
bool startDialogue(DialogueState& st, NarrativeVars& vars, const DialogueScript& s, std::string_view label, OnCue&& onCue)
{
	const int at = s.findLabel(label);
	if (at < 0) { return false; }
	const std::uint32_t serial = st.serial;
	st = DialogueState{};
	st.serial = serial;
	st.script = s.id;
	st.pc = static_cast<std::uint16_t>(at);
	st.mode = 1;
	dialogue_detail::run(st, vars, s, onCue);
	return true;
}

/// @brief 台詞を読み終えて次へ進める (選択肢の時は何もしない)
template <class OnCue>
void advanceDialogue(DialogueState& st, NarrativeVars& vars, const DialogueScript& s, OnCue&& onCue)
{
	if (!st.inScript(s) || st.mode != 1) { return; }
	++st.pc;
	dialogue_detail::run(st, vars, s, onCue);
}

/// @brief 見えている選択肢の index 番目を選ぶ。範囲外なら false
template <class OnCue>
bool chooseDialogue(DialogueState& st, NarrativeVars& vars, const DialogueScript& s, int index, OnCue&& onCue)
{
	if (!st.inScript(s) || st.mode != 2 || index < 0 || index >= st.choiceCount) { return false; }
	st.pc = s.ops[st.choices[index]].target;
	st.choiceCount = 0;
	dialogue_detail::run(st, vars, s, onCue);
	return true;
}

/// @brief 今の台詞。台詞を出していなければ nullptr
[[nodiscard]] inline const DialogueOp* currentLine(const DialogueState& st, const DialogueScript& s) noexcept
{
	return (st.inScript(s) && st.mode == 1 && st.pc < s.ops.size()) ? &s.ops[st.pc] : nullptr;
}

/// @brief 見えている選択肢の index 番目。無ければ nullptr
[[nodiscard]] inline const DialogueOp* choiceAt(const DialogueState& st, const DialogueScript& s, int index) noexcept
{
	if (!st.inScript(s) || st.mode != 2 || index < 0 || index >= st.choiceCount) { return nullptr; }
	return &s.ops[st.choices[index]];
}

}  // namespace mitiru::narrative
