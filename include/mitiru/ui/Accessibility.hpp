#pragma once

/// @file Accessibility.hpp
/// @brief General-purpose accessibility support for the UI system.
/// @details Bridges the VN accessibility module into the general UI namespace,
///          adding UINode tree traversal for screen reader output and focus
///          indicator rendering. Re-exports core types from vn::Accessibility.

#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/vn/Accessibility.hpp>

namespace mitiru::ui
{

// ════════════════════════════════════════════════════════════════════
//  Re-exported types from VN accessibility
// ════════════════════════════════════════════════════════════════════

/// @brief Color blindness mode (re-exported from vn module).
using ColorBlindMode = vn::ColorBlindMode;

/// @brief Color transform utility (re-exported from vn module).
using ColorTransform = vn::ColorTransform;

// ════════════════════════════════════════════════════════════════════
//  General-purpose accessibility configuration
// ════════════════════════════════════════════════════════════════════

/// @brief Accessibility settings for the general UI system.
struct UIAccessibilityConfig
{
	bool highContrast          = false;   ///< High-contrast mode.
	bool reducedMotion         = false;   ///< Reduce or disable animations.
	bool screenReaderEnabled   = false;   ///< Enable screen reader text output.
	float textSizeMultiplier   = 1.0f;    ///< Text size scale (1.0-3.0).
	float letterSpacing        = 1.0f;    ///< Letter spacing multiplier.
	float lineSpacing          = 1.0f;    ///< Line spacing multiplier.
	ColorBlindMode colorBlindMode = ColorBlindMode::None; ///< Color vision mode.

	/// @brief Clamp text size multiplier to safe range.
	void setTextSizeMultiplier(float multiplier) noexcept
	{
		textSizeMultiplier = std::clamp(multiplier, 1.0f, 3.0f);
	}
};

// ════════════════════════════════════════════════════════════════════
//  Focus indicator
// ════════════════════════════════════════════════════════════════════

/// @brief Visual configuration for focus indicators.
struct FocusIndicatorStyle
{
	sgc::Colorf color{0.2f, 0.6f, 1.0f, 0.9f}; ///< Focus ring colour.
	float thickness   = 2.0f;                     ///< Ring thickness in pixels.
	float padding     = 2.0f;                     ///< Padding outside the node bounds.
	float cornerRadius = 4.0f;                    ///< Rounded corner radius.
	bool pulsate      = true;                     ///< Subtle pulsation animation.
	float pulsateSpeed = 2.0f;                    ///< Pulsation cycles per second.
};

/// @brief Computes focus indicator bounds for a given UINode.
/// @param node The focused node.
/// @param style Focus indicator style.
/// @return Outer rectangle for the focus ring.
[[nodiscard]] inline sgc::Rectf computeFocusIndicatorBounds(
	const UINode& node, const FocusIndicatorStyle& style = {}) noexcept
{
	const auto& b = node.bounds();
	return sgc::Rectf{
		b.x() - style.padding - style.thickness,
		b.y() - style.padding - style.thickness,
		b.width() + (style.padding + style.thickness) * 2.0f,
		b.height() + (style.padding + style.thickness) * 2.0f
	};
}

/// @brief Compute focus indicator opacity with optional pulsation.
/// @param style Focus indicator style.
/// @param elapsedTime Time in seconds (for pulsation).
/// @return Opacity value [0, 1].
[[nodiscard]] inline float computeFocusIndicatorOpacity(
	const FocusIndicatorStyle& style, float elapsedTime) noexcept
{
	if (!style.pulsate) return style.color.a;
	const float phase = std::sin(elapsedTime * style.pulsateSpeed * 6.283185f);
	return style.color.a * (0.7f + 0.3f * phase);
}

// ════════════════════════════════════════════════════════════════════
//  Screen reader text from UINode tree
// ════════════════════════════════════════════════════════════════════

/// @brief Screen reader description of a single UI element.
struct UIElementDescription
{
	UINodeId id = INVALID_UI_NODE;   ///< Node ID.
	std::string role;                 ///< Semantic role name.
	std::string name;                 ///< Node name / label.
	std::string value;                ///< Current value (for sliders, etc.).
	std::string state;                ///< State info ("focused", "disabled", etc.).
};

/// @brief Convert a UIRole to a screen reader-friendly role string.
/// @param role The UIRole enumeration value.
/// @return Human-readable role name.
[[nodiscard]] inline std::string roleToString(UIRole role) noexcept
{
	switch (role)
	{
	case UIRole::Container:   return "group";
	case UIRole::Label:       return "label";
	case UIRole::Button:      return "button";
	case UIRole::ProgressBar: return "progress bar";
	case UIRole::Image:       return "image";
	case UIRole::HealthBar:   return "health bar";
	case UIRole::ScoreLabel:  return "score";
	case UIRole::MiniMap:     return "mini map";
	case UIRole::Inventory:   return "inventory";
	case UIRole::DialogBox:   return "dialog";
	case UIRole::MenuItem:    return "menu item";
	case UIRole::Tooltip:     return "tooltip";
	case UIRole::Panel:       return "panel";
	case UIRole::Slider:      return "slider";
	case UIRole::Toggle:      return "toggle";
	case UIRole::TextInput:   return "text input";
	case UIRole::Dropdown:    return "dropdown";
	case UIRole::ListView:    return "list view";
	case UIRole::TabBar:      return "tab bar";
	case UIRole::Custom:      return "custom";
	}
	return "unknown";
}

/// @brief Build a screen reader description for a single UINode.
/// @param node The UINode to describe.
/// @return Element description for screen reader output.
[[nodiscard]] inline UIElementDescription describeNode(const UINode& node)
{
	UIElementDescription desc;
	desc.id = node.id();
	desc.role = roleToString(node.role());
	desc.name = node.name();

	if (!node.text().empty())
	{
		desc.value = node.text();
	}
	else if (node.role() == UIRole::ProgressBar
		|| node.role() == UIRole::HealthBar
		|| node.role() == UIRole::Slider)
	{
		const float pct = (node.maxValue() > 0.0f)
			? (node.value() / node.maxValue()) * 100.0f
			: 0.0f;
		desc.value = std::to_string(static_cast<int>(pct)) + "%";
	}

	if (!node.visible())
	{
		desc.state = "hidden";
	}

	return desc;
}

/// @brief Traverse a UINode tree and collect screen reader descriptions.
/// @details Only includes visible nodes. Recursion follows the child tree.
/// @param root The root node to traverse.
/// @return Flat list of element descriptions in tree order.
[[nodiscard]] inline std::vector<UIElementDescription> traverseForScreenReader(
	const UINode& root)
{
	std::vector<UIElementDescription> result;

	struct TraversalFrame
	{
		const UINode* node;
	};

	std::vector<TraversalFrame> stack;
	stack.push_back({&root});

	while (!stack.empty())
	{
		auto current = stack.back();
		stack.pop_back();

		if (!current.node->visible()) continue;

		result.push_back(describeNode(*current.node));

		// Push children in reverse order so first child is processed first.
		for (std::size_t i = current.node->childCount(); i > 0; --i)
		{
			stack.push_back({&current.node->child(i - 1)});
		}
	}

	return result;
}

/// @brief Generate a single plain-text string from UINode tree for screen readers.
/// @param root The root node.
/// @return Concatenated text description.
[[nodiscard]] inline std::string generateScreenReaderText(const UINode& root)
{
	const auto descriptions = traverseForScreenReader(root);
	std::string result;

	for (const auto& desc : descriptions)
	{
		if (desc.role == "group" || desc.role == "panel") continue;

		result += desc.role;
		if (!desc.name.empty())
		{
			result += " \"" + desc.name + "\"";
		}
		if (!desc.value.empty())
		{
			result += ": " + desc.value;
		}
		if (!desc.state.empty())
		{
			result += " (" + desc.state + ")";
		}
		result += "\n";
	}

	return result;
}

// ════════════════════════════════════════════════════════════════════
//  Color transforms (delegated to vn::ColorTransform)
// ════════════════════════════════════════════════════════════════════

/// @brief Apply color blindness transform to a colour.
/// @param color Input colour.
/// @param mode Color blindness mode.
/// @return Transformed colour.
[[nodiscard]] inline sgc::Colorf applyColorBlindTransform(
	const sgc::Colorf& color, ColorBlindMode mode) noexcept
{
	return ColorTransform::applyMode(color, mode);
}

// ════════════════════════════════════════════════════════════════════
//  Reduced motion helper
// ════════════════════════════════════════════════════════════════════

/// @brief Query whether animations should be reduced.
/// @param config Accessibility configuration.
/// @return True if animations should be reduced or skipped.
[[nodiscard]] inline bool shouldReduceMotion(
	const UIAccessibilityConfig& config) noexcept
{
	return config.reducedMotion;
}

/// @brief Adjust animation duration based on accessibility settings.
/// @param baseDuration Original duration in seconds.
/// @param config Accessibility configuration.
/// @return Adjusted duration (0 if reduced motion is enabled).
[[nodiscard]] inline float adjustAnimationDuration(
	float baseDuration, const UIAccessibilityConfig& config) noexcept
{
	return config.reducedMotion ? 0.0f : baseDuration;
}

/// @brief Adjust font size based on accessibility settings.
/// @param baseFontSize Original font size.
/// @param config Accessibility configuration.
/// @return Scaled font size.
[[nodiscard]] inline float adjustFontSize(
	float baseFontSize, const UIAccessibilityConfig& config) noexcept
{
	return baseFontSize * config.textSizeMultiplier;
}

} // namespace mitiru::ui
