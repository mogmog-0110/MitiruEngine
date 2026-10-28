// layoutText の実装。文字列を Item に分け、行分けして位置を決める。各段階の実装は TextLayoutItems.cpp と TextLayoutPlace.cpp にある。

#include <mitiru/text/TextLayout.hpp>

#include "TextLayoutItems.hpp"

namespace mitiru::text
{

void layoutText(FontFace& face, std::string_view text, const LayoutOptions& options, TextLayout& out)
{
	out.clear();
	if (text.empty() || options.fontSize <= 0.0f) { return; }
	// 毎フレーム呼ぶため、作業用の入れ物を使い回す。Item は text を指すため、呼び出しをまたいで保持しない。
	static thread_local detail::ItemSet items;
	static thread_local std::vector<detail::LineRange> lines;
	detail::buildItems(face, text, options, items);
	detail::breakLines(items, options.extent, lines);
	detail::placeLines(face, options, items, lines, out);
}

} // namespace mitiru::text
