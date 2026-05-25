#pragma once

/// @file Accessibility.hpp
/// @brief UI system 向けの汎用 accessibility support。
/// @details VN の accessibility module を汎用 UI namespace へ橋渡しし、
///          screen reader 出力用の UINode tree traversal と focus
///          indicator 描画を追加する。vn::Accessibility の中核型を再 export する。

#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/vn/Accessibility.hpp>

namespace mitiru::ui
{

// ════════════════════════════════════════════════════════════════════
//  VN accessibility から再 export した型
// ════════════════════════════════════════════════════════════════════

/// @brief 色覚モード (vn module から再 export)。
using ColorBlindMode = vn::ColorBlindMode;

/// @brief 色変換ユーティリティ (vn module から再 export)。
using ColorTransform = vn::ColorTransform;

// ════════════════════════════════════════════════════════════════════
//  汎用 accessibility 設定
// ════════════════════════════════════════════════════════════════════

/// @brief 汎用 UI system の accessibility 設定。
struct UIAccessibilityConfig
{
	bool highContrast          = false;   ///< 高コントラストモード。
	bool reducedMotion         = false;   ///< アニメーションを抑制 / 無効化する。
	bool screenReaderEnabled   = false;   ///< screen reader のテキスト出力を有効化する。
	float textSizeMultiplier   = 1.0f;    ///< 文字サイズ倍率 (1.0-3.0)。
	float letterSpacing        = 1.0f;    ///< 字間倍率。
	float lineSpacing          = 1.0f;    ///< 行間倍率。
	ColorBlindMode colorBlindMode = ColorBlindMode::None; ///< 色覚モード。

	/// @brief 文字サイズ倍率を安全な範囲に clamp する。
	void setTextSizeMultiplier(float multiplier) noexcept
	{
		textSizeMultiplier = std::clamp(multiplier, 1.0f, 3.0f);
	}
};

// ════════════════════════════════════════════════════════════════════
//  Focus indicator
// ════════════════════════════════════════════════════════════════════

/// @brief focus indicator の見た目設定。
struct FocusIndicatorStyle
{
	sgc::Colorf color{0.2f, 0.6f, 1.0f, 0.9f}; ///< focus ring の色。
	float thickness   = 2.0f;                     ///< ring の太さ (px)。
	float padding     = 2.0f;                     ///< node 境界の外側の padding。
	float cornerRadius = 4.0f;                    ///< 角丸の半径。
	bool pulsate      = true;                     ///< 控えめな脈動アニメーション。
	float pulsateSpeed = 2.0f;                    ///< 1 秒あたりの脈動回数。
};

/// @brief 指定 UINode の focus indicator 境界を計算する。
/// @param node focus 中の node。
/// @param style focus indicator style。
/// @return focus ring の外側矩形。
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

/// @brief focus indicator の不透明度を計算する (任意で脈動を加味)。
/// @param style focus indicator style。
/// @param elapsedTime 経過秒数 (脈動用)。
/// @return 不透明度 [0, 1]。
[[nodiscard]] inline float computeFocusIndicatorOpacity(
	const FocusIndicatorStyle& style, float elapsedTime) noexcept
{
	if (!style.pulsate) return style.color.a;
	const float phase = std::sin(elapsedTime * style.pulsateSpeed * 6.283185f);
	return style.color.a * (0.7f + 0.3f * phase);
}

// ════════════════════════════════════════════════════════════════════
//  UINode tree からの screen reader テキスト
// ════════════════════════════════════════════════════════════════════

/// @brief 単一 UI 要素の screen reader 用説明。
struct UIElementDescription
{
	UINodeId id = INVALID_UI_NODE;   ///< Node ID。
	std::string role;                 ///< 意味的な role 名。
	std::string name;                 ///< node 名 / label。
	std::string value;                ///< 現在値 (slider 等向け)。
	std::string state;                ///< 状態情報 ("focused"、"disabled" 等)。
};

/// @brief UIRole を screen reader 向けの role 文字列へ変換する。
/// @param role UIRole の列挙値。
/// @return 人間が読める role 名。
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

/// @brief 単一 UINode の screen reader 用説明を構築する。
/// @param node 説明対象の UINode。
/// @return screen reader 出力用の要素説明。
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

/// @brief UINode tree を走査し screen reader 用説明を収集する。
/// @details visible な node のみ含む。再帰は child tree を辿る。
/// @param root 走査対象の root node。
/// @return tree 順に並んだ要素説明の平坦リスト。
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

		// 最初の child を先に処理するため逆順に push する。
		for (std::size_t i = current.node->childCount(); i > 0; --i)
		{
			stack.push_back({&current.node->child(i - 1)});
		}
	}

	return result;
}

/// @brief screen reader 向けに UINode tree から単一の plain-text 文字列を生成する。
/// @param root root node。
/// @return 連結したテキスト説明。
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
//  色変換 (vn::ColorTransform へ委譲)
// ════════════════════════════════════════════════════════════════════

/// @brief 色に色覚変換を適用する。
/// @param color 入力色。
/// @param mode 色覚モード。
/// @return 変換後の色。
[[nodiscard]] inline sgc::Colorf applyColorBlindTransform(
	const sgc::Colorf& color, ColorBlindMode mode) noexcept
{
	return ColorTransform::applyMode(color, mode);
}

// ════════════════════════════════════════════════════════════════════
//  reduced motion ヘルパー
// ════════════════════════════════════════════════════════════════════

/// @brief アニメーションを抑制すべきか問い合わせる。
/// @param config accessibility 設定。
/// @return 抑制 / スキップすべきなら true。
[[nodiscard]] inline bool shouldReduceMotion(
	const UIAccessibilityConfig& config) noexcept
{
	return config.reducedMotion;
}

/// @brief accessibility 設定に応じてアニメーション時間を調整する。
/// @param baseDuration 元の時間 (秒)。
/// @param config accessibility 設定。
/// @return 調整後の時間 (reduced motion 有効なら 0)。
[[nodiscard]] inline float adjustAnimationDuration(
	float baseDuration, const UIAccessibilityConfig& config) noexcept
{
	return config.reducedMotion ? 0.0f : baseDuration;
}

/// @brief accessibility 設定に応じて font size を調整する。
/// @param baseFontSize 元の font size。
/// @param config accessibility 設定。
/// @return スケール後の font size。
[[nodiscard]] inline float adjustFontSize(
	float baseFontSize, const UIAccessibilityConfig& config) noexcept
{
	return baseFontSize * config.textSizeMultiplier;
}

} // namespace mitiru::ui
