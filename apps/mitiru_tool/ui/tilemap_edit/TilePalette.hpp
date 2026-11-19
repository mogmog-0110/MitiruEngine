#pragma once
// タイルセットの PNG がないか読めないときは、スキーマの色の四角で描く

#include "../ImageDecode.hpp"
#include "../stage_edit/Canvas.hpp"
#include "TileDoc.hpp"
#include "TileSchema.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mitiru::tool::tilemapedit
{

inline constexpr int kPickerWidth = 300;

class TilePalette
{
public:
	static constexpr int kSwatch = 32;
	static constexpr int kGap = 4;

	TilePalette(const TileSchema& schema, std::optional<DecodedImage> image);

	[[nodiscard]] const std::vector<int>& ids() const noexcept { return m_ids; }
	[[nodiscard]] bool hasImage() const noexcept { return m_image.has_value(); }
	[[nodiscard]] std::string label(int id) const;
	[[nodiscard]] stageedit::Rgb color(int id) const;

	/// 向きは反転してから回す。回転は 90 度単位
	void paint(stageedit::Canvas& canvas, double x, double y, double size, const Cell& cell, int alpha = 255) const;

	[[nodiscard]] int pickerHeight(int width) const;
	[[nodiscard]] stageedit::Canvas renderPicker(int width, const Cell& brush) const;
	/// ID 0 は消しゴム
	[[nodiscard]] std::optional<int> pickAt(int width, double sx, double sy) const;

private:
	[[nodiscard]] int columns(int width) const;
	[[nodiscard]] bool imageCell(int id, int& sx, int& sy) const;

	const TileSchema* m_schema;
	std::optional<DecodedImage> m_image;
	std::vector<int> m_ids;
	int m_tilesetCount = 0;
};

} // namespace mitiru::tool::tilemapedit
