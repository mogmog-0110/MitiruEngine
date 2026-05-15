#pragma once

/**
 * @file UITemplate.hpp
 * @brief JSON-driven UI tree generation and serialisation.
 *
 * Converts between a compact JSON template format and the runtime UINode tree.
 * Provides factory helpers for common HUD layouts and element creation.
 *
 * @par JSON template format
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
 * @brief Bidirectional converter between JSON templates and UINode trees.
 *
 * All public methods are static; the class is not meant to be instantiated.
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
     * @brief Parse a JSON template string into a UI node tree.
     *
     * Uses @c mitiru::data::JsonReader internally. Each JSON object becomes
     * a UINode; nested @c "children" arrays produce child nodes recursively.
     *
     * @param jsonTemplate  Well-formed JSON string.
     * @return Shared pointer to the root UINode, or nullptr on parse failure.
     */
    static std::shared_ptr<UINode> fromJson(const std::string& jsonTemplate) {
        mitiru::data::JsonReader reader;
        if (!reader.parse(jsonTemplate)) {
            return nullptr;
        }
        return parseNode(reader);
    }

    /**
     * @brief Serialise an existing UI tree back to a JSON template string.
     *
     * @param root  The root node of the tree to serialise.
     * @return Pretty-printed JSON string.
     */
    static std::string toJson(const UINode& root) {
        return nodeToJson(root, 0);
    }

    // -----------------------------------------------------------------
    // Factory helpers
    // -----------------------------------------------------------------

    /**
     * @brief Create a standard game HUD layout.
     *
     * The returned tree contains:
     * - Health bar (top-left)
     * - Score label (top-right)
     * - Minimap placeholder (bottom-right)
     *
     * @param screenW  Viewport width in pixels.
     * @param screenH  Viewport height in pixels.
     * @return Shared pointer to the root HUD node.
     */
    static std::shared_ptr<UINode> createHUD(float screenW, float screenH) {
        auto root = makeNode(generateId(), "hud", UIRole::Panel);
        root->setBounds(sgc::Rectf{0.0f, 0.0f, screenW, screenH});

        // Health bar -- top-left
        auto hp = makeNode(generateId(), "hp_bar", UIRole::HealthBar);
        hp->setValue(1.0f);
        hp->setBounds(sgc::Rectf{10.0f, 10.0f, 200.0f, 24.0f});
        root->addChild(hp);

        // Score label -- top-right
        auto score = makeNode(generateId(), "score_label", UIRole::Label);
        score->setText("Score: 0");
        score->setBounds(sgc::Rectf{screenW - 210.0f, 10.0f, 200.0f, 24.0f});
        root->addChild(score);

        // Minimap -- bottom-right
        auto minimap = makeNode(generateId(), "minimap", UIRole::Panel);
        minimap->setBounds(sgc::Rectf{screenW - 170.0f, screenH - 170.0f, 160.0f, 160.0f});
        root->addChild(minimap);

        return root;
    }

    /**
     * @brief Create a single UI element by its type name.
     *
     * @param typeName  Role as a lowercase string (e.g. "label", "button").
     * @param name      Human-readable name / identifier.
     * @return Shared pointer to the new node.
     */
    static std::shared_ptr<UINode> createElement(const std::string& typeName,
                                                  const std::string& name) {
        return makeNode(generateId(), name, roleFromString(typeName));
    }

    // -----------------------------------------------------------------
    // String  <->  Enum mapping
    // -----------------------------------------------------------------

    /**
     * @brief Map a type-name string to a UIRole enum value.
     *
     * Recognised names (case-sensitive, lowercase):
     * "panel", "label", "button", "health_bar", "image", "progress_bar",
     * "container", "score_label", "minimap", "inventory", "dialog_box",
     * "menu_item", "tooltip", "custom".
     *
     * @param typeName  Lowercase role name.
     * @return Corresponding UIRole; defaults to UIRole::Panel for unknowns.
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
     * @brief Convert a UIRole enum to its canonical string representation.
     *
     * @param role  The UIRole value.
     * @return Lowercase string name.
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
     * @brief Map an anchor-name string to an Anchor enum value.
     *
     * Recognised names (hyphenated, lowercase):
     * "top-left", "top-center", "top-right",
     * "center-left", "center", "center-right",
     * "bottom-left", "bottom-center", "bottom-right".
     *
     * @param anchorName  Lowercase, hyphenated anchor name.
     * @return Corresponding Anchor; defaults to Anchor::TopLeft for unknowns.
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
    /** @brief Monotonically increasing ID counter. */
    static inline uint32_t s_nextId = 1;

    /**
     * @brief Generate a unique UINodeId.
     * @return Next available ID.
     */
    static UINodeId generateId() {
        return static_cast<UINodeId>(s_nextId++);
    }

    /**
     * @brief Helper to construct a UINode from id, name, and role.
     *
     * Builds a UINodeData and passes it to the UINode constructor.
     *
     * @param id    Node ID.
     * @param name  Node name.
     * @param role  Semantic role.
     * @return Shared pointer to the new UINode.
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
     * @brief Build a UINode (and its children) from a parsed JsonReader.
     */
    static std::shared_ptr<UINode> parseNode(mitiru::data::JsonReader& reader) {
        const std::string typeName = reader.getString("type").value_or("");
        const std::string name     = reader.getString("name").value_or("");

        auto node = makeNode(generateId(), name, roleFromString(typeName));

        // Optional bounds
        const float w = reader.getFloat("width").value_or(0.0f);
        const float h = reader.getFloat("height").value_or(0.0f);
        if (w > 0.0f || h > 0.0f) {
            node->setBounds(sgc::Rectf{0.0f, 0.0f, w, h});
        }

        // Optional data fields
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

        // Children parsing is limited — getArray returns optional<vector<string>>
        // Full recursive JSON parsing requires a richer JsonReader
        // For now, skip children in fromJson() (use createHUD() for tree creation)

        return node;
    }

    // -----------------------------------------------------------------
    // JSON serialisation (recursive)
    // -----------------------------------------------------------------

    /**
     * @brief Indent helper: returns @p depth * 2 spaces.
     */
    static std::string indent(int depth) {
        return std::string(static_cast<size_t>(depth) * 2, ' ');
    }

    /**
     * @brief Recursively serialise a node and its children to JSON.
     */
    static std::string nodeToJson(const UINode& node, int depth) {
        std::string json;
        const std::string pad = indent(depth);
        const std::string pad1 = indent(depth + 1);

        json += pad + "{\n";
        json += pad1 + "\"type\": \"" + roleToString(node.role()) + "\",\n";
        json += pad1 + "\"name\": \"" + node.name() + "\"";

        // Bounds
        const auto& b = node.bounds();
        if (b.width() > 0.0f || b.height() > 0.0f) {
            json += ",\n";
            json += pad1 + "\"width\": "  + std::to_string(static_cast<int>(b.width()));
            json += ",\n";
            json += pad1 + "\"height\": " + std::to_string(static_cast<int>(b.height()));
        }

        // Children
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
