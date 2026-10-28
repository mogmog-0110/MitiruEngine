#pragma once

// 文字列を Item に分けて行に割る、組版の内部宣言。
// 公開 API は include/mitiru/text/TextLayout.hpp の layoutText だけで、このヘッダーは src/text 内でだけ使う。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mitiru/text/FontFace.hpp>
#include <mitiru/text/TextLayout.hpp>

namespace mitiru::text::detail
{

struct Item
{
	enum class Kind : std::uint8_t
	{
		Cell,     ///< 縦書きでは立てた 1 字 (塊)、横書きでは 1 字
		Run,      ///< 縦書きでは寝かせた語、横書きでは欧文の語
		Combined, ///< 縦中横
		Ruby,     ///< 親文字とルビ
		Newline,
	};

	Kind kind = Kind::Cell;
	std::string_view text;      ///< 字形を選ぶ元の文字列。Ruby は親文字全体
	std::string_view ruby;
	float length = 0.0f;        ///< 行の進む向きの長さ。Ruby は親文字とルビの長い側
	float baseLength = 0.0f;    ///< Ruby: 親文字だけの長さ
	float rubyLength = 0.0f;    ///< Ruby: ルビだけの長さ
	float shrink = 1.0f;        ///< Combined: 1 字分に収めるための縮小比
	std::uint32_t firstCp = 0;  ///< 禁則の判定に使う。Ruby は親文字の先頭と末尾
	std::uint32_t lastCp = 0;
	std::uint32_t baseBegin = 0, baseCount = 0;  ///< Ruby: ItemSet::baseItems の範囲
	std::uint32_t rubyBegin = 0, rubyCount = 0;  ///< Ruby: ItemSet::rubyCells の範囲
};

struct ItemSet
{
	std::vector<Item> items;
	std::vector<Item> baseItems;
	std::vector<Item> rubyCells;
	bool hasRuby = false;

	void clear()
	{
		items.clear();
		baseItems.clear();
		rubyCells.clear();
		hasRuby = false;
	}
};

struct LineRange
{
	std::size_t first = 0;
	std::size_t count = 0;
};

[[nodiscard]] inline bool isVertical(Writing w) noexcept { return w != Writing::Horizontal; }

/// @brief 文字列を Item に分け、ルビ記法を解く。
void buildItems(FontFace& face, std::string_view text, const LayoutOptions& options, ItemSet& out);

/// @brief Item を禁則に沿って行に割る。
void breakLines(const ItemSet& set, float extent, std::vector<LineRange>& lines);

/// @brief 各項目の前後に余りを半分ずつ配り、全体の長さ total に均等に並べる。
/// 余りがなければ詰めて並べる。
void distribute(const std::vector<float>& lengths, float total, std::vector<float>& starts);

void placeLines(FontFace& face, const LayoutOptions& options, const ItemSet& set,
                const std::vector<LineRange>& lines, TextLayout& out);

} // namespace mitiru::text::detail
