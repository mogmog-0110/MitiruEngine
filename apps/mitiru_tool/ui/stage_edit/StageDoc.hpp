#pragma once
// stage_edit が編集している 1 枚のステージ。文書は JsonText (書式を保つ) で持ち、物の一覧は文書から毎回作り直す。
// 編集はすべて「文書の書き換え」で、取り消しは書き換え前の文字列を積む。ゲームにもファイルにも触らない。

#include "EditSchema.hpp"
#include "Geometry.hpp"

#include <mitiru/data/JsonText.hpp>

#include <optional>
#include <string>
#include <vector>

namespace mitiru::tool::stageedit
{

struct Item
{
	int kind = 0;
	int index = -1;          ///< 配列の中の番号。single は -1
	std::string pointer;     ///< 文書の中の位置 ("boxes/3")
	Geom geom;
};

/// パネルの 1 行 (欄 1 つ)
struct FieldRow
{
	std::string key;
	std::string label;
	FieldType type = FieldType::Number;
	bool present = true;
	bool optional = false;
	std::vector<std::string> comps;    ///< 数の欄の成分 (文字)
	std::vector<std::string> compNames;
	bool flag = false;
	std::string text;
};

class StageDoc
{
public:
	/// schema は文書より長く生きること
	[[nodiscard]] bool load(const Schema* schema, const std::string& text, std::string& error);

	[[nodiscard]] std::string text() const { return m_doc.render(); }
	[[nodiscard]] const std::string& keptText() const noexcept { return m_kept; }
	/// 今の文書を「残した版」にする (残すを押した後)
	void markKept() { m_kept = text(); }
	[[nodiscard]] bool dirty() const { return text() != m_kept; }

	[[nodiscard]] const std::vector<Item>& items() const noexcept { return m_items; }
	[[nodiscard]] int unmanagedCount() const noexcept { return m_unmanaged; }
	[[nodiscard]] const Schema& schema() const noexcept { return *m_schema; }
	[[nodiscard]] int selected() const noexcept { return m_selected; }
	void select(int item) { m_selected = item >= 0 && item < static_cast<int>(m_items.size()) ? item : -1; }

	/// ドラッグ 1 回ぶんの区切り。begin の時点の文書が基準で、move / handle は基準からの書き換えを何度でも言い直せる
	void beginGesture(int item);
	void gestureMove(double dx, double dy, double dz);
	void gestureHandle(const Handle& handle, double wx, double wz);
	/// @return 文書が変わっていたら true (変わったときだけ取り消しに積む)
	bool endGesture();

	bool nudge(double dx, double dy, double dz);
	bool rotate(double deg);
	bool duplicate();
	bool remove();
	/// kind の物を (x, z) の辺りへ足して選ぶ。足せなければ false
	bool add(int kind, double x, double z);

	[[nodiscard]] std::vector<FieldRow> rows(int item) const;
	bool setField(int item, const std::string& key, int comp, const std::string& value);
	bool toggleField(int item, const std::string& key);
	bool addField(int item, const std::string& key);
	bool removeField(int item, const std::string& key);

	[[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
	[[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
	bool undo();
	bool redo();
	/// 取り消しの履歴ごと、文書を text に置き換える (捨てる / 開き直し)
	bool reset(const std::string& text, std::string& error);

	[[nodiscard]] const mitiru::data::JsonText& json() const noexcept { return m_doc; }

private:
	void refresh();
	void pushUndo(std::string before);
	[[nodiscard]] bool matches(const Kind& kind, const nlohmann::ordered_json& value) const;
	[[nodiscard]] Item* selectedItem();
	bool replaceWith(const std::string& text);

	const Schema* m_schema = nullptr;
	mitiru::data::JsonText m_doc;
	std::string m_kept;
	std::vector<Item> m_items;
	int m_unmanaged = 0;
	int m_selected = -1;
	std::vector<std::string> m_undo;
	std::vector<std::string> m_redo;
	std::optional<mitiru::data::JsonText> m_base;
	std::string m_baseText;
	Item m_baseItem;
};

} // namespace mitiru::tool::stageedit
