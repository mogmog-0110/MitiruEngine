#pragma once
// stage_edit の絵を CPU で塗る小さな画布。線、塗り、円、数字 (目盛り用の 3x5 の点字体)。RGBA8、行の詰め無し。
// GPU にも RmlUi にも触らないので、窓なしの試験で絵の画素まで確かめられる。

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru::tool::stageedit
{

struct Rgba
{
	std::uint8_t r = 0;
	std::uint8_t g = 0;
	std::uint8_t b = 0;
	std::uint8_t a = 255;
};

class Canvas
{
public:
	Canvas(int width, int height);

	[[nodiscard]] int width() const noexcept { return m_w; }
	[[nodiscard]] int height() const noexcept { return m_h; }
	[[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept { return m_px; }
	[[nodiscard]] std::vector<std::uint8_t> takePixels() { return std::move(m_px); }
	[[nodiscard]] Rgba at(int x, int y) const;

	void clear(Rgba c);
	void blend(int x, int y, Rgba c);
	void line(double x0, double y0, double x1, double y1, Rgba c, int thickness = 1);
	void fillRect(double x0, double y0, double x1, double y1, Rgba c);
	void strokeRect(double x0, double y0, double x1, double y1, Rgba c, int thickness = 1);
	void fillCircle(double cx, double cy, double r, Rgba c);
	void strokeCircle(double cx, double cy, double r, Rgba c, int thickness = 1);
	/// 偶奇で塗る多角形
	void fillPolygon(const std::vector<std::pair<double, double>>& pts, Rgba c);
	/// 数字と - . だけの小さな字。scale 倍して (x, y) を左上に置く
	void digits(int x, int y, std::string_view text, Rgba c, int scale = 2);
	[[nodiscard]] static int digitsWidth(std::string_view text, int scale = 2);

private:
	int m_w;
	int m_h;
	std::vector<std::uint8_t> m_px;
};

} // namespace mitiru::tool::stageedit
