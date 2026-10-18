#pragma once
// 真上から見た図。物を色つきの形で塗り、目盛りの数字を端に出し、クリックで物を選び、選んだ物の取っ手を引ける。
// 向き (画面の右が +x か -x か、上が +z か -z か) はスキーマが決める。

#include "Canvas.hpp"
#include "StageDoc.hpp"

#include <optional>
#include <utility>

namespace mitiru::tool::stageedit
{

class TopView
{
public:
	TopView(const Schema& schema, int width, int height);

	[[nodiscard]] int width() const noexcept { return m_w; }
	[[nodiscard]] int height() const noexcept { return m_h; }
	[[nodiscard]] double pixelsPerMeter() const noexcept { return m_ppm; }
	[[nodiscard]] double centerX() const noexcept { return m_cx; }
	[[nodiscard]] double centerZ() const noexcept { return m_cz; }

	[[nodiscard]] std::pair<double, double> toScreen(double x, double z) const;
	[[nodiscard]] std::pair<double, double> toWorld(double sx, double sy) const;

	/// 物が全部収まる位置と倍率にする
	void fit(const StageDoc& doc);
	void centerOn(double x, double z) { m_cx = x; m_cz = z; }
	void zoomAt(double sx, double sy, double factor);
	void panPixels(double dx, double dy);

	[[nodiscard]] Canvas render(const StageDoc& doc) const;
	/// (sx, sy) にある物の番号。無ければ -1。重なったら小さい物、点は線と箱より先
	[[nodiscard]] int pick(const StageDoc& doc, double sx, double sy) const;
	[[nodiscard]] std::optional<Handle> pickHandle(const StageDoc& doc, int selected, double sx, double sy) const;
	/// 目盛りの間隔 (m)
	[[nodiscard]] double gridStep() const;

private:
	void drawGrid(Canvas& canvas) const;
	void drawItem(Canvas& canvas, const StageDoc& doc, const Item& item, bool selected, double yLow, double yHigh) const;

	const Schema* m_schema;
	int m_w;
	int m_h;
	double m_cx = 0.0;
	double m_cz = 0.0;
	double m_ppm = 4.0;
};

} // namespace mitiru::tool::stageedit
