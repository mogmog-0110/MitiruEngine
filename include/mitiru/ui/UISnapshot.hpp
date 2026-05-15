#pragma once

/// @file UISnapshot.hpp
/// @brief UIツリーのスナップショット取得ユーティリティ
/// @details UINodeツリーの現在状態を階層的またはフラットなデータ構造としてキャプチャし、
///          JSON変換を提供する。

#include <string>
#include <vector>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::ui
{

/// @brief UIノードのスナップショット情報
/// @details UINodeの状態を値としてコピーした構造体。子要素も再帰的に保持する。
struct UIElementInfo
{
	UINodeId id = INVALID_UI_NODE;  ///< ノードID
	std::string name;                ///< ノード名
	UIRole role = UIRole::Container; ///< セマンティックロール
	sgc::Rectf bounds{};             ///< バウンズ
	std::string text;                ///< テキスト内容
	float value = 0.0f;              ///< 値
	float maxValue = 1.0f;           ///< 最大値
	bool visible = true;             ///< 可視フラグ
	std::vector<UIElementInfo> children; ///< 子要素のスナップショット

	/// @brief スナップショット情報をJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";
		json += "\"id\":" + std::to_string(id);
		json += ",\"name\":\"" + observe::jsonEscape(name) + "\"";
		json += ",\"role\":" + std::to_string(static_cast<int>(role));
		json += ",\"bounds\":{";
		json += "\"x\":" + std::to_string(bounds.x());
		json += ",\"y\":" + std::to_string(bounds.y());
		json += ",\"w\":" + std::to_string(bounds.width());
		json += ",\"h\":" + std::to_string(bounds.height());
		json += "}";
		json += ",\"text\":\"" + observe::jsonEscape(text) + "\"";
		json += ",\"value\":" + std::to_string(value);
		json += ",\"maxValue\":" + std::to_string(maxValue);
		json += ",\"visible\":" + std::string(visible ? "true" : "false");

		if (!children.empty())
		{
			json += ",\"children\":[";
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				if (i > 0) { json += ","; }
				json += children[i].toJson();
			}
			json += "]";
		}

		json += "}";
		return json;
	}
};

/// @brief UIツリーのスナップショットユーティリティ
/// @details UINodeツリーの状態を階層的・フラット形式で取得し、JSON変換を提供する。
///
/// @code
/// auto snapshot = mitiru::ui::UISnapshot::capture(rootNode);
/// auto jsonStr = mitiru::ui::UISnapshot::toJson(rootNode);
/// auto flat = mitiru::ui::UISnapshot::flatten(rootNode);
/// @endcode
class UISnapshot
{
public:
	/// @brief UINodeツリーの現在状態を階層的スナップショットとしてキャプチャする
	/// @param root ルートノード
	/// @return 階層構造を持つUIElementInfo
	[[nodiscard]] static UIElementInfo capture(const UINode& root)
	{
		UIElementInfo info;
		info.id = root.id();
		info.name = root.name();
		info.role = root.role();
		info.bounds = root.bounds();
		info.text = root.text();
		info.value = root.value();
		info.maxValue = root.maxValue();
		info.visible = root.visible();

		for (const auto& child : root.children())
		{
			if (child)
			{
				info.children.push_back(capture(*child));
			}
		}

		return info;
	}

	/// @brief UINodeツリーを深さ優先でフラット化する
	/// @param root ルートノード
	/// @return 深さ優先順のUIElementInfoリスト（子要素のchildrenは空）
	[[nodiscard]] static std::vector<UIElementInfo> flatten(const UINode& root)
	{
		std::vector<UIElementInfo> result;
		flattenImpl(root, result);
		return result;
	}

	/// @brief UINodeツリー全体をJSON文字列に変換する
	/// @param root ルートノード
	/// @return JSON文字列
	[[nodiscard]] static std::string toJson(const UINode& root)
	{
		return capture(root).toJson();
	}

	/// @brief 可視かつContainer以外の要素のみのサマリーJSONを生成する
	/// @param root ルートノード
	/// @return JSON配列文字列
	[[nodiscard]] static std::string toSummaryJson(const UINode& root)
	{
		std::vector<UIElementInfo> visible;
		collectVisibleNonContainer(root, visible);

		std::string json = "[";
		for (std::size_t i = 0; i < visible.size(); ++i)
		{
			if (i > 0) { json += ","; }
			json += visible[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	/// @brief フラット化の再帰実装
	static void flattenImpl(const UINode& node, std::vector<UIElementInfo>& result)
	{
		UIElementInfo info;
		info.id = node.id();
		info.name = node.name();
		info.role = node.role();
		info.bounds = node.bounds();
		info.text = node.text();
		info.value = node.value();
		info.maxValue = node.maxValue();
		info.visible = node.visible();
		// children は空のまま（フラットリスト）
		result.push_back(std::move(info));

		for (const auto& child : node.children())
		{
			if (child)
			{
				flattenImpl(*child, result);
			}
		}
	}

	/// @brief 可視かつContainer以外の要素を再帰的に収集する
	static void collectVisibleNonContainer(
		const UINode& node, std::vector<UIElementInfo>& result)
	{
		if (node.visible() && node.role() != UIRole::Container)
		{
			UIElementInfo info;
			info.id = node.id();
			info.name = node.name();
			info.role = node.role();
			info.bounds = node.bounds();
			info.text = node.text();
			info.value = node.value();
			info.maxValue = node.maxValue();
			info.visible = node.visible();
			result.push_back(std::move(info));
		}

		for (const auto& child : node.children())
		{
			if (child)
			{
				collectVisibleNonContainer(*child, result);
			}
		}
	}
};

} // namespace mitiru::ui
