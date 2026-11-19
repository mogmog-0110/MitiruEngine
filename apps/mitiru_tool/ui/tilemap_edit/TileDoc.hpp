#pragma once
// JsonText で持つため、書き換えたタイルの行以外は 1 byte も変わらない。マスの一覧は文書と同じ内容なので、編集時は両方を直す

#include <mitiru/data/JsonText.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::tool::tilemapedit
{

struct Cell
{
	int id = 0;
	float rotation = 0.0f;    ///< 度
	bool flipX = false;
	bool flipY = false;
	[[nodiscard]] bool operator==(const Cell&) const = default;
};

using MaybeCell = std::optional<Cell>;

struct Change
{
	int layer = 0;
	int x = 0;
	int y = 0;
	MaybeCell before;
	MaybeCell after;
	int index = -1;       ///< 取り消しが消したタイルを元の位置へ戻せるように、tiles 配列の中の位置を覚える
};

/// 両端を含む
struct Rect
{
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	[[nodiscard]] static Rect of(int ax, int ay, int bx, int by);
	[[nodiscard]] int width() const noexcept { return x1 - x0 + 1; }
	[[nodiscard]] int height() const noexcept { return y1 - y0 + 1; }
};

struct Clip
{
	int width = 0;
	int height = 0;
	std::vector<MaybeCell> cells;
	[[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
	[[nodiscard]] const MaybeCell& at(int x, int y) const { return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
};

/// 反転してから回すため、反転後の回転は逆向きになる
[[nodiscard]] Cell mirrorCell(Cell cell, bool horizontal);
[[nodiscard]] Clip mirrorClip(const Clip& clip, bool horizontal);

class TileDoc
{
public:
	[[nodiscard]] bool reset(const std::string& text, std::string& error);

	[[nodiscard]] std::string text() const { return m_doc.render(); }
	[[nodiscard]] const std::string& mapName() const noexcept { return m_name; }
	[[nodiscard]] int tileWidth() const noexcept { return m_tileWidth; }
	[[nodiscard]] int tileHeight() const noexcept { return m_tileHeight; }
	[[nodiscard]] int layerCount() const noexcept { return static_cast<int>(m_layers.size()); }
	[[nodiscard]] const std::string& layerName(int layer) const { return m_layers[static_cast<std::size_t>(layer)].name; }
	[[nodiscard]] int width(int layer) const { return m_layers[static_cast<std::size_t>(layer)].width; }
	[[nodiscard]] int height(int layer) const { return m_layers[static_cast<std::size_t>(layer)].height; }
	/// 範囲外か、x または y が整数でないタイルは変更しない
	[[nodiscard]] int unmanaged(int layer) const { return m_layers[static_cast<std::size_t>(layer)].unmanaged; }
	[[nodiscard]] int tileCount(int layer) const { return m_layers[static_cast<std::size_t>(layer)].count; }
	[[nodiscard]] MaybeCell at(int layer, int x, int y) const;
	[[nodiscard]] bool inside(int layer, int x, int y) const;

	bool set(int layer, int x, int y, const MaybeCell& cell);
	void beginGroup();
	bool endGroup();

	[[nodiscard]] Clip copy(int layer, Rect rect) const;
	/// 空のマスも貼るため、コピーした範囲がそのまま再現される
	bool paste(int layer, int x, int y, const Clip& clip);
	bool clear(int layer, Rect rect);
	bool mirror(int layer, Rect rect, bool horizontal);

	[[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
	[[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
	bool undo();
	bool redo();
	[[nodiscard]] bool dirty() const noexcept { return m_undo.size() != m_keptDepth; }
	/// 文書を書き換えるたびに増える。取り消しとやり直しでも増える。自動で試すかどうかの判断に使う
	[[nodiscard]] std::uint64_t revision() const noexcept { return m_revision; }
	void markKept() noexcept { m_keptDepth = m_undo.size(); }

private:
	struct Layer
	{
		std::string name;
		std::string pointer;          ///< "layers/0"
		std::string tilesPointer;     ///< "layers/0/tiles"
		int width = 0;
		int height = 0;
		int unmanaged = 0;
		int count = 0;
		bool hasTilesArray = false;
		std::vector<MaybeCell> cells;
		std::vector<int> index;       ///< マスのタイルが tiles 配列の何番目か。無ければ -1
	};

	[[nodiscard]] bool resetChecked(const std::string& text, std::string& error);
	[[nodiscard]] static bool readLayer(const mitiru::data::JsonText& doc, std::size_t index, Layer& layer, std::string& error);
	void apply(Change& change);
	void insertTile(Layer& layer, Change& change);
	void eraseTile(Layer& layer, Change& change);
	void updateTile(Layer& layer, const Change& change);
	void commit(std::vector<Change> group);
	[[nodiscard]] Layer* layerAt(int layer);
	[[nodiscard]] const Layer* layerAt(int layer) const;

	mitiru::data::JsonText m_doc;
	std::string m_name;
	int m_tileWidth = 0;
	int m_tileHeight = 0;
	std::vector<Layer> m_layers;
	std::vector<std::vector<Change>> m_undo;
	std::vector<std::vector<Change>> m_redo;
	std::vector<Change> m_group;
	bool m_grouping = false;
	std::size_t m_keptDepth = 0;
	std::uint64_t m_revision = 0;
};

} // namespace mitiru::tool::tilemapedit
