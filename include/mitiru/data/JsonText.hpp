#pragma once

/// @file JsonText.hpp
/// @brief 書式をそのまま残して JSON の一部だけを書き換える。
/// @details 手で整えた JSON (空白、改行、1 行に収めた配列、数の書き方) を、書き換えた所以外は 1 byte も変えずに書き戻す。
///          git の差分が書き換えた行だけになる。ツール窓 (stage_edit) と、調整値の窓が使う。
///          読んだ文書は木で持つ。触っていない部分木は元の文字列をそのまま出し、触った木だけを
///          「元の空白 + 子の文字列」で組み直す。位置は JSON Pointer 風の "boxes/3/min/0" (配列は 0 からの番号)。
///          コメントと末尾のカンマは JSON でないので読まない。

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::data
{

class JsonText
{
public:
	enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };

	/// 新しく作る部分木の書式。Inline は 1 行 (`{"at": [0.0, 1.1]}`)、Block は 1 要素 1 行 (Python の indent=1 と同じ)
	enum class Style : std::uint8_t { Inline, Block };

	struct Node;

	struct Member
	{
		std::string lead;        ///< 直前の "," か開き括弧の後ろから、この要素の頭までの空白
		std::string key;         ///< 鍵の元の文字列 (引用符つき)。配列は空
		std::string colon;       ///< 鍵の終わりから値の頭まで (": ")。配列は空
		std::string tail;        ///< 値の終わりから "," まで。最後の要素の分は closeGap に入る
		std::shared_ptr<Node> value;
	};

	struct Node
	{
		Kind kind = Kind::Null;
		std::string raw;                 ///< 数・文字列・真偽・null の元の文字列
		std::vector<Member> members;
		std::string closeGap;            ///< 最後の要素の後ろから閉じ括弧まで
		std::size_t srcBegin = 0;        ///< 元の文字列の中の範囲。dirty でなければこれをそのまま出す
		std::size_t srcEnd = 0;
		bool dirty = false;
		bool fromSource = false;         ///< 元の文字列に範囲がある (新しく作った木は false)

		[[nodiscard]] bool isContainer() const noexcept { return kind == Kind::Array || kind == Kind::Object; }
	};

	JsonText() = default;
	JsonText(JsonText&&) noexcept = default;
	JsonText& operator=(JsonText&&) noexcept = default;
	JsonText(const JsonText& other)
		: m_src(other.m_src), m_headGap(other.m_headGap), m_tail(other.m_tail), m_newline(other.m_newline), m_bom(other.m_bom)
		, m_root(other.m_root ? std::make_shared<Node>(deepCopy(*other.m_root)) : nullptr) {}
	JsonText& operator=(const JsonText& other)
	{
		if (this != &other) { *this = JsonText(other); }
		return *this;
	}

	/// @return 読めなければ nullopt。error に「N 行目: 内容」
	[[nodiscard]] static std::optional<JsonText> parse(std::string_view text, std::string& error)
	{
		JsonText doc;
		doc.m_src = std::string(text);
		std::size_t pos = 0;
		if (doc.m_src.compare(0, 3, "\xEF\xBB\xBF") == 0) { doc.m_bom = true; pos = 3; }
		doc.m_newline = doc.m_src.find("\r\n") != std::string::npos ? "\r\n" : "\n";
		Parser parser{ doc.m_src, pos, {} };
		parser.skipSpace();
		doc.m_headGap = doc.m_src.substr(pos, parser.pos - pos);
		doc.m_root = parser.value();
		if (!parser.error.empty()) { error = parser.error; return std::nullopt; }
		const std::size_t tailBegin = parser.pos;
		parser.skipSpace();
		if (parser.pos != doc.m_src.size())
		{
			error = parser.where() + "JSON の後ろに余分な文字がある";
			return std::nullopt;
		}
		doc.m_tail = doc.m_src.substr(tailBegin);
		return doc;
	}

	[[nodiscard]] std::string render() const
	{
		std::string out = m_bom ? "\xEF\xBB\xBF" : "";
		out += m_headGap;
		appendNode(out, *m_root);
		out += m_tail;
		return out;
	}

	[[nodiscard]] const Node* find(std::string_view pointer) const { return const_cast<JsonText*>(this)->mutableFind(pointer); }
	[[nodiscard]] bool has(std::string_view pointer) const { return find(pointer) != nullptr; }

	/// 部分木を nlohmann の値にする。位置が無ければ null
	[[nodiscard]] nlohmann::ordered_json value(std::string_view pointer) const
	{
		const Node* n = find(pointer);
		return n == nullptr ? nlohmann::ordered_json() : toValue(*n);
	}

	[[nodiscard]] std::size_t size(std::string_view pointer) const
	{
		const Node* n = find(pointer);
		return n != nullptr && n->isContainer() ? n->members.size() : 0;
	}

	/// 数を書き換える。値が同じなら元の書き方のまま。元が整数の書き方で、新しい値も整数なら整数のまま
	bool setNumber(std::string_view pointer, double v)
	{
		Node* n = touch(pointer);
		return n != nullptr && assignNumber(*n, v);
	}

	bool setString(std::string_view pointer, const std::string& v)
	{
		Node* n = touch(pointer);
		if (n == nullptr || n->kind != Kind::String) { return false; }
		n->raw = quote(v);
		return true;
	}

	bool setBool(std::string_view pointer, bool v)
	{
		Node* n = touch(pointer);
		if (n == nullptr || n->kind != Kind::Bool) { return false; }
		n->raw = v ? "true" : "false";
		return true;
	}

	/// 配列の index の位置へ入れる (index が size 以上なら末尾)。空白は兄弟のものを写す
	bool insertAt(std::string_view arrayPointer, std::size_t index, std::shared_ptr<Node> node)
	{
		Node* arr = touch(arrayPointer);
		if (arr == nullptr || arr->kind != Kind::Array || node == nullptr) { return false; }
		index = std::min(index, arr->members.size());
		Member m;
		m.value = std::move(node);
		if (arr->members.empty())
		{
			const std::string indent = indentOf(arrayPointer);
			m.lead = m_newline + indent + unit();
			arr->closeGap = m_newline + indent;
		}
		else if (index == 0)
		{
			m.lead = arr->members.front().lead;
			arr->members.front().lead = arr->members.size() > 1 ? arr->members[1].lead : (m.lead.empty() ? std::string(" ") : m.lead);
		}
		else
		{
			const std::string& prev = arr->members[index - 1].lead;
			m.lead = prev.empty() && arr->members.size() > 1 ? arr->members[1].lead : prev;
		}
		arr->members.insert(arr->members.begin() + static_cast<std::ptrdiff_t>(index), std::move(m));
		return true;
	}

	/// 配列の要素か、オブジェクトの鍵を消す
	bool erase(std::string_view containerPointer, std::size_t index)
	{
		Node* c = touch(containerPointer);
		if (c == nullptr || !c->isContainer() || index >= c->members.size()) { return false; }
		if (index == 0 && c->members.size() > 1) { c->members[1].lead = c->members[0].lead; }
		c->members.erase(c->members.begin() + static_cast<std::ptrdiff_t>(index));
		if (c->members.empty()) { c->closeGap.clear(); }
		return true;
	}

	/// オブジェクトの鍵を入れる (あれば値を置き換える)。無ければ末尾に足す
	bool setMember(std::string_view objectPointer, const std::string& key, std::shared_ptr<Node> node)
	{
		Node* obj = touch(objectPointer);
		if (obj == nullptr || obj->kind != Kind::Object || node == nullptr) { return false; }
		for (Member& m : obj->members)
		{
			if (unquote(m.key) == key) { m.value = std::move(node); return true; }
		}
		Member m;
		m.key = quote(key);
		m.value = std::move(node);
		if (obj->members.empty())
		{
			const std::string indent = indentOf(objectPointer);
			m.lead = m_newline + indent + unit();
			m.colon = ": ";
			obj->closeGap = m_newline + indent;
		}
		else
		{
			m.lead = obj->members.back().lead;
			m.colon = obj->members.back().colon;
		}
		obj->members.push_back(std::move(m));
		return true;
	}

	bool eraseMember(std::string_view objectPointer, const std::string& key)
	{
		const Node* obj = find(objectPointer);
		if (obj == nullptr || obj->kind != Kind::Object) { return false; }
		for (std::size_t i = 0; i < obj->members.size(); ++i)
		{
			if (unquote(obj->members[i].key) == key) { return erase(objectPointer, i); }
		}
		return false;
	}

	/// pointer の部分木の複製 (複製の中を書き換えても元は変わらない)
	[[nodiscard]] std::shared_ptr<Node> clone(std::string_view pointer) const
	{
		const Node* n = find(pointer);
		return n == nullptr ? nullptr : std::make_shared<Node>(deepCopy(*n));
	}

	/// 複製の中の数を書き換える (insertAt する前の下ごしらえ)
	static bool setNumberIn(Node& root, std::string_view pointer, double v)
	{
		Node* n = walk(root, pointer, true);
		return n != nullptr && assignNumber(*n, v);
	}

	/// to の部分木の整数の数を、from の同じ位置の数が小数の書き方なら ".0" をつけた書き方にそろえる
	/// (小数で書き並べたファイルへ新しく足した要素の数が、1 つだけ整数の書き方で浮くのを防ぐ)
	void copyNumberStyle(std::string_view toPointer, std::string_view fromPointer)
	{
		Node* to = touch(toPointer);
		const Node* from = find(fromPointer);
		if (to != nullptr && from != nullptr) { matchStyle(*to, *from); }
	}

	/// 値から新しい部分木を作る。arrayPointer は入れ先の配列 (Block の字下げをその位置から決める)
	[[nodiscard]] std::shared_ptr<Node> make(const nlohmann::ordered_json& v, Style style, std::string_view arrayPointer) const
	{
		return build(v, style, indentOf(arrayPointer) + unit());
	}

	/// 数の書き方。整数で表せて元も整数なら整数、そうでなければ 4 桁までの最短の小数
	[[nodiscard]] static std::string formatNumber(double v, std::string_view original)
	{
		if (!std::isfinite(v)) { return "0"; }
		const bool origFraction = original.find_first_of(".eE") != std::string_view::npos;
		const double rounded = std::round(v * 10000.0) / 10000.0;
		if (std::fabs(rounded - std::round(rounded)) < 1e-9 && std::fabs(rounded) < 1e15)
		{
			const long long whole = static_cast<long long>(std::llround(rounded));
			return std::to_string(whole) + (origFraction ? ".0" : "");
		}
		char buf[64];
		const auto res = std::to_chars(buf, buf + sizeof(buf), rounded);
		return std::string(buf, res.ptr);
	}

	/// 入れ物の中身が改行で並んでいるか (1 行に収めた配列は false)。空なら nullopt
	[[nodiscard]] std::optional<bool> multiline(std::string_view pointer) const
	{
		const Node* n = find(pointer);
		if (n == nullptr || !n->isContainer() || n->members.empty()) { return std::nullopt; }
		return n->members.front().lead.find('\n') != std::string::npos;
	}

	[[nodiscard]] const std::string& newline() const noexcept { return m_newline; }

private:
	struct Parser
	{
		const std::string& s;
		std::size_t pos;
		std::string error;

		void skipSpace()
		{
			while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) { ++pos; }
		}

		[[nodiscard]] std::string where() const
		{
			std::size_t line = 1;
			for (std::size_t i = 0; i < pos && i < s.size(); ++i) { if (s[i] == '\n') { ++line; } }
			return std::to_string(line) + " 行目: ";
		}

		bool fail(const char* what)
		{
			if (error.empty()) { error = where() + what; }
			return false;
		}

		std::shared_ptr<Node> value()
		{
			auto node = std::make_shared<Node>();
			node->fromSource = true;
			node->srcBegin = pos;
			if (pos >= s.size()) { fail("値が途中で終わっている"); return node; }
			const char c = s[pos];
			if (c == '{') { container(*node, Kind::Object, '}'); }
			else if (c == '[') { container(*node, Kind::Array, ']'); }
			else if (c == '"') { node->kind = Kind::String; scanString(); }
			else if (c == 't' || c == 'f' || c == 'n') { literal(*node); }
			else if (c == '-' || (c >= '0' && c <= '9')) { node->kind = Kind::Number; scanNumber(); }
			else { fail("ここに値は置けない"); }
			node->srcEnd = pos;
			if (!node->isContainer()) { node->raw = s.substr(node->srcBegin, node->srcEnd - node->srcBegin); }
			return node;
		}

		void container(Node& node, Kind kind, char close)
		{
			node.kind = kind;
			++pos;
			bool afterComma = false;
			while (error.empty())
			{
				const std::size_t gapBegin = pos;
				skipSpace();
				if (pos >= s.size()) { fail("閉じ括弧が無い"); return; }
				if (s[pos] == close)
				{
					if (afterComma) { fail("最後の要素の後ろにカンマがある"); return; }
					node.closeGap = s.substr(gapBegin, pos - gapBegin);
					++pos;
					return;
				}
				Member m;
				m.lead = s.substr(gapBegin, pos - gapBegin);
				if (kind == Kind::Object) { if (!member(m)) { return; } }
				else { m.value = value(); }
				if (!error.empty()) { return; }
				const std::size_t tailBegin = pos;
				skipSpace();
				if (pos >= s.size()) { fail("閉じ括弧が無い"); return; }
				const bool comma = s[pos] == ',';
				if (!comma && s[pos] != close) { fail("要素の区切りは , か閉じ括弧"); return; }
				if (comma) { m.tail = s.substr(tailBegin, pos - tailBegin); }
				else { node.closeGap = s.substr(tailBegin, pos - tailBegin); }
				node.members.push_back(std::move(m));
				++pos;
				if (!comma) { return; }
				afterComma = true;
			}
		}

		bool member(Member& m)
		{
			if (pos >= s.size() || s[pos] != '"') { return fail("鍵は \"...\" で書く"); }
			const std::size_t keyBegin = pos;
			scanString();
			m.key = s.substr(keyBegin, pos - keyBegin);
			const std::size_t colonBegin = pos;
			skipSpace();
			if (pos >= s.size() || s[pos] != ':') { return fail("鍵の後ろに : が無い"); }
			++pos;
			skipSpace();
			m.colon = s.substr(colonBegin, pos - colonBegin);
			m.value = value();
			return error.empty();
		}

		void scanString()
		{
			++pos;
			while (pos < s.size() && s[pos] != '"')
			{
				if (s[pos] == '\\') { ++pos; }
				++pos;
			}
			if (pos >= s.size()) { fail("文字列が閉じていない"); return; }
			++pos;
		}

		void scanNumber()
		{
			const std::size_t begin = pos;
			if (s[pos] == '-') { ++pos; }
			const std::size_t digits = pos;
			while (pos < s.size() && ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' || s[pos] == '-')) { ++pos; }
			if (pos == digits || pos == begin) { fail("数が読めない"); }
		}

		void literal(Node& node)
		{
			for (const auto& [word, kind] : { std::pair<std::string_view, Kind>{ "true", Kind::Bool }, { "false", Kind::Bool }, { "null", Kind::Null } })
			{
				if (s.compare(pos, word.size(), word) == 0) { node.kind = kind; pos += word.size(); return; }
			}
			fail("true / false / null のどれかを書く");
		}
	};

	static std::string quote(const std::string& s) { return nlohmann::json(s).dump(-1, ' ', false); }

	static std::string unquote(std::string_view quoted)
	{
		const auto j = nlohmann::json::parse(quoted, nullptr, false);
		return j.is_string() ? j.get<std::string>() : std::string(quoted);
	}

	static bool assignNumber(Node& n, double v)
	{
		if (n.kind != Kind::Number) { return false; }
		const nlohmann::json old = nlohmann::json::parse(n.raw, nullptr, false);
		if (old.is_number() && std::fabs(old.get<double>() - v) < 1e-9) { return true; }
		n.raw = formatNumber(v, n.raw);
		return true;
	}

	static void matchStyle(Node& to, const Node& from)
	{
		if (to.kind == Kind::Number && from.kind == Kind::Number)
		{
			const bool fromFraction = from.raw.find_first_of(".eE") != std::string::npos;
			const bool toFraction = to.raw.find_first_of(".eE") != std::string::npos;
			if (fromFraction && !toFraction) { to.raw += ".0"; }
			return;
		}
		if (to.kind != from.kind || !to.isContainer()) { return; }
		to.dirty = true;
		for (std::size_t i = 0; i < to.members.size() && i < from.members.size(); ++i) { matchStyle(*to.members[i].value, *from.members[i].value); }
	}

	static Node deepCopy(const Node& n)
	{
		Node out = n;
		for (std::size_t i = 0; i < out.members.size(); ++i)
		{
			out.members[i].value = std::make_shared<Node>(deepCopy(*n.members[i].value));
		}
		return out;
	}

	static nlohmann::ordered_json toValue(const Node& n)
	{
		switch (n.kind)
		{
		case Kind::Array:
		{
			auto arr = nlohmann::ordered_json::array();
			for (const Member& m : n.members) { arr.push_back(toValue(*m.value)); }
			return arr;
		}
		case Kind::Object:
		{
			auto obj = nlohmann::ordered_json::object();
			for (const Member& m : n.members) { obj[unquote(m.key)] = toValue(*m.value); }
			return obj;
		}
		default:
			return nlohmann::ordered_json::parse(n.raw, nullptr, false);
		}
	}

	void appendNode(std::string& out, const Node& n) const
	{
		if (!n.isContainer()) { out += n.raw; return; }
		if (!n.dirty && n.fromSource) { out.append(m_src, n.srcBegin, n.srcEnd - n.srcBegin); return; }
		out += n.kind == Kind::Array ? '[' : '{';
		for (std::size_t i = 0; i < n.members.size(); ++i)
		{
			const Member& m = n.members[i];
			out += m.lead;
			if (n.kind == Kind::Object) { out += m.key; out += m.colon; }
			appendNode(out, *m.value);
			if (i + 1 < n.members.size()) { out += m.tail; out += ','; }
		}
		out += n.closeGap;
		out += n.kind == Kind::Array ? ']' : '}';
	}

	static std::vector<std::string> split(std::string_view pointer)
	{
		std::vector<std::string> parts;
		std::size_t start = 0;
		while (start < pointer.size())
		{
			const std::size_t end = std::min(pointer.find('/', start), pointer.size());
			if (end > start) { parts.emplace_back(pointer.substr(start, end - start)); }
			start = end + 1;
		}
		return parts;
	}

	static Member* memberOf(Node& n, const std::string& token)
	{
		if (n.kind == Kind::Array)
		{
			std::size_t idx = 0;
			const auto res = std::from_chars(token.data(), token.data() + token.size(), idx);
			if (res.ec != std::errc() || res.ptr != token.data() + token.size() || idx >= n.members.size()) { return nullptr; }
			return &n.members[idx];
		}
		if (n.kind == Kind::Object)
		{
			for (Member& m : n.members) { if (unquote(m.key) == token) { return &m; } }
		}
		return nullptr;
	}

	// markDirty: 通った入れ物を全部「組み直す」印にする
	static Node* walk(Node& root, std::string_view pointer, bool markDirty)
	{
		Node* cur = &root;
		if (markDirty && cur->isContainer()) { cur->dirty = true; }
		for (const std::string& token : split(pointer))
		{
			Member* m = memberOf(*cur, token);
			if (m == nullptr) { return nullptr; }
			cur = m->value.get();
			if (markDirty && cur->isContainer()) { cur->dirty = true; }
		}
		return cur;
	}

	Node* mutableFind(std::string_view pointer) { return m_root ? walk(*m_root, pointer, false) : nullptr; }
	Node* touch(std::string_view pointer) { return m_root ? walk(*m_root, pointer, true) : nullptr; }

	// pointer の要素が書かれている行の字下げ (その要素の lead の最後の改行より後ろ)
	[[nodiscard]] std::string indentOf(std::string_view pointer) const
	{
		Node* cur = m_root.get();
		std::string indent;
		for (const std::string& token : split(pointer))
		{
			const Member* hit = cur != nullptr ? memberOf(*cur, token) : nullptr;
			if (hit == nullptr) { break; }
			const std::size_t nl = hit->lead.rfind('\n');
			if (nl != std::string::npos) { indent = hit->lead.substr(nl + 1); }
			cur = hit->value.get();
		}
		return indent;
	}

	// 一段ぶんの字下げ。ルート直下の最初の要素の字下げから取る (Python の indent=1 なら 1 字)
	[[nodiscard]] std::string unit() const
	{
		if (m_root && m_root->isContainer() && !m_root->members.empty())
		{
			const std::string& lead = m_root->members.front().lead;
			const std::size_t nl = lead.rfind('\n');
			if (nl != std::string::npos && lead.size() > nl + 1) { return lead.substr(nl + 1); }
		}
		return " ";
	}

	[[nodiscard]] std::shared_ptr<Node> build(const nlohmann::ordered_json& v, Style style, const std::string& indent) const
	{
		auto node = std::make_shared<Node>();
		if (v.is_array() || v.is_object())
		{
			node->kind = v.is_array() ? Kind::Array : Kind::Object;
			const bool block = style == Style::Block;
			const std::string inner = indent + unit();
			const auto add = [&](const std::string& key, const nlohmann::ordered_json& child) {
				Member m;
				m.lead = block ? m_newline + inner : (node->members.empty() ? std::string() : std::string(" "));
				if (v.is_object()) { m.key = quote(key); m.colon = ": "; }
				m.value = build(child, style, inner);
				node->members.push_back(std::move(m));
			};
			if (v.is_array()) { for (const auto& e : v) { add({}, e); } }
			else { for (auto it = v.begin(); it != v.end(); ++it) { add(it.key(), it.value()); } }
			if (block && !node->members.empty()) { node->closeGap = m_newline + indent; }
			node->dirty = true;
			return node;
		}
		if (v.is_number())
		{
			node->kind = Kind::Number;
			node->raw = v.is_number_integer() ? std::to_string(v.get<long long>()) : formatNumber(v.get<double>(), "0.0");
		}
		else if (v.is_string()) { node->kind = Kind::String; node->raw = quote(v.get<std::string>()); }
		else if (v.is_boolean()) { node->kind = Kind::Bool; node->raw = v.get<bool>() ? "true" : "false"; }
		else { node->kind = Kind::Null; node->raw = "null"; }
		return node;
	}

	std::string m_src;
	std::string m_headGap;
	std::string m_tail;
	std::string m_newline = "\n";
	bool m_bom = false;
	std::shared_ptr<Node> m_root;
};

} // namespace mitiru::data
