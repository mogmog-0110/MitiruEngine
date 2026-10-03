#pragma once

/// @file DialogueCompile.hpp
/// @brief LineScript で読んだ会話の台本の行を DialogueOp の列に直す。

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/i18n/Utf8Text.hpp>
#include <mitiru/script/LineScript.hpp>

namespace mitiru::narrative
{

namespace dialogue_detail
{

/// @brief 後で決めるジャンプ先 (ラベルがまだ出てきていない)
struct Fixup
{
	std::size_t op = 0;
	std::string label;
	int         line = 0;
};

/// @brief @if の入れ子 1 段。偽の時に飛ぶ命令と、各枝の終わりから @endif へ飛ぶ命令を覚える
struct IfBlock
{
	std::optional<std::size_t> pendingFalse;
	std::vector<std::size_t>   exits;
	int                        line = 0;
};

class Compiler
{
public:
	explicit Compiler(DialogueScript& out) : m_out(out) {}

	[[nodiscard]] bool run(const std::vector<script::LineRecord>& records, std::string& error)
	{
		for (const script::LineRecord& r : records)
		{
			if (!record(r)) { break; }
		}
		if (m_error.empty() && !m_ifs.empty()) { fail(m_ifs.back().line, "@if に対する @endif が無い"); }
		if (m_error.empty()) { emit(DialogueOpKind::End, records.empty() ? 0 : records.back().line); }
		if (m_error.empty()) { resolve(); }
		if (m_error.empty() && m_out.ops.size() > 65535) { fail(0, "命令が多すぎる (65535 まで)"); }
		m_out.names = m_names.sortedNames();
		error = m_error;
		return m_error.empty();
	}

private:
	bool fail(int line, const std::string& why)
	{
		if (m_error.empty()) { m_error = std::to_string(line) + " 行目: " + why; }
		return false;
	}

	DialogueOp& emit(DialogueOpKind kind, int line)
	{
		DialogueOp op;
		op.kind = kind;
		op.line = line;
		m_out.ops.push_back(std::move(op));
		return m_out.ops.back();
	}

	[[nodiscard]] std::uint16_t here() const noexcept { return static_cast<std::uint16_t>(m_out.ops.size()); }

	ExprRef compileExpr(std::string_view src, int line)
	{
		std::string why;
		const ExprRef ref = m_out.exprs.compile(src, m_names, why);
		if (!why.empty()) { fail(line, why); }
		return ref;
	}

	[[nodiscard]] std::string autoKey(const char* kind)
	{
		++m_counter;
		return m_out.name + "." + (m_label.empty() ? std::string("top") : m_label) + "." + kind + std::to_string(m_counter);
	}

	bool record(const script::LineRecord& r)
	{
		switch (r.kind)
		{
		case script::LineKind::Label:
			m_out.labels.emplace_back(r.label, here());
			m_label = r.label;
			m_counter = 0;
			return true;
		case script::LineKind::Text:    return line(r);
		case script::LineKind::Command: return command(r);
		default:                        return fail(r.line, "台本に項目の行は書けない");
		}
	}

	bool line(const script::LineRecord& r)
	{
		if (!r.directive.empty() && r.directive != "key") { return fail(r.line, "台詞の後ろに置けるのは @key だけ"); }
		DialogueOp& op = emit(DialogueOpKind::Line, r.line);
		op.speaker = r.speaker;
		op.source = r.text;
		op.text = r.text;
		op.key = r.directive == "key" ? r.directiveArg : autoKey("");
		return true;
	}

	bool command(const script::LineRecord& r)
	{
		const std::string& d = r.directive;
		const std::string_view arg = i18n::trim(r.directiveArg);
		if (d == "choice") { return choice(arg, r.line); }
		if (d == "jump" || d == "call")
		{
			const std::size_t at = m_out.ops.size();
			emit(d == "jump" ? DialogueOpKind::Jump : DialogueOpKind::Call, r.line);
			m_fixups.push_back({at, std::string(arg), r.line});
			return !arg.empty() || fail(r.line, "@" + d + " の行き先が無い");
		}
		if (d == "if" || d == "elif" || d == "else" || d == "endif") { return branch(d, arg, r.line); }
		if (d == "set") { return set(arg, r.line); }
		if (d == "return") { emit(DialogueOpKind::Return, r.line); return true; }
		if (d == "end") { emit(DialogueOpKind::End, r.line); return true; }
		if (d == "cue") { return cue(r); }
		return fail(r.line, "@" + d + " は台詞の後ろにだけ書ける");
	}

	/// @brief 「文言 -> ラベル [key=鍵] [if 条件]」
	bool choice(std::string_view arg, int lineNo)
	{
		const std::size_t arrow = arg.find("->");
		if (arrow == std::string_view::npos) { return fail(lineNo, "@choice は「文言 -> ラベル」と書く"); }
		std::string_view rest = i18n::trim(arg.substr(arrow + 2));
		std::string_view cond;
		if (const std::size_t ifAt = rest.find(" if "); ifAt != std::string_view::npos)
		{
			cond = rest.substr(ifAt + 4);
			rest = i18n::trim(rest.substr(0, ifAt));
		}
		const auto tokens = script::tokenize(rest);
		if (tokens.empty()) { return fail(lineNo, "@choice の行き先が無い"); }
		const std::size_t at = m_out.ops.size();
		DialogueOp& op = emit(DialogueOpKind::Choice, lineNo);
		op.source = std::string(i18n::trim(arg.substr(0, arrow)));
		op.text = op.source;
		op.key = (tokens.size() > 1 && tokens[1].substr(0, 4) == "key=") ? std::string(tokens[1].substr(4)) : autoKey("c");
		m_fixups.push_back({at, std::string(tokens[0]), lineNo});
		m_out.ops[at].expr = compileExpr(cond, lineNo);
		return m_error.empty();
	}

	bool branch(const std::string& d, std::string_view arg, int lineNo)
	{
		if (d == "if")
		{
			m_ifs.push_back(IfBlock{std::nullopt, {}, lineNo});
			return condition(arg, lineNo);
		}
		if (m_ifs.empty()) { return fail(lineNo, "@" + d + " の前に @if が無い"); }
		IfBlock& b = m_ifs.back();
		if (d == "endif")
		{
			if (b.pendingFalse) { m_out.ops[*b.pendingFalse].target = here(); }
			for (const std::size_t j : b.exits) { m_out.ops[j].target = here(); }
			m_ifs.pop_back();
			return true;
		}
		if (!b.pendingFalse) { return fail(lineNo, "@else の後に @" + d + " は書けない"); }
		b.exits.push_back(m_out.ops.size());
		emit(DialogueOpKind::Jump, lineNo);
		m_out.ops[*b.pendingFalse].target = here();
		b.pendingFalse.reset();
		return d == "else" || condition(arg, lineNo);
	}

	bool condition(std::string_view arg, int lineNo)
	{
		if (arg.empty()) { return fail(lineNo, "条件が無い"); }
		m_ifs.back().pendingFalse = m_out.ops.size();
		const ExprRef ref = compileExpr(arg, lineNo);
		emit(DialogueOpKind::JumpIfNot, lineNo).expr = ref;
		return m_error.empty();
	}

	/// @brief 「名前 = 式」
	bool set(std::string_view arg, int lineNo)
	{
		const std::size_t eq = arg.find('=');
		if (eq == std::string_view::npos) { return fail(lineNo, "@set は「名前 = 式」と書く"); }
		const std::string_view varName = i18n::trim(arg.substr(0, eq));
		if (varName.empty()) { return fail(lineNo, "@set の名前が無い"); }
		const ExprRef value = compileExpr(arg.substr(eq + 1), lineNo);
		if (m_error.empty() && value.count == 0) { return fail(lineNo, "@set の式が無い"); }
		DialogueOp& op = emit(DialogueOpKind::Set, lineNo);
		op.var = m_names.intern(varName);
		op.expr = value;
		return m_names.error().empty() || fail(lineNo, m_names.error());
	}

	/// @brief 「種類 名前 [actor=..] [value=..] [duration=..]」
	bool cue(const script::LineRecord& r)
	{
		const auto tokens = script::tokenize(r.directiveArg);
		const auto kind = tokens.empty() ? std::nullopt : parseCueKind(tokens[0]);
		if (!kind) { return fail(r.line, "@cue の種類は sound / music / vfx / mark / call / anim"); }
		const std::string name = (tokens.size() > 1 && tokens[1].find('=') == std::string_view::npos) ? std::string(tokens[1]) : "";
		std::string actor;
		float value = 1.0f, duration = 0.0f;
		for (const auto& [k, v] : r.params)
		{
			if (k == "actor") { actor = v; }
			else if (k == "value") { value = std::strtof(v.c_str(), nullptr); }
			else if (k == "duration") { duration = std::strtof(v.c_str(), nullptr); }
		}
		emit(DialogueOpKind::CueOp, r.line).cue = makeCue(*kind, name, actor, value, duration);
		return true;
	}

	void resolve()
	{
		for (const Fixup& f : m_fixups)
		{
			const int at = m_out.findLabel(f.label);
			if (at < 0) { fail(f.line, "ラベル *" + f.label + " が無い"); return; }
			m_out.ops[f.op].target = static_cast<std::uint16_t>(at);
		}
	}

	DialogueScript&      m_out;
	NarrativeNames       m_names;
	std::vector<Fixup>   m_fixups;
	std::vector<IfBlock> m_ifs;
	std::string          m_label;
	int                  m_counter = 0;
	std::string          m_error;
};

[[nodiscard]] inline script::LineScript makeReader()
{
	script::LineScript ls;
	ls.allowText(true);
	for (const char* d : {"choice", "jump", "call", "return", "end", "if", "elif", "else", "endif", "set", "cue", "key"})
	{
		ls.directive(d);
	}
	return ls;
}

}  // namespace dialogue_detail

/// @brief 台本の文字列から読む。name は自動の訳の鍵 (`<name>.<ラベル>.<番号>`) と照合に使う
[[nodiscard]] inline std::optional<DialogueScript> loadDialogueFromText(std::string_view text, std::string_view name,
                                                                      std::string& error)
{
	script::LineScript ls = dialogue_detail::makeReader();
	if (!ls.parse(text)) { error = std::string(name) + ": " + ls.error(); return std::nullopt; }
	DialogueScript out;
	out.name = std::string(name);
	out.id = nameHash(out.name);
	dialogue_detail::Compiler compiler(out);
	if (!compiler.run(ls.records(), error)) { error = std::string(name) + ": " + error; return std::nullopt; }
	return out;
}

/// @brief ファイルから読む (vfs::readAsset)。名前はファイル名から拡張子を除いたもの
[[nodiscard]] inline std::optional<DialogueScript> loadDialogueFile(std::string_view path, std::string& error)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes) { error = "会話の台本を読めない: " + std::string(path); return std::nullopt; }
	std::string stem = std::filesystem::path{std::string(path)}.filename().string();
	stem = stem.substr(0, stem.find('.'));
	return loadDialogueFromText(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), stem, error);
}

}  // namespace mitiru::narrative
