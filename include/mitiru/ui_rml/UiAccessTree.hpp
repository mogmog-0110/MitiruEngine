#pragma once

/// @file UiAccessTree.hpp
/// @brief RML の要素の木を、読み上げソフトに見せる role と name の木に直す純関数。
/// @details RmlUi にも Win32 にも AccessKit にも依存しない。入力は要素ごとの事実 (ElementFacts)、出力は
///          pre-order の節の列。どの要素がどの role になるかの規則はここだけにあり、単体で試せる。
///          UI は状態の持ち主ではないので、木は読み取り専用で、操作は AccessRequest として host が受ける。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::ui_rml
{

enum class AccessRole : std::uint8_t
{
	Window, Group, Label, Heading, Button, Link, CheckBox, RadioButton, Slider, ProgressBar, ComboBox,
	TextInput, PasswordInput, TabList, Tab, TabPanel, Image, List, ListItem, Dialog,
};

enum class AccessLive : std::uint8_t { Off, Polite, Assertive };
enum class AccessToggle : std::uint8_t { None, Off, On };

/// 節が受けられる操作。bit で持つ。
namespace access_action
{
inline constexpr std::uint8_t kFocus = 1;
inline constexpr std::uint8_t kClick = 2;
inline constexpr std::uint8_t kSetValue = 4;
inline constexpr std::uint8_t kIncrement = 8;
inline constexpr std::uint8_t kDecrement = 16;
}

/// 論理解像度 (UI の座標) の矩形。窓の画素への倍率は、橋の側が掛ける。
struct AccessRect
{
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	[[nodiscard]] bool operator==(const AccessRect&) const noexcept = default;
};

struct AccessNode
{
	std::uint64_t id = 0;
	AccessRole role = AccessRole::Group;
	std::string name;
	std::string value;
	AccessLive live = AccessLive::Off;
	AccessToggle toggle = AccessToggle::None;
	bool disabled = false;
	bool selected = false;
	bool modal = false;
	bool hasRange = false;
	double rangeMin = 0.0;
	double rangeMax = 0.0;
	double rangeStep = 0.0;
	double rangeValue = 0.0;
	int level = 0;
	std::uint8_t actions = 0;
	AccessRect bounds;
	std::vector<std::uint64_t> children;
	[[nodiscard]] bool operator==(const AccessNode&) const = default;
};

inline constexpr std::uint64_t kAccessRootId = 1;

struct UiAccessTree
{
	std::vector<AccessNode> nodes;   ///< pre-order。nodes[0] が根 (Window)
	std::uint64_t focus = kAccessRootId;
	float logicalWidth = 0.0f;    ///< bounds の座標系の大きさ (UI の論理解像度)。窓の画素へ伸ばす倍率の元
	float logicalHeight = 0.0f;

	[[nodiscard]] bool operator==(const UiAccessTree&) const = default;
	[[nodiscard]] bool empty() const noexcept { return nodes.empty(); }
	[[nodiscard]] const AccessNode* find(std::uint64_t id) const noexcept
	{
		for (const AccessNode& n : nodes) { if (n.id == id) { return &n; } }
		return nullptr;
	}
};

/// 読み上げソフトからの操作の頼み。UI を直接書き換えず、フレームの中で host が要素に届ける。
struct AccessRequest
{
	enum class Kind : std::uint8_t { Focus, Click, SetValue, Increment, Decrement };
	Kind kind = Kind::Focus;
	std::uint64_t target = 0;
	std::string text;        ///< SetValue が文字のとき
	double number = 0.0;     ///< SetValue が数のとき
	bool hasNumber = false;
};

/// 要素 1 つの事実。RmlUi から読んだ属性と見えている文字を、role の規則に渡す形にしたもの。
struct ElementFacts
{
	std::uint64_t id = 0;            ///< 呼び出し側が払う安定した番号 (同じ要素は同じ番号)
	std::string tag;                 ///< 小文字。文字の連なりは "#text"
	std::string type;                ///< input の type
	std::string role;                ///< role 属性
	std::string ariaLabel;
	std::string title;
	std::string alt;
	std::string placeholder;
	std::string text;                ///< 文字の連なりのときの文字 (空白は 1 つにまとめる)
	std::string value;               ///< 入力欄・選択の現在の値
	std::string a11y;                ///< data-a11y ("polite" / "assertive")
	std::string ariaHidden;
	bool visible = true;
	bool disabled = false;
	bool checked = false;
	bool selected = false;
	bool readOnly = false;
	bool hasClick = false;           ///< data-event-click を持つ
	bool tabbable = false;           ///< キーで選べる (tab-index が効く)
	bool focused = false;
	bool focusTrap = false;
	bool hasRange = false;
	double rangeMin = 0.0;
	double rangeMax = 0.0;
	double rangeStep = 0.0;
	double rangeValue = 0.0;
	AccessRect bounds;
};

struct FactsNode
{
	ElementFacts facts;
	std::vector<FactsNode> children;
};

namespace detail
{

struct Built
{
	AccessNode node;
	std::vector<Built> kids;
	bool textRun = false;   ///< 文字の連なり 1 つから作った Label
};

struct Converted
{
	std::vector<Built> built;   ///< 0 個 (捨てる)、1 個 (節になる)、複数 (節にならず、子が親へ上がる)
	bool hasFocus = false;
};

[[nodiscard]] inline bool isHeadingTag(const std::string& t) noexcept
{
	return t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6';
}

/// role 属性の値から。決まらなければ nullptr
[[nodiscard]] inline const AccessRole* roleFromAttribute(const std::string& r, AccessRole& storage) noexcept
{
	struct Pair { const char* name; AccessRole role; };
	static constexpr Pair kTable[] = {
		{ "button", AccessRole::Button }, { "link", AccessRole::Link }, { "checkbox", AccessRole::CheckBox },
		{ "switch", AccessRole::CheckBox }, { "radio", AccessRole::RadioButton }, { "slider", AccessRole::Slider },
		{ "progressbar", AccessRole::ProgressBar }, { "tab", AccessRole::Tab }, { "tablist", AccessRole::TabList },
		{ "tabpanel", AccessRole::TabPanel }, { "dialog", AccessRole::Dialog }, { "alertdialog", AccessRole::Dialog },
		{ "heading", AccessRole::Heading }, { "list", AccessRole::List }, { "listitem", AccessRole::ListItem },
		{ "group", AccessRole::Group }, { "img", AccessRole::Image }, { "image", AccessRole::Image },
		{ "textbox", AccessRole::TextInput }, { "combobox", AccessRole::ComboBox },
	};
	for (const Pair& p : kTable)
	{
		if (r == p.name) { storage = p.role; return &storage; }
	}
	return nullptr;
}

/// 要素が持つ role。nullopt 相当は false を返す (役割の無い入れ物)
[[nodiscard]] inline bool classify(const ElementFacts& f, AccessRole& out) noexcept
{
	if (f.role == "none" || f.role == "presentation") { return false; }
	if (roleFromAttribute(f.role, out) != nullptr) { return true; }
	const std::string& t = f.tag;
	if (t == "button") { out = AccessRole::Button; return true; }
	if (t == "input")
	{
		if (f.type == "checkbox") { out = AccessRole::CheckBox; }
		else if (f.type == "radio") { out = AccessRole::RadioButton; }
		else if (f.type == "range") { out = AccessRole::Slider; }
		else if (f.type == "button" || f.type == "submit" || f.type == "reset") { out = AccessRole::Button; }
		else if (f.type == "password") { out = AccessRole::PasswordInput; }
		else { out = AccessRole::TextInput; }
		return true;
	}
	if (t == "textarea") { out = AccessRole::TextInput; return true; }
	if (t == "select") { out = AccessRole::ComboBox; return true; }
	if (t == "progress") { out = AccessRole::ProgressBar; return true; }
	if (t == "a") { out = AccessRole::Link; return true; }
	if (t == "img") { out = AccessRole::Image; return true; }
	if (isHeadingTag(t)) { out = AccessRole::Heading; return true; }
	if (t == "tab") { out = AccessRole::Tab; return true; }
	if (t == "tabs") { out = AccessRole::TabList; return true; }
	if (t == "panel") { out = AccessRole::TabPanel; return true; }
	if (t == "ul" || t == "ol") { out = AccessRole::List; return true; }
	if (t == "li") { out = AccessRole::ListItem; return true; }
	if (f.focusTrap) { out = AccessRole::Dialog; return true; }
	return false;
}

/// 中身を読み上げの名前にする役割。子の要素は節にしない (button の中の span など)。
[[nodiscard]] inline bool nameFromContent(AccessRole r) noexcept
{
	switch (r)
	{
	case AccessRole::Button: case AccessRole::Link: case AccessRole::CheckBox: case AccessRole::RadioButton:
	case AccessRole::Tab: case AccessRole::Heading: case AccessRole::ListItem: return true;
	default: return false;
	}
}

[[nodiscard]] inline std::string joinNames(const std::vector<Built>& kids)
{
	std::string out;
	for (const Built& k : kids)
	{
		std::string part = k.node.name;
		if (part.empty() && !k.kids.empty()) { part = joinNames(k.kids); }
		if (part.empty()) { continue; }
		if (!out.empty()) { out += ' '; }
		out += part;
	}
	return out;
}

[[nodiscard]] inline std::uint8_t actionsFor(AccessRole r, const ElementFacts& f) noexcept
{
	std::uint8_t a = 0;
	if (f.tabbable || r == AccessRole::TextInput || r == AccessRole::PasswordInput || r == AccessRole::Slider ||
	    r == AccessRole::ComboBox || r == AccessRole::CheckBox || r == AccessRole::RadioButton || r == AccessRole::Button)
	{
		a |= access_action::kFocus;
	}
	const bool clickable = f.hasClick || r == AccessRole::Button || r == AccessRole::Link || r == AccessRole::CheckBox ||
	                       r == AccessRole::RadioButton || r == AccessRole::Tab;
	if (clickable) { a |= access_action::kClick; }
	if ((r == AccessRole::TextInput || r == AccessRole::Slider || r == AccessRole::ComboBox) && !f.readOnly)
	{
		a |= access_action::kSetValue;
	}
	if (r == AccessRole::Slider) { a |= access_action::kIncrement | access_action::kDecrement; }
	if (f.disabled) { a = 0; }
	return a;
}

class TreeBuilder
{
public:
	[[nodiscard]] Converted convert(const FactsNode& n)
	{
		const ElementFacts& f = n.facts;
		Converted out;
		if (!f.visible || f.ariaHidden == "true") { return out; }
		if (f.tag == "#text")
		{
			if (f.text.empty()) { return out; }
			Built run = labelOf(f, f.text);
			run.textRun = true;
			out.built.push_back(std::move(run));
			return out;
		}
		std::vector<Built> kids;
		for (const FactsNode& c : n.children)
		{
			Converted r = convert(c);
			out.hasFocus = out.hasFocus || r.hasFocus;
			for (Built& b : r.built) { kids.push_back(std::move(b)); }
		}
		if (f.focused) { out.hasFocus = true; }

		AccessRole role{};
		const bool semantic = classify(f, role);
		if (!f.a11y.empty() && !semantic)
		{
			// data-a11y の要素は、文字そのものが読み上げの文 (段階 1 と同じ規則)。live region として残す
			Built b = labelOf(f, f.ariaLabel.empty() ? joinNames(kids) : f.ariaLabel);
			b.node.live = f.a11y == "assertive" ? AccessLive::Assertive : AccessLive::Polite;
			return finish(out, std::move(b), f, true);
		}
		if (semantic) { return finish(out, makeSemantic(f, role, std::move(kids)), f, nameFromContent(role)); }
		if (f.hasClick || f.tabbable)
		{
			// 押せる div や span は、役割を書いていなくても button として見せる (中身が空でも aria-label で名前を持てる)
			return finish(out, makeSemantic(f, AccessRole::Button, std::move(kids)), f, true);
		}
		if (kids.empty()) { return out; }
		if (std::all_of(kids.begin(), kids.end(), [](const Built& b) { return b.textRun; }))
		{
			// 文字の連なりだけを持つ入れ物 (p や span) は 1 つの Label にまとめて、読み上げの区切りを減らす
			Built b = labelOf(f, f.ariaLabel.empty() ? joinNames(kids) : f.ariaLabel);
			return finish(out, std::move(b), f, true);
		}
		if (!f.ariaLabel.empty())
		{
			Built g = makeSemantic(f, AccessRole::Group, std::move(kids));
			return finish(out, std::move(g), f, false);
		}
		out.built = std::move(kids);   // 役割の無い入れ物は節にせず、子を親へ上げる
		return out;
	}

	[[nodiscard]] UiAccessTree build(const std::string& windowName, const std::vector<FactsNode>& documents)
	{
		Built root;
		root.node.id = kAccessRootId;
		root.node.role = AccessRole::Window;
		root.node.name = windowName;
		for (const FactsNode& d : documents)
		{
			Converted r = convert(d);
			for (Built& b : r.built) { root.kids.push_back(std::move(b)); }
		}
		UiAccessTree tree;
		flatten(root, tree.nodes);
		tree.focus = kAccessRootId;
		const auto isFocus = [this](const AccessNode& n) { return n.id == m_focus; };
		if (m_focus != 0 && std::any_of(tree.nodes.begin(), tree.nodes.end(), isFocus))
		{
			tree.focus = m_focus;
		}
		return tree;
	}

private:
	std::uint64_t m_focus = 0;

	[[nodiscard]] static Built labelOf(const ElementFacts& f, std::string name)
	{
		Built b;
		b.node.id = f.id;
		b.node.role = AccessRole::Label;
		b.node.name = std::move(name);
		b.node.bounds = f.bounds;
		return b;
	}

	[[nodiscard]] Built makeSemantic(const ElementFacts& f, AccessRole role, std::vector<Built> kids) const
	{
		Built b;
		AccessNode& n = b.node;
		n.id = f.id;
		n.role = role;
		n.bounds = f.bounds;
		n.disabled = f.disabled;
		n.selected = f.selected;
		n.actions = actionsFor(role, f);
		if (role == AccessRole::CheckBox || role == AccessRole::RadioButton)
		{
			n.toggle = f.checked ? AccessToggle::On : AccessToggle::Off;
		}
		if (role == AccessRole::Heading) { n.level = isHeadingTag(f.tag) ? f.tag[1] - '0' : 2; }
		if (role == AccessRole::Dialog) { n.modal = f.focusTrap; }
		if (role == AccessRole::Slider || role == AccessRole::ProgressBar)
		{
			n.hasRange = f.hasRange;
			n.rangeMin = f.rangeMin;
			n.rangeMax = f.rangeMax;
			n.rangeStep = f.rangeStep;
			n.rangeValue = f.rangeValue;
		}
		if (role == AccessRole::TextInput || role == AccessRole::ComboBox) { n.value = f.value; }
		// パスワードは値を渡さない (読み上げソフトに文字を読ませない)
		const std::string fromAttributes = !f.ariaLabel.empty() ? f.ariaLabel : !f.title.empty() ? f.title : std::string();
		if (nameFromContent(role))
		{
			std::string content = joinNames(kids);
			if (!f.ariaLabel.empty()) { n.name = f.ariaLabel; }
			else if (!content.empty()) { n.name = content; }
			else if (!f.title.empty()) { n.name = f.title; }
			else if (!f.value.empty() && f.tag == "input" && (f.type == "button" || f.type == "submit" || f.type == "reset"))
			{
				n.name = f.value;   // 文字の無い input のボタンは、value が表示の文字
			}
			else { n.name = f.alt; }
			return b;   // 中身は名前に使い切り、子の節は作らない
		}
		if (role == AccessRole::Image)
		{
			n.name = !f.ariaLabel.empty() ? f.ariaLabel : !f.alt.empty() ? f.alt : f.title;
			return b;
		}
		n.name = !fromAttributes.empty() ? fromAttributes : f.placeholder;
		b.kids = std::move(kids);
		return b;
	}

	[[nodiscard]] Converted finish(Converted out, Built b, const ElementFacts& f, bool dropKids)
	{
		if (dropKids) { b.kids.clear(); }
		if (f.focused || (out.hasFocus && dropKids)) { m_focus = b.node.id; }
		out.built.clear();
		out.built.push_back(std::move(b));
		return out;
	}

	static void flatten(Built& b, std::vector<AccessNode>& nodes)
	{
		const std::size_t index = nodes.size();
		nodes.push_back(std::move(b.node));
		for (Built& k : b.kids)
		{
			nodes[index].children.push_back(k.node.id);
			flatten(k, nodes);
		}
	}
};

}  // namespace detail

/// @brief 要素の事実の木 (文書ごと) から、読み上げソフトに見せる節の木を作る。
/// @details 隠れた要素 (visible が偽、aria-hidden) は含めない。focused の要素が節にならなければ、根を選んだことにする。
///          節の番号 (id) は呼び出し側が要素ごとに払う。同じ要素が同じ番号を保つので、読み上げソフトのフォーカスが飛ばない。
[[nodiscard]] inline UiAccessTree buildAccessTree(
	const std::string& windowName, const std::vector<FactsNode>& documents)
{
	detail::TreeBuilder builder;
	return builder.build(windowName, documents);
}

/// @brief 木が AccessKit に渡せる形か確かめる。番号の重複、存在しない子、2 つの親を持つ節、根から届かない節があれば false。
/// @details AccessKit は形の崩れた木で panic し、C の境界を越えるとプロセスごと落ちる。渡す前にここで止める。
[[nodiscard]] inline bool isWellFormed(const UiAccessTree& tree)
{
	if (tree.nodes.empty() || tree.nodes.front().id != kAccessRootId) { return false; }
	std::unordered_map<std::uint64_t, std::size_t> index;
	index.reserve(tree.nodes.size());
	for (std::size_t i = 0; i < tree.nodes.size(); ++i)
	{
		if (!index.emplace(tree.nodes[i].id, i).second) { return false; }
	}
	if (index.find(tree.focus) == index.end()) { return false; }
	// 根から幅優先でたどる。根から切れた輪 (互いを子にする節) は届かないので、届いた数が全体に足りず false になる
	std::vector<char> reached(tree.nodes.size(), 0);
	std::vector<std::size_t> pending = { 0 };
	reached[0] = 1;
	std::size_t count = 1;
	while (!pending.empty())
	{
		const std::size_t at = pending.back();
		pending.pop_back();
		for (const std::uint64_t child : tree.nodes[at].children)
		{
			const auto it = index.find(child);
			if (it == index.end() || reached[it->second] != 0) { return false; }   // 無い子、または 2 つ目の親 (根を子にする輪も)
			reached[it->second] = 1;
			++count;
			pending.push_back(it->second);
		}
	}
	return count == tree.nodes.size();
}

/// 前に渡した木と比べて、変わった節 (新しい節と、中身か子の並びが変わった節) の番号。
struct AccessDelta
{
	std::vector<std::size_t> changed;   ///< next.nodes の添字
	bool focusChanged = false;
	bool rootChanged = false;
};

[[nodiscard]] inline AccessDelta diffAccessTree(const UiAccessTree* prev, const UiAccessTree& next)
{
	AccessDelta d;
	if (prev == nullptr || prev->empty())
	{
		for (std::size_t i = 0; i < next.nodes.size(); ++i) { d.changed.push_back(i); }
		d.focusChanged = true;
		d.rootChanged = true;
		return d;
	}
	std::unordered_map<std::uint64_t, std::size_t> before;
	before.reserve(prev->nodes.size());
	for (std::size_t i = 0; i < prev->nodes.size(); ++i) { before.emplace(prev->nodes[i].id, i); }
	for (std::size_t i = 0; i < next.nodes.size(); ++i)
	{
		const auto it = before.find(next.nodes[i].id);
		if (it == before.end() || !(prev->nodes[it->second] == next.nodes[i])) { d.changed.push_back(i); }
	}
	d.focusChanged = prev->focus != next.focus;
	d.rootChanged = prev->nodes.front().id != next.nodes.front().id;
	return d;
}

}  // namespace mitiru::ui_rml
