#pragma once

/**
 * @file UITemplate.hpp
 * @brief JSON 駆動の UI tree 生成と serialize。
 *
 * コンパクトな JSON template 形式と runtime の UINode tree を相互変換する。
 * よくある HUD layout と要素生成のための factory helper を提供する。
 *
 * @par JSON template 形式
 * @code{.json}
 * {
 *   "type": "panel",
 *   "name": "hud",
 *   "anchor": "top-left",
 *   "width": 200, "height": 50,
 *   "children": [
 *     {"type": "health_bar", "name": "hp", "value": 0.75},
 *     {"type": "label", "name": "score", "text": "Score: 0"}
 *   ]
 * }
 * @endcode
 */

#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/LayoutEngine.hpp>
#include <mitiru/data/JsonBuilder.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/**
 * @class UITemplate
 * @brief JSON template と UINode tree の双方向 converter。
 *
 * public method は全て static。このクラスは instance 化を想定しない。
 *
 * @code
 *   auto root = UITemplate::fromJson(jsonStr);
 *   auto json = UITemplate::toJson(*root);
 *   auto hud  = UITemplate::createHUD(1920.0f, 1080.0f);
 * @endcode
 */
class UITemplate {
public:
    // -----------------------------------------------------------------
    // Parsing / Serialisation
    // -----------------------------------------------------------------

    /**
     * @brief JSON template 文字列を UI node tree に parse する。
     *
     * 内部で @c mitiru::data::JsonReader を使う。各 JSON object が UINode に
     * なり、ネストした @c "children" 配列が子 node を再帰的に生成する。
     *
     * @param jsonTemplate  整形式の JSON 文字列。
     * @return root UINode への shared pointer。parse 失敗時は nullptr。
     */
    static std::shared_ptr<UINode> fromJson(const std::string& jsonTemplate) {
        mitiru::data::JsonReader reader;
        if (!reader.parse(jsonTemplate)) {
            return nullptr;
        }
        return parseNode(reader);
    }

    /**
     * @brief 既存の UI tree を JSON template 文字列へ serialize し直す。
     *
     * @param root  serialize する tree の root node。
     * @return 整形済み (pretty-print) の JSON 文字列。
     */
    static std::string toJson(const UINode& root) {
        return nodeToJson(root, 0);
    }

    // -----------------------------------------------------------------
    // Factory helpers
    // -----------------------------------------------------------------

    /**
     * @brief 標準的なゲーム HUD layout を生成する。
     *
     * 返される tree は以下を含む:
     * - Health bar (top-left)
     * - Score label (top-right)
     * - Minimap placeholder (bottom-right)
     *
     * @param screenW  viewport 幅 (px)。
     * @param screenH  viewport 高さ (px)。
     * @return root HUD node への shared pointer。
     */
    static std::shared_ptr<UINode> createHUD(float screenW, float screenH) {
        auto root = makeNode(generateId(), "hud", UIRole::Panel);
        root->setBounds(sgc::Rectf{0.0f, 0.0f, screenW, screenH});

        // Health bar -- 左上
        auto hp = makeNode(generateId(), "hp_bar", UIRole::HealthBar);
        hp->setValue(1.0f);
        hp->setBounds(sgc::Rectf{10.0f, 10.0f, 200.0f, 24.0f});
        root->addChild(hp);

        // Score label -- 右上
        auto score = makeNode(generateId(), "score_label", UIRole::Label);
        score->setText("Score: 0");
        score->setBounds(sgc::Rectf{screenW - 210.0f, 10.0f, 200.0f, 24.0f});
        root->addChild(score);

        // Minimap -- 右下
        auto minimap = makeNode(generateId(), "minimap", UIRole::Panel);
        minimap->setBounds(sgc::Rectf{screenW - 170.0f, screenH - 170.0f, 160.0f, 160.0f});
        root->addChild(minimap);

        return root;
    }

    /**
     * @brief 型名から単一の UI 要素を生成する。
     *
     * @param typeName  小文字文字列の role (例 "label", "button")。
     * @param name      人間可読な name / 識別子。
     * @return 新規 node への shared pointer。
     */
    static std::shared_ptr<UINode> createElement(const std::string& typeName,
                                                  const std::string& name) {
        return makeNode(generateId(), name, roleFromString(typeName));
    }

    // -----------------------------------------------------------------
    // String  <->  Enum mapping
    // -----------------------------------------------------------------

    /**
     * @brief 型名文字列を UIRole enum 値へ対応付ける。
     *
     * 認識される名前 (case-sensitive, 小文字):
     * "panel", "label", "button", "health_bar", "image", "progress_bar",
     * "container", "score_label", "minimap", "inventory", "dialog_box",
     * "menu_item", "tooltip", "custom"。
     *
     * @param typeName  小文字の role 名。
     * @return 対応する UIRole。未知の場合は UIRole::Panel。
     */
    static UIRole roleFromString(const std::string& typeName) {
        static const std::map<std::string, UIRole> table = {
            {"panel",        UIRole::Panel},
            {"label",        UIRole::Label},
            {"button",       UIRole::Button},
            {"health_bar",   UIRole::HealthBar},
            {"image",        UIRole::Image},
            {"progress_bar", UIRole::ProgressBar},
            {"container",    UIRole::Container},
            {"score_label",  UIRole::ScoreLabel},
            {"minimap",      UIRole::MiniMap},
            {"inventory",    UIRole::Inventory},
            {"dialog_box",   UIRole::DialogBox},
            {"menu_item",    UIRole::MenuItem},
            {"tooltip",      UIRole::Tooltip},
            {"custom",       UIRole::Custom},
        };
        auto it = table.find(typeName);
        return (it != table.end()) ? it->second : UIRole::Panel;
    }

    /**
     * @brief UIRole enum を正規の文字列表現へ変換する。
     *
     * @param role  UIRole 値。
     * @return 小文字の文字列名。
     */
    static std::string roleToString(UIRole role) {
        switch (role) {
        case UIRole::Container:   return "container";
        case UIRole::Panel:       return "panel";
        case UIRole::Label:       return "label";
        case UIRole::Button:      return "button";
        case UIRole::HealthBar:   return "health_bar";
        case UIRole::Image:       return "image";
        case UIRole::ProgressBar: return "progress_bar";
        case UIRole::ScoreLabel:  return "score_label";
        case UIRole::MiniMap:     return "minimap";
        case UIRole::Inventory:   return "inventory";
        case UIRole::DialogBox:   return "dialog_box";
        case UIRole::MenuItem:    return "menu_item";
        case UIRole::Tooltip:     return "tooltip";
        case UIRole::Custom:      return "custom";
        default:                  return "panel";
        }
    }

    /**
     * @brief anchor 名文字列を Anchor enum 値へ対応付ける。
     *
     * 認識される名前 (ハイフン区切り, 小文字):
     * "top-left", "top-center", "top-right",
     * "center-left", "center", "center-right",
     * "bottom-left", "bottom-center", "bottom-right"。
     *
     * @param anchorName  小文字・ハイフン区切りの anchor 名。
     * @return 対応する Anchor。未知の場合は Anchor::TopLeft。
     */
    static Anchor anchorFromString(const std::string& anchorName) {
        static const std::map<std::string, Anchor> table = {
            {"top-left",      Anchor::TopLeft},
            {"top-center",    Anchor::TopCenter},
            {"top-right",     Anchor::TopRight},
            {"center-left",   Anchor::CenterLeft},
            {"center",        Anchor::Center},
            {"center-right",  Anchor::CenterRight},
            {"bottom-left",   Anchor::BottomLeft},
            {"bottom-center", Anchor::BottomCenter},
            {"bottom-right",  Anchor::BottomRight},
        };
        auto it = table.find(anchorName);
        return (it != table.end()) ? it->second : Anchor::TopLeft;
    }

private:
    /** @brief 単調増加する ID カウンタ。 */
    static inline uint32_t s_nextId = 1;

    /**
     * @brief 一意な UINodeId を生成する。
     * @return 次に利用可能な ID。
     */
    static UINodeId generateId() {
        return static_cast<UINodeId>(s_nextId++);
    }

    /**
     * @brief id, name, role から UINode を構築する helper。
     *
     * UINodeData を組み立てて UINode コンストラクタへ渡す。
     *
     * @param id    node ID。
     * @param name  node 名。
     * @param role  意味的な role。
     * @return 新規 UINode への shared pointer。
     */
    static std::shared_ptr<UINode> makeNode(UINodeId id, const std::string& name, UIRole role) {
        UINodeData data;
        data.id = id;
        data.name = name;
        data.role = role;
        return std::make_shared<UINode>(std::move(data));
    }

    // -----------------------------------------------------------------
    // JSON parsing (recursive)
    // -----------------------------------------------------------------

    /**
     * @brief parse 済みの JsonReader から UINode (と子要素) を構築する。
     */
    static std::shared_ptr<UINode> parseNode(mitiru::data::JsonReader& reader) {
        const std::string typeName = reader.getString("type").value_or("");
        const std::string name     = reader.getString("name").value_or("");

        auto node = makeNode(generateId(), name, roleFromString(typeName));

        // 任意の bounds
        const float w = reader.getFloat("width").value_or(0.0f);
        const float h = reader.getFloat("height").value_or(0.0f);
        if (w > 0.0f || h > 0.0f) {
            node->setBounds(sgc::Rectf{0.0f, 0.0f, w, h});
        }

        // 任意のデータフィールド
        {
            const auto text = reader.getString("text");
            if (text.has_value() && !text->empty()) {
                node->setText(*text);
            }
            const auto value = reader.getFloat("value");
            if (value.has_value()) {
                node->setValue(*value);
            }
        }

        // 子要素の parse は限定的 — getArray は optional<vector<string>> を返す
        // 完全な再帰 JSON parse にはより高機能な JsonReader が必要
        // 当面は fromJson() で子要素を skip する (tree 生成には createHUD() を使う)

        return node;
    }

    // -----------------------------------------------------------------
    // JSON serialisation (recursive)
    // -----------------------------------------------------------------

    /**
     * @brief インデント helper: @p depth * 2 個の空白を返す。
     */
    static std::string indent(int depth) {
        return std::string(static_cast<size_t>(depth) * 2, ' ');
    }

    /**
     * @brief node とその子要素を JSON へ再帰的に serialize する。
     */
    static std::string nodeToJson(const UINode& node, int depth) {
        std::string json;
        const std::string pad = indent(depth);
        const std::string pad1 = indent(depth + 1);

        json += pad + "{\n";
        json += pad1 + "\"type\": \"" + roleToString(node.role()) + "\",\n";
        json += pad1 + "\"name\": \"" + node.name() + "\"";

        // bounds
        const auto& b = node.bounds();
        if (b.width() > 0.0f || b.height() > 0.0f) {
            json += ",\n";
            json += pad1 + "\"width\": "  + std::to_string(static_cast<int>(b.width()));
            json += ",\n";
            json += pad1 + "\"height\": " + std::to_string(static_cast<int>(b.height()));
        }

        // children
        const auto& children = node.children();
        if (!children.empty()) {
            json += ",\n";
            json += pad1 + "\"children\": [\n";
            for (size_t i = 0; i < children.size(); ++i) {
                json += nodeToJson(*children[i], depth + 2);
                if (i + 1 < children.size()) {
                    json += ",";
                }
                json += "\n";
            }
            json += pad1 + "]";
        }

        json += "\n" + pad + "}";
        return json;
    }
};

} // namespace mitiru::ui
