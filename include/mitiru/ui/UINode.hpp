#pragma once

/// @file UINode.hpp
/// @brief セマンティックUIツリーのノード定義
/// @details UIツリーの基本構成要素。各ノードはID・ロール・バウンズ・値・子ノードを持ち、
///          再帰的な検索やJSON変換をサポートする。

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Rect.hpp>

// Windows.h の DialogBox マクロが enum と衝突するため解除
#ifdef DialogBox
#undef DialogBox
#endif
#include <sgc/types/Color.hpp>
#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::ui
{

/// @brief UIノードの識別子型
using UINodeId = std::uint32_t;

/// @brief 無効なUIノードIDを表す定数
inline constexpr UINodeId INVALID_UI_NODE = 0;

/// @brief UI要素のセマンティックロール
enum class UIRole : std::uint8_t
{
	Container,   ///< コンテナ要素
	Label,       ///< テキストラベル
	Button,      ///< ボタン
	ProgressBar, ///< プログレスバー
	Image,       ///< 画像
	HealthBar,   ///< HPバー
	ScoreLabel,  ///< スコア表示
	MiniMap,     ///< ミニマップ
	Inventory,   ///< インベントリ
	DialogBox,   ///< ダイアログボックス
	MenuItem,    ///< メニュー項目
	Tooltip,     ///< ツールチップ
	Panel,       ///< パネル
	Slider,      ///< スライダー
	Toggle,      ///< トグル／チェックボックス
	TextInput,   ///< テキスト入力
	Dropdown,    ///< ドロップダウン
	ListView,    ///< リストビュー
	TabBar,      ///< タブバー
	Custom       ///< カスタム要素
};

/// @brief UIノードのデータ
/// @details UINode が保持する全プロパティをまとめた構造体。
struct UINodeData
{
	UINodeId id = INVALID_UI_NODE;              ///< ノードID
	std::string name;                            ///< ノード名
	UIRole role = UIRole::Container;             ///< セマンティックロール
	sgc::Rectf bounds{};                         ///< スクリーン空間でのバウンズ
	std::string text;                            ///< テキスト内容（ラベル・ボタン用）
	float value = 0.0f;                          ///< 値（プログレスバー等、0〜maxValue）
	float maxValue = 1.0f;                       ///< 最大値
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};  ///< 表示色
	bool visible = true;                         ///< 可視フラグ
	std::map<std::string, std::string> properties; ///< カスタムキー・バリュー
};

/// @brief セマンティックUIツリーのノード
/// @details 階層構造を持つUI要素の基本単位。子ノードの追加・削除、
///          ID・名前・ロールによる再帰検索、JSON変換を提供する。
///
/// @code
/// mitiru::ui::UINodeData data;
/// data.id = 1;
/// data.name = "root";
/// data.role = mitiru::ui::UIRole::Container;
/// auto root = std::make_shared<mitiru::ui::UINode>(std::move(data));
///
/// mitiru::ui::UINodeData childData;
/// childData.id = 2;
/// childData.name = "hp_bar";
/// childData.role = mitiru::ui::UIRole::HealthBar;
/// childData.value = 0.75f;
/// root->addChild(std::make_shared<mitiru::ui::UINode>(std::move(childData)));
///
/// auto* found = root->findByName("hp_bar");
/// @endcode
class UINode
{
	UINodeData m_data;                            ///< ノードデータ
	std::vector<std::shared_ptr<UINode>> m_children; ///< 子ノード
	UINode* m_parent = nullptr;                   ///< 親ノード（非所有）
	bool m_focusable = false;                     ///< フォーカス可能フラグ
	bool m_hitTestEnabled = true;                 ///< ヒットテスト有効フラグ
	int m_zIndex = 0;                             ///< Z順序（高い値が手前）
	bool m_interactable = false;                  ///< インタラクション可能フラグ

	/// @brief ロールに基づくデフォルトのフォーカス可能判定
	static bool isDefaultFocusable(UIRole role) noexcept
	{
		switch (role)
		{
		case UIRole::Button:
		case UIRole::Slider:
		case UIRole::Toggle:
		case UIRole::TextInput:
		case UIRole::Dropdown:
		case UIRole::MenuItem:
			return true;
		default:
			return false;
		}
	}

	/// @brief ロールに基づくデフォルトのインタラクション可能判定
	static bool isDefaultInteractable(UIRole role) noexcept
	{
		switch (role)
		{
		case UIRole::Button:
		case UIRole::Slider:
		case UIRole::Toggle:
		case UIRole::TextInput:
		case UIRole::Dropdown:
		case UIRole::MenuItem:
			return true;
		default:
			return false;
		}
	}

public:
	/// @brief UINodeDataを指定して構築する
	/// @param data ノードデータ
	explicit UINode(UINodeData data)
		: m_data(std::move(data))
		, m_focusable(isDefaultFocusable(m_data.role))
		, m_interactable(isDefaultInteractable(m_data.role))
	{
	}

	// ── ゲッター ────────────────────────────────────────────

	/// @brief ノードIDを取得する
	[[nodiscard]] UINodeId id() const noexcept { return m_data.id; }

	/// @brief ノード名を取得する
	[[nodiscard]] const std::string& name() const noexcept { return m_data.name; }

	/// @brief セマンティックロールを取得する
	[[nodiscard]] UIRole role() const noexcept { return m_data.role; }

	/// @brief バウンズを取得する
	[[nodiscard]] const sgc::Rectf& bounds() const noexcept { return m_data.bounds; }

	/// @brief テキストを取得する
	[[nodiscard]] const std::string& text() const noexcept { return m_data.text; }

	/// @brief 値を取得する
	[[nodiscard]] float value() const noexcept { return m_data.value; }

	/// @brief 最大値を取得する
	[[nodiscard]] float maxValue() const noexcept { return m_data.maxValue; }

	/// @brief 表示色を取得する
	[[nodiscard]] const sgc::Colorf& color() const noexcept { return m_data.color; }

	/// @brief 可視フラグを取得する
	[[nodiscard]] bool visible() const noexcept { return m_data.visible; }

	/// @brief ノードデータ全体を取得する
	[[nodiscard]] const UINodeData& data() const noexcept { return m_data; }

	/// @brief フォーカス可能フラグを取得する
	[[nodiscard]] bool focusable() const noexcept { return m_focusable; }

	/// @brief ヒットテスト有効フラグを取得する
	[[nodiscard]] bool hitTestEnabled() const noexcept { return m_hitTestEnabled; }

	/// @brief Z順序を取得する
	[[nodiscard]] int zIndex() const noexcept { return m_zIndex; }

	/// @brief インタラクション可能フラグを取得する
	[[nodiscard]] bool interactable() const noexcept { return m_interactable; }

	// ── セッター ────────────────────────────────────────────

	/// @brief バウンズを設定する
	/// @param bounds 新しいバウンズ
	void setBounds(const sgc::Rectf& bounds) { m_data.bounds = bounds; }

	/// @brief テキストを設定する
	/// @param text 新しいテキスト
	void setText(const std::string& text) { m_data.text = text; }

	/// @brief 値を設定する
	/// @param value 新しい値
	void setValue(float value) { m_data.value = value; }

	/// @brief 可視フラグを設定する
	/// @param visible 可視であればtrue
	void setVisible(bool visible) { m_data.visible = visible; }

	/// @brief フォーカス可能フラグを設定する
	/// @param focusable フォーカス可能であればtrue
	void setFocusable(bool focusable) noexcept { m_focusable = focusable; }

	/// @brief ヒットテスト有効フラグを設定する
	/// @param enabled 有効であればtrue
	void setHitTestEnabled(bool enabled) noexcept { m_hitTestEnabled = enabled; }

	/// @brief Z順序を設定する
	/// @param zIndex Z順序値（高い値が手前）
	void setZIndex(int zIndex) noexcept { m_zIndex = zIndex; }

	/// @brief インタラクション可能フラグを設定する
	/// @param interactable インタラクション可能であればtrue
	void setInteractable(bool interactable) noexcept { m_interactable = interactable; }

	/// @brief カスタムプロパティを設定する
	/// @param key プロパティキー
	/// @param value プロパティ値
	void setProperty(const std::string& key, const std::string& value)
	{
		m_data.properties.insert_or_assign(key, value);
	}

	/// @brief カスタムプロパティを取得する
	/// @param key プロパティキー
	/// @return プロパティ値（未設定の場合は空文字列）
	[[nodiscard]] std::string getProperty(std::string_view key) const
	{
		const auto it = m_data.properties.find(std::string(key));
		if (it != m_data.properties.end())
		{
			return it->second;
		}
		return {};
	}

	// ── ツリー操作 ──────────────────────────────────────────

	/// @brief 子ノードを追加する
	/// @param child 追加する子ノード
	void addChild(std::shared_ptr<UINode> child)
	{
		if (child)
		{
			child->m_parent = this;
			m_children.push_back(std::move(child));
		}
	}

	/// @brief 指定IDの子ノードを削除する
	/// @param childId 削除する子ノードのID
	void removeChild(UINodeId childId)
	{
		m_children.erase(
			std::remove_if(m_children.begin(), m_children.end(),
				[childId](const std::shared_ptr<UINode>& child)
				{
					return child && child->id() == childId;
				}),
			m_children.end());
	}

	/// @brief 子ノード一覧を取得する
	[[nodiscard]] const std::vector<std::shared_ptr<UINode>>& children() const noexcept
	{
		return m_children;
	}

	/// @brief 子ノード数を取得する
	/// @return 子ノード数
	[[nodiscard]] std::size_t childCount() const noexcept
	{
		return m_children.size();
	}

	/// @brief 親ノードを取得する
	/// @return 親ノードへのポインタ（ルートの場合はnullptr）
	[[nodiscard]] UINode* parent() const noexcept { return m_parent; }

	// ── 検索 ────────────────────────────────────────────────

	/// @brief サブツリーからIDで検索する
	/// @param id 検索対象のノードID
	/// @return 見つかったノードへのポインタ（見つからなければnullptr）
	[[nodiscard]] UINode* findById(UINodeId id)
	{
		if (m_data.id == id)
		{
			return this;
		}
		for (auto& child : m_children)
		{
			if (auto* found = child->findById(id))
			{
				return found;
			}
		}
		return nullptr;
	}

	/// @brief サブツリーから名前で検索する
	/// @param name 検索対象のノード名
	/// @return 見つかったノードへのポインタ（見つからなければnullptr）
	[[nodiscard]] UINode* findByName(std::string_view name)
	{
		if (m_data.name == name)
		{
			return this;
		}
		for (auto& child : m_children)
		{
			if (auto* found = child->findByName(name))
			{
				return found;
			}
		}
		return nullptr;
	}

	/// @brief サブツリーからロールで全件検索する
	/// @param role 検索対象のロール
	/// @return マッチした全ノードへのポインタリスト
	[[nodiscard]] std::vector<const UINode*> findByRole(UIRole role) const
	{
		std::vector<const UINode*> result;
		findByRoleImpl(role, result);
		return result;
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief ノードとその子孫をJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";
		json += "\"id\":" + std::to_string(m_data.id);
		json += ",\"name\":\"" + observe::jsonEscape(m_data.name) + "\"";
		json += ",\"role\":" + std::to_string(static_cast<int>(m_data.role));
		json += ",\"bounds\":{";
		json += "\"x\":" + std::to_string(m_data.bounds.x());
		json += ",\"y\":" + std::to_string(m_data.bounds.y());
		json += ",\"w\":" + std::to_string(m_data.bounds.width());
		json += ",\"h\":" + std::to_string(m_data.bounds.height());
		json += "}";
		json += ",\"text\":\"" + observe::jsonEscape(m_data.text) + "\"";
		json += ",\"value\":" + std::to_string(m_data.value);
		json += ",\"maxValue\":" + std::to_string(m_data.maxValue);
		json += ",\"color\":{";
		json += "\"r\":" + std::to_string(m_data.color.r);
		json += ",\"g\":" + std::to_string(m_data.color.g);
		json += ",\"b\":" + std::to_string(m_data.color.b);
		json += ",\"a\":" + std::to_string(m_data.color.a);
		json += "}";
		json += ",\"visible\":" + std::string(m_data.visible ? "true" : "false");

		if (!m_data.properties.empty())
		{
			json += ",\"properties\":{";
			bool first = true;
			for (const auto& [key, val] : m_data.properties)
			{
				if (!first) { json += ","; }
				json += "\"" + observe::jsonEscape(key) + "\":\"" +
					observe::jsonEscape(val) + "\"";
				first = false;
			}
			json += "}";
		}

		if (!m_children.empty())
		{
			json += ",\"children\":[";
			for (std::size_t i = 0; i < m_children.size(); ++i)
			{
				if (i > 0) { json += ","; }
				json += m_children[i]->toJson();
			}
			json += "]";
		}

		json += "}";
		return json;
	}

private:
	/// @brief ロール検索の再帰実装
	void findByRoleImpl(UIRole role, std::vector<const UINode*>& result) const
	{
		if (m_data.role == role)
		{
			result.push_back(this);
		}
		for (const auto& child : m_children)
		{
			child->findByRoleImpl(role, result);
		}
	}
};

} // namespace mitiru::ui
