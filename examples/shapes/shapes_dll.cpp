// shapes。図形描画のカタログ。基本の図形を 4 列 x 2 段のマスに並べて見せる
// 実行すると: 紙白の画面に rect / circle / line / gradient / 破線 / 三角形 / 多角形 (五角形・六角形) が並ぶ
// 関連 API: drawRect / fillCircle / line / drawGradientRect / dashedLine / drawTriangle / drawPolygon

#include <cmath>       // std::cos / std::sin (多角形の頂点を円周上に置くのに使う)
#include <cstddef>     // std::size_t
#include <vector>      // drawPolygon に渡す頂点の配列

#include <mitiru.hpp>

#include "../common/chapter_hud.hpp"   // 章の名前ラベルと共通パレット (theme::k... の色)

using namespace mitiru;

// 見本を 4 列 x 2 段のマスに並べる。マスの左上 = (kCol[列], kRow[段])。
constexpr float kCol[4] = {20.0f, 335.0f, 650.0f, 965.0f};
constexpr float kRow[2] = {70.0f, 390.0f};
constexpr float kCellW  = 295.0f;   // マスの幅
constexpr float kCellH  = 280.0f;   // マスの高さ

struct Shapes02
{
	// 見本 1 マスぶんの枠と見出しを描く。見出しは大きめ (22px) + 濃い色ではっきり読ませる。
	void cell(Screen& s, float x, float y, const char* name) const
	{
		s.drawRectFrame(Rect{x, y, kCellW, kCellH}, theme::kFrame, 1.0f);
		s.text(name, x + 14.0f, y + 10.0f, theme::kInk, 22.0f);
	}

	// 中心 (cx,cy)・半径 r・頂点数 n の正多角形の頂点を、真上を頂点にして順に作る。
	// deg() は度をラジアンに直す。画面の y は下向きなので -90 度から始めると頂点が上を向く。
	std::vector<Vec2> regularPolygon(float cx, float cy, float r, int n) const
	{
		std::vector<Vec2> pts;
		pts.reserve(static_cast<std::size_t>(n));
		for (int k = 0; k < n; ++k)
		{
			const float a = deg(-90.0f + 360.0f / static_cast<float>(n) * static_cast<float>(k));
			pts.push_back(Vec2{cx + r * std::cos(a), cy + r * std::sin(a)});
		}
		return pts;
	}

	// 頂点をぐるりと線で結んで、多角形の枠 (ふち) を描く。
	void outline(Screen& s, const std::vector<Vec2>& pts, const Color& c, float w) const
	{
		for (std::size_t i = 0; i < pts.size(); ++i)
		{
			s.drawLine(pts[i], pts[(i + 1) % pts.size()], c, w);
		}
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "Shapes");

		// ── 上段: 四角・円・線・グラデーション ──
		float x = kCol[0], y = kRow[0];
		cell(s, x, y, "rect");
		s.drawRect(x + 30.0f, y + 70.0f, 105.0f, 80.0f, theme::kBlue);                       // 塗り
		s.drawRoundedRect(Rect{x + 155.0f, y + 70.0f, 110.0f, 80.0f}, theme::kPink, 14.0f);   // 角丸
		s.drawRectFrame(Rect{x + 30.0f, y + 175.0f, 235.0f, 75.0f}, theme::kOrange, 2.0f);    // 枠線

		x = kCol[1];
		cell(s, x, y, "circle");
		s.fillCircle(x + 80.0f, y + 130.0f, 52.0f, theme::kOrange);                  // 塗り
		s.drawRing(Vec2{x + 205.0f, y + 130.0f}, 52.0f, 36.0f, theme::kBlue);        // リング
		s.fillCircle(x + 150.0f, y + 220.0f, 28.0f, theme::kGreen);

		x = kCol[2];
		cell(s, x, y, "line");
		s.line(x + 30.0f, y + 90.0f,  x + 265.0f, y + 90.0f,  theme::kInk,  1.0f);   // 太さ 1
		s.line(x + 30.0f, y + 135.0f, x + 265.0f, y + 135.0f, theme::kBlue, 4.0f);   // 太さ 4
		s.line(x + 30.0f, y + 185.0f, x + 265.0f, y + 185.0f, theme::kPink, 10.0f);  // 太さ 10
		s.line(x + 30.0f, y + 245.0f, x + 265.0f, y + 220.0f, theme::kAmber, 2.0f);  // 斜め

		x = kCol[3];
		cell(s, x, y, "gradient");
		s.drawGradientRect(Rect{x + 30.0f, y + 70.0f, 235.0f, 80.0f}, theme::kBlue, theme::kPink);    // 上→下
		s.drawGradientRectH(Rect{x + 30.0f, y + 170.0f, 235.0f, 80.0f}, theme::kOrange, theme::kRed); // 左→右

		// ── 下段: 破線・三角形・多角形 (五角形 / 六角形) ──
		x = kCol[0]; y = kRow[1];
		cell(s, x, y, "dashedLine");
		s.dashedLine(x + 30.0f, y + 100.0f, x + 265.0f, y + 100.0f, theme::kInk,  2.0f, 12.0f, 8.0f);
		s.dashedLine(x + 30.0f, y + 155.0f, x + 265.0f, y + 155.0f, theme::kBlue, 3.0f,  6.0f, 6.0f);
		s.dashedLine(x + 30.0f, y + 235.0f, x + 265.0f, y + 200.0f, theme::kPink, 2.0f,  9.0f, 6.0f);

		// 三角形は drawTriangle (頂点 3 つ) で塗る。仕上げにふちを線でなぞる。
		x = kCol[1];
		const auto tri = regularPolygon(x + 147.0f, y + 175.0f, 92.0f, 3);
		cell(s, x, y, "triangle");
		s.drawTriangle(tri[0], tri[1], tri[2], theme::kOrange);
		outline(s, tri, theme::kInk, 2.0f);

		// 五角形・六角形は drawPolygon (頂点をまとめて渡す) で塗る。
		x = kCol[2];
		const auto penta = regularPolygon(x + 147.0f, y + 170.0f, 90.0f, 5);
		cell(s, x, y, "pentagon");
		s.drawPolygon(penta, theme::kBlue);
		outline(s, penta, theme::kInk, 2.0f);

		x = kCol[3];
		const auto hexa = regularPolygon(x + 147.0f, y + 170.0f, 90.0f, 6);
		cell(s, x, y, "hexagon");
		s.drawPolygon(hexa, theme::kGreen);
		outline(s, hexa, theme::kInk, 2.0f);
	}
};

// 実行:  mitiru_host.exe shapes/shapes.dll
MITIRU_GAME(Shapes02);
