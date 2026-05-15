#pragma once

/// @file UIPrefab.hpp
/// @brief JSON駆動のUIプリファブシステム

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/UIDataBinding.hpp>

namespace mitiru::ui
{

// ── JSON簡易パーサーユーティリティ ──────────────────────────────

namespace prefab_detail
{

inline void skipWS(const std::string& s, std::size_t& p) noexcept
{
	while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
}

[[nodiscard]] inline std::string readStr(const std::string& s, std::size_t& p)
{
	if (p >= s.size() || s[p] != '"') return {};
	++p;
	std::string r;
	while (p < s.size() && s[p] != '"')
	{
		if (s[p] == '\\' && p + 1 < s.size()) { ++p; r += s[p]; }
		else { r += s[p]; }
		++p;
	}
	if (p < s.size()) ++p;
	return r;
}

[[nodiscard]] inline float readNum(const std::string& s, std::size_t& p)
{
	const std::size_t start = p;
	if (p < s.size() && s[p] == '-') ++p;
	while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
	if (p < s.size() && s[p] == '.') { ++p; while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p; }
	return std::stof(s.substr(start, p - start));
}

inline void skipVal(const std::string& s, std::size_t& p)
{
	skipWS(s, p);
	if (p >= s.size()) return;
	if (s[p] == '"') { readStr(s, p); return; }
	if (s[p] == '{' || s[p] == '[')
	{
		const char open = s[p], close = (open == '{') ? '}' : ']';
		int d = 1; ++p;
		while (p < s.size() && d > 0) { if (s[p] == open) ++d; else if (s[p] == close) --d; ++p; }
		return;
	}
	while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']') ++p;
}

[[nodiscard]] inline sgc::Colorf parseHexColor(const std::string& hex)
{
	sgc::Colorf c{0.0f, 0.0f, 0.0f, 1.0f};
	if (hex.size() < 7 || hex[0] != '#') return c;
	auto b = [&](std::size_t o) { return static_cast<float>(std::stoul(hex.substr(o, 2), nullptr, 16)) / 255.0f; };
	c.r = b(1); c.g = b(3); c.b = b(5);
	if (hex.size() >= 9) c.a = b(7);
	return c;
}

} // namespace prefab_detail

// ── プリファブノード定義 ────────────────────────────────────────

struct UIPrefabNodeDef
{
	std::string type;
	std::string name;
	std::string text;
	float value = 0.0f, maxValue = 1.0f;
	float width = 0.0f, height = 0.0f;
	std::string onClick;
	std::string backgroundColor;
	float cornerRadius = 0.0f;
	std::unordered_map<std::string, std::string> bindDefs;
	std::vector<UIPrefabNodeDef> children;
};

// ── UIPrefab ────────────────────────────────────────────────────

/// @brief JSONテンプレートからUINodeツリーを生成するプリファブ
/// @code
/// auto prefab = mitiru::ui::UIPrefab::loadFromJson(R"({
///   "type": "Panel",
///   "children": [
///     {"type": "Label", "text": "Score: {score}", "bind": {"text": "scoreText"}},
///     {"type": "Button", "text": "Attack", "onClick": "cmd:battle.attack"}
///   ]
/// })");
/// auto root = prefab.instantiate();
/// @endcode
class UIPrefab
{
	UIPrefabNodeDef m_root;
	static inline std::uint32_t s_nextId = 10000;

public:
	[[nodiscard]] static UIPrefab loadFromJson(const std::string& jsonStr)
	{
		UIPrefab prefab;
		std::size_t pos = 0;
		prefab.m_root = parseNodeDef(jsonStr, pos);
		return prefab;
	}

	explicit UIPrefab(UIPrefabNodeDef root) : m_root(std::move(root)) {}

	[[nodiscard]] std::shared_ptr<UINode> instantiate() const { return buildNode(m_root); }

	[[nodiscard]] std::shared_ptr<UINode> instantiateWithBindings(UIBindingContext& ctx) const
	{
		auto root = buildNode(m_root);
		applyBindings(*root, m_root, ctx);
		return root;
	}

	[[nodiscard]] const UIPrefabNodeDef& rootDef() const noexcept { return m_root; }

private:
	UIPrefab() = default;

	[[nodiscard]] static UIRole parseRole(const std::string& t)
	{
		if (t == "Panel" || t == "panel") return UIRole::Panel;
		if (t == "Label" || t == "label") return UIRole::Label;
		if (t == "Button" || t == "button") return UIRole::Button;
		if (t == "ProgressBar" || t == "progress_bar") return UIRole::ProgressBar;
		if (t == "HealthBar" || t == "health_bar") return UIRole::HealthBar;
		if (t == "Image" || t == "image") return UIRole::Image;
		if (t == "Container" || t == "container") return UIRole::Container;
		if (t == "Slider" || t == "slider") return UIRole::Slider;
		if (t == "Toggle" || t == "toggle") return UIRole::Toggle;
		if (t == "ListView" || t == "list_view") return UIRole::ListView;
		if (t == "TextInput" || t == "text_input") return UIRole::TextInput;
		return UIRole::Panel;
	}

	[[nodiscard]] static std::shared_ptr<UINode> buildNode(const UIPrefabNodeDef& def)
	{
		UINodeData data;
		data.id = static_cast<UINodeId>(s_nextId++);
		data.name = def.name;
		data.role = parseRole(def.type);
		data.text = def.text;
		data.value = def.value;
		data.maxValue = def.maxValue;
		if (def.width > 0.0f || def.height > 0.0f)
			data.bounds = sgc::Rectf{0.0f, 0.0f, def.width, def.height};
		if (!def.backgroundColor.empty())
			data.color = prefab_detail::parseHexColor(def.backgroundColor);

		auto node = std::make_shared<UINode>(std::move(data));
		if (!def.onClick.empty()) node->setProperty("onClick", def.onClick);
		for (const auto& child : def.children) node->addChild(buildNode(child));
		return node;
	}

	static void applyBindings(UINode& node, const UIPrefabNodeDef& def, UIBindingContext& ctx)
	{
		if (!def.text.empty() && def.text.find('{') != std::string::npos)
			ctx.bindExpression(node, UIBindProperty::Text, def.text);

		for (const auto& [prop, key] : def.bindDefs)
			ctx.bind(key, node, parseBindProp(prop));

		const auto& ch = node.children();
		for (std::size_t i = 0; i < ch.size() && i < def.children.size(); ++i)
			applyBindings(*ch[i], def.children[i], ctx);
	}

	[[nodiscard]] static UIBindProperty parseBindProp(const std::string& n)
	{
		if (n == "text") return UIBindProperty::Text;
		if (n == "value" || n == "max") return UIBindProperty::Value;
		if (n == "visible") return UIBindProperty::Visible;
		if (n == "color") return UIBindProperty::Color;
		if (n == "enabled") return UIBindProperty::Enabled;
		return UIBindProperty::Text;
	}

	// ── JSONパーサー ─────────────────────────────────────────

	[[nodiscard]] static UIPrefabNodeDef parseNodeDef(const std::string& s, std::size_t& p)
	{
		using namespace prefab_detail;
		UIPrefabNodeDef def;
		skipWS(s, p);
		if (p >= s.size() || s[p] != '{') return def;
		++p;
		while (p < s.size())
		{
			skipWS(s, p);
			if (p < s.size() && s[p] == '}') { ++p; break; }
			if (p < s.size() && s[p] == ',') { ++p; continue; }
			const auto key = readStr(s, p);
			skipWS(s, p);
			if (p < s.size() && s[p] == ':') ++p;
			skipWS(s, p);

			if      (key == "type")   def.type = readStr(s, p);
			else if (key == "name")   def.name = readStr(s, p);
			else if (key == "text")   def.text = readStr(s, p);
			else if (key == "onClick") def.onClick = readStr(s, p);
			else if (key == "value")  def.value = readNum(s, p);
			else if (key == "width")  def.width = readNum(s, p);
			else if (key == "height") def.height = readNum(s, p);
			else if (key == "maxValue") def.maxValue = readNum(s, p);
			else if (key == "cornerRadius") def.cornerRadius = readNum(s, p);
			else if (key == "style")  parseStyle(s, p, def);
			else if (key == "bind")   parseBind(s, p, def);
			else if (key == "children") parseChildren(s, p, def);
			else skipVal(s, p);
		}
		return def;
	}

	static void parseStyle(const std::string& s, std::size_t& p, UIPrefabNodeDef& def)
	{
		using namespace prefab_detail;
		skipWS(s, p);
		if (p >= s.size() || s[p] != '{') { skipVal(s, p); return; }
		++p;
		while (p < s.size())
		{
			skipWS(s, p);
			if (p < s.size() && s[p] == '}') { ++p; break; }
			if (p < s.size() && s[p] == ',') { ++p; continue; }
			const auto k = readStr(s, p); skipWS(s, p);
			if (p < s.size() && s[p] == ':') ++p; skipWS(s, p);
			if (k == "backgroundColor") def.backgroundColor = readStr(s, p);
			else if (k == "cornerRadius") def.cornerRadius = readNum(s, p);
			else skipVal(s, p);
		}
	}

	static void parseBind(const std::string& s, std::size_t& p, UIPrefabNodeDef& def)
	{
		using namespace prefab_detail;
		skipWS(s, p);
		if (p >= s.size() || s[p] != '{') { skipVal(s, p); return; }
		++p;
		while (p < s.size())
		{
			skipWS(s, p);
			if (p < s.size() && s[p] == '}') { ++p; break; }
			if (p < s.size() && s[p] == ',') { ++p; continue; }
			const auto prop = readStr(s, p); skipWS(s, p);
			if (p < s.size() && s[p] == ':') ++p; skipWS(s, p);
			def.bindDefs[prop] = readStr(s, p);
		}
	}

	static void parseChildren(const std::string& s, std::size_t& p, UIPrefabNodeDef& def)
	{
		using namespace prefab_detail;
		skipWS(s, p);
		if (p >= s.size() || s[p] != '[') { skipVal(s, p); return; }
		++p;
		while (p < s.size())
		{
			skipWS(s, p);
			if (p < s.size() && s[p] == ']') { ++p; break; }
			if (p < s.size() && s[p] == ',') { ++p; continue; }
			def.children.push_back(parseNodeDef(s, p));
		}
	}
};

// ── UIPrefabLibrary ─────────────────────────────────────────────

/// @brief 名前付きプリファブの管理ライブラリ
class UIPrefabLibrary
{
	std::unordered_map<std::string, UIPrefab> m_prefabs;

public:
	void registerPrefab(const std::string& name, UIPrefab prefab)
	{
		m_prefabs.insert_or_assign(name, std::move(prefab));
	}

	void registerFromJson(const std::string& name, const std::string& jsonStr)
	{
		m_prefabs.insert_or_assign(name, UIPrefab::loadFromJson(jsonStr));
	}

	[[nodiscard]] std::shared_ptr<UINode> instantiate(const std::string& name) const
	{
		const auto it = m_prefabs.find(name);
		return (it != m_prefabs.end()) ? it->second.instantiate() : nullptr;
	}

	[[nodiscard]] std::shared_ptr<UINode> instantiateWithBindings(
		const std::string& name, UIBindingContext& ctx) const
	{
		const auto it = m_prefabs.find(name);
		return (it != m_prefabs.end()) ? it->second.instantiateWithBindings(ctx) : nullptr;
	}

	[[nodiscard]] bool has(const std::string& name) const { return m_prefabs.count(name) > 0; }
	[[nodiscard]] std::size_t count() const noexcept { return m_prefabs.size(); }
};

} // namespace mitiru::ui
