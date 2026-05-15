#pragma once

/// @file UIDataBinding.hpp
/// @brief UIとゲームステート間のデータバインディングシステム

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief バインド可能なプロパティの種別
enum class UIBindProperty : std::uint8_t
{
	Text, Value, Visible, Color, Enabled, PositionX, PositionY, SizeW, SizeH,
};

/// @brief コンテキスト変数の値型
using UIBindValue = std::variant<std::string, float, bool, int>;

namespace detail
{

[[nodiscard]] inline std::string valueToString(const UIBindValue& v)
{
	struct V
	{
		std::string operator()(const std::string& s) const { return s; }
		std::string operator()(float f) const { return std::to_string(f); }
		std::string operator()(bool b) const { return b ? "true" : "false"; }
		std::string operator()(int i) const { return std::to_string(i); }
	};
	return std::visit(V{}, v);
}

[[nodiscard]] inline float valueToFloat(const UIBindValue& v)
{
	struct V
	{
		float operator()(const std::string&) const { return 0.0f; }
		float operator()(float f) const { return f; }
		float operator()(bool b) const { return b ? 1.0f : 0.0f; }
		float operator()(int i) const { return static_cast<float>(i); }
	};
	return std::visit(V{}, v);
}

[[nodiscard]] inline bool valueToBool(const UIBindValue& v)
{
	struct V
	{
		bool operator()(const std::string& s) const { return !s.empty(); }
		bool operator()(float f) const { return f != 0.0f; }
		bool operator()(bool b) const { return b; }
		bool operator()(int i) const { return i != 0; }
	};
	return std::visit(V{}, v);
}

inline void applyToNode(UINode& node, UIBindProperty prop, const UIBindValue& value)
{
	const auto& b = node.bounds();
	switch (prop)
	{
	case UIBindProperty::Text:      node.setText(valueToString(value)); break;
	case UIBindProperty::Value:     node.setValue(valueToFloat(value)); break;
	case UIBindProperty::Visible:   node.setVisible(valueToBool(value)); break;
	case UIBindProperty::Color:     node.setProperty("color", valueToString(value)); break;
	case UIBindProperty::Enabled:   node.setProperty("enabled", valueToBool(value) ? "true" : "false"); break;
	case UIBindProperty::PositionX: node.setBounds({valueToFloat(value), b.y(), b.width(), b.height()}); break;
	case UIBindProperty::PositionY: node.setBounds({b.x(), valueToFloat(value), b.width(), b.height()}); break;
	case UIBindProperty::SizeW:     node.setBounds({b.x(), b.y(), valueToFloat(value), b.height()}); break;
	case UIBindProperty::SizeH:     node.setBounds({b.x(), b.y(), b.width(), valueToFloat(value)}); break;
	}
}

[[nodiscard]] inline UIBindValue readFromNode(const UINode& node, UIBindProperty prop)
{
	switch (prop)
	{
	case UIBindProperty::Text:      return node.text();
	case UIBindProperty::Value:     return node.value();
	case UIBindProperty::Visible:   return node.visible();
	case UIBindProperty::Enabled:   return node.getProperty("enabled") != "false";
	case UIBindProperty::PositionX: return node.bounds().x();
	case UIBindProperty::PositionY: return node.bounds().y();
	case UIBindProperty::SizeW:     return node.bounds().width();
	case UIBindProperty::SizeH:     return node.bounds().height();
	default: return std::string{};
	}
}

} // namespace detail

// ── バインディングエントリ ──────────────────────────────────────

struct UIBindingEntry
{
	std::string key;
	UINode* node = nullptr;
	UIBindProperty property{};
	UIBindValue lastValue;
};

struct UITwoWayBindingEntry
{
	UINode* nodeA = nullptr;  UIBindProperty propA{};
	UINode* nodeB = nullptr;  UIBindProperty propB{};
	UIBindValue lastValueA;   UIBindValue lastValueB;
};

struct UIExpressionBindingEntry
{
	UINode* node = nullptr;
	UIBindProperty property{};
	std::string formatExpr;
};

struct UIListBindingEntry
{
	UINode* listNode = nullptr;
	std::string dataKey;
	std::function<std::shared_ptr<UINode>(std::size_t)> factory;
	std::size_t lastCount = 0;
};

// ── UIBindingContext ──────────────────────────────────────────

/// @brief UIデータバインディングコンテキスト
/// @code
/// mitiru::ui::UIBindingContext ctx;
/// ctx.set("hp", 75.0f);
/// ctx.bind("hp", hpBarNode, mitiru::ui::UIBindProperty::Value);
/// ctx.bindExpression(labelNode, mitiru::ui::UIBindProperty::Text, "HP: {hp}/{maxHp}");
/// ctx.update(); // 毎フレーム呼び出し
/// @endcode
class UIBindingContext
{
	std::unordered_map<std::string, UIBindValue> m_variables;
	std::vector<UIBindingEntry> m_bindings;
	std::vector<UITwoWayBindingEntry> m_twoWayBindings;
	std::vector<UIExpressionBindingEntry> m_expressionBindings;
	std::vector<UIListBindingEntry> m_listBindings;
	std::unordered_map<std::string, std::vector<UIBindValue>> m_arrayVariables;

public:
	void set(const std::string& key, UIBindValue value)
	{
		m_variables.insert_or_assign(key, std::move(value));
	}

	template <typename T>
	[[nodiscard]] T get(const std::string& key) const
	{
		const auto it = m_variables.find(key);
		if (it == m_variables.end()) return T{};
		if (const auto* val = std::get_if<T>(&it->second)) return *val;
		return T{};
	}

	void setArray(const std::string& key, std::vector<UIBindValue> data)
	{
		m_arrayVariables.insert_or_assign(key, std::move(data));
	}

	[[nodiscard]] const std::vector<UIBindValue>& getArray(const std::string& key) const
	{
		static const std::vector<UIBindValue> empty;
		const auto it = m_arrayVariables.find(key);
		return (it != m_arrayVariables.end()) ? it->second : empty;
	}

	/// @brief コンテキスト変数をノードプロパティにバインド（一方向）
	void bind(const std::string& key, UINode& node, UIBindProperty property)
	{
		m_bindings.push_back({key, &node, property, {}});
	}

	/// @brief source→target 一方向バインディング
	void bindOneWay(UINode& source, UIBindProperty sourceProp,
	                UINode& target, UIBindProperty targetProp)
	{
		UITwoWayBindingEntry entry;
		entry.nodeA = &source;  entry.propA = sourceProp;
		entry.nodeB = &target;  entry.propB = targetProp;
		entry.lastValueA = detail::readFromNode(source, sourceProp);
		m_twoWayBindings.push_back(std::move(entry));
	}

	/// @brief 双方向バインディング
	void bindTwoWay(UINode& nodeA, UIBindProperty propA,
	                UINode& nodeB, UIBindProperty propB)
	{
		UITwoWayBindingEntry entry;
		entry.nodeA = &nodeA;  entry.propA = propA;
		entry.nodeB = &nodeB;  entry.propB = propB;
		entry.lastValueA = detail::readFromNode(nodeA, propA);
		entry.lastValueB = detail::readFromNode(nodeB, propB);
		m_twoWayBindings.push_back(std::move(entry));
	}

	/// @brief 式バインディング（"HP: {hp}/{maxHp}" 形式）
	void bindExpression(UINode& node, UIBindProperty property, const std::string& formatExpr)
	{
		m_expressionBindings.push_back({&node, property, formatExpr});
	}

	/// @brief リストバインディング（配列データから子ノード自動生成）
	void bindList(UINode& listNode, const std::string& dataKey,
	              std::function<std::shared_ptr<UINode>(std::size_t)> factory)
	{
		m_listBindings.push_back({&listNode, dataKey, std::move(factory), 0});
	}

	/// @brief 全バインディングを同期する（毎フレーム呼び出し）
	void update()
	{
		// コンテキスト変数→ノード
		for (auto& e : m_bindings)
		{
			if (!e.node) continue;
			const auto it = m_variables.find(e.key);
			if (it == m_variables.end()) continue;
			if (it->second != e.lastValue)
			{
				detail::applyToNode(*e.node, e.property, it->second);
				e.lastValue = it->second;
			}
		}
		// 双方向
		for (auto& e : m_twoWayBindings)
		{
			if (!e.nodeA || !e.nodeB) continue;
			const auto curA = detail::readFromNode(*e.nodeA, e.propA);
			const auto curB = detail::readFromNode(*e.nodeB, e.propB);
			if (curA != e.lastValueA)
			{
				detail::applyToNode(*e.nodeB, e.propB, curA);
				e.lastValueA = curA;  e.lastValueB = curA;
			}
			else if (curB != e.lastValueB)
			{
				detail::applyToNode(*e.nodeA, e.propA, curB);
				e.lastValueA = curB;  e.lastValueB = curB;
			}
		}
		// 式バインディング
		for (auto& e : m_expressionBindings)
		{
			if (!e.node) continue;
			detail::applyToNode(*e.node, e.property, resolveExpression(e.formatExpr));
		}
		// リストバインディング
		for (auto& e : m_listBindings)
		{
			if (!e.listNode || !e.factory) continue;
			const auto& data = getArray(e.dataKey);
			if (data.size() == e.lastCount) continue;
			auto& children = e.listNode->children();
			while (!children.empty()) e.listNode->removeChild(children.back()->id());
			for (std::size_t i = 0; i < data.size(); ++i)
			{
				if (auto child = e.factory(i)) e.listNode->addChild(std::move(child));
			}
			e.lastCount = data.size();
		}
	}

	void clear()
	{
		m_bindings.clear();
		m_twoWayBindings.clear();
		m_expressionBindings.clear();
		m_listBindings.clear();
	}

	[[nodiscard]] std::size_t bindingCount() const noexcept
	{
		return m_bindings.size() + m_twoWayBindings.size()
			+ m_expressionBindings.size() + m_listBindings.size();
	}

private:
	[[nodiscard]] std::string resolveExpression(const std::string& expr) const
	{
		std::string result;
		result.reserve(expr.size());
		for (std::size_t i = 0; i < expr.size(); ++i)
		{
			if (expr[i] == '{')
			{
				const auto end = expr.find('}', i + 1);
				if (end != std::string::npos)
				{
					const auto it = m_variables.find(expr.substr(i + 1, end - i - 1));
					if (it != m_variables.end()) result += detail::valueToString(it->second);
					else result += expr.substr(i, end - i + 1);
					i = end;
					continue;
				}
			}
			result += expr[i];
		}
		return result;
	}
};

} // namespace mitiru::ui
