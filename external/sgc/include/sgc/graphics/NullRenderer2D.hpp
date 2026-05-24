#pragma once

/// @file NullRenderer2D.hpp
/// @brief IRenderer2Dのヌル実装（テスト用）
///
/// 全描画メソッドはno-opだが、呼び出し回数と最後のパラメータを記録する。
/// ユニットテストでの描画検証に使用する。
///
/// @code
/// sgc::graphics::NullRenderer2D renderer;
/// renderer.drawCircle({100, 200}, 30.0f, Color::red());
/// assert(renderer.drawCallCount() == 1);
/// assert(renderer.lastCircleCenter().x == 100.0f);
/// @endcode

#include "sgc/graphics/IRenderer2D.hpp"

namespace sgc::graphics
{

/// @brief IRenderer2Dのヌル実装（テスト・ヘッドレス環境用）
///
/// 全描画メソッドは何も描画しないが、以下を記録する:
/// - 各描画メソッドの呼び出し回数
/// - 最後の描画パラメータ（位置、色、サイズ等）
class NullRenderer2D : public IRenderer2D
{
public:
	/// @brief コンストラクタ
	/// @param screenSize スクリーンサイズ（デフォルト: 1280x720）
	explicit NullRenderer2D(Vec2f screenSize = {1280.0f, 720.0f}) noexcept
		: m_screenSize(screenSize)
	{
	}

	// ── 描画メソッド（no-op + 記録）──────────────────

	void drawCircle(Vec2f center, float radius, Color fill) override
	{
		++m_circleCount;
		++m_totalDrawCalls;
		m_lastCircleCenter = center;
		m_lastCircleRadius = radius;
		m_lastColor = fill;
	}

	void drawCircleOutline(Vec2f center, float radius, Color stroke, float thickness) override
	{
		++m_circleOutlineCount;
		++m_totalDrawCalls;
		m_lastCircleCenter = center;
		m_lastCircleRadius = radius;
		m_lastColor = stroke;
		m_lastThickness = thickness;
	}

	void drawRect(Vec2f pos, Vec2f size, Color fill) override
	{
		++m_rectCount;
		++m_totalDrawCalls;
		m_lastRectPos = pos;
		m_lastRectSize = size;
		m_lastColor = fill;
	}

	void drawRectOutline(Vec2f pos, Vec2f size, Color stroke, float thickness) override
	{
		++m_rectOutlineCount;
		++m_totalDrawCalls;
		m_lastRectPos = pos;
		m_lastRectSize = size;
		m_lastColor = stroke;
		m_lastThickness = thickness;
	}

	void drawLine(Vec2f from, Vec2f to, Color color, float thickness) override
	{
		++m_lineCount;
		++m_totalDrawCalls;
		m_lastLineFrom = from;
		m_lastLineTo = to;
		m_lastColor = color;
		m_lastThickness = thickness;
	}

	void drawTriangle(Vec2f a, Vec2f b, Vec2f c, Color fill) override
	{
		++m_triangleCount;
		++m_totalDrawCalls;
		m_lastTriA = a;
		m_lastTriB = b;
		m_lastTriC = c;
		m_lastColor = fill;
	}

	void drawPolygon(const std::vector<Vec2f>& points, Color fill) override
	{
		++m_polygonCount;
		++m_totalDrawCalls;
		m_lastPolygonPointCount = static_cast<int>(points.size());
		m_lastColor = fill;
	}

	void drawBezier(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3,
	                Color color, float thickness, int segments) override
	{
		++m_bezierCount;
		++m_totalDrawCalls;
		m_lastColor = color;
		m_lastThickness = thickness;
		m_lastBezierSegments = segments;
	}

	void drawText(Vec2f pos, const std::string& text, float fontSize, Color color) override
	{
		++m_textCount;
		++m_totalDrawCalls;
		m_lastTextPos = pos;
		m_lastText = text;
		m_lastFontSize = fontSize;
		m_lastColor = color;
	}

	void setViewMatrix(const Mat4f& mat) override
	{
		m_viewMatrix = mat;
		m_viewMatrixSetCount++;
	}

	[[nodiscard]] Vec2f getScreenSize() const override
	{
		return m_screenSize;
	}

	void beginFrame() override
	{
		++m_beginFrameCount;
	}

	void endFrame() override
	{
		++m_endFrameCount;
	}

	// ── カウンタ・記録の取得 ──────────────────────────

	/// @brief 合計描画コール数を取得する
	[[nodiscard]] int drawCallCount() const noexcept { return m_totalDrawCalls; }

	/// @brief drawCircle呼び出し回数を取得する
	[[nodiscard]] int circleCount() const noexcept { return m_circleCount; }

	/// @brief drawCircleOutline呼び出し回数を取得する
	[[nodiscard]] int circleOutlineCount() const noexcept { return m_circleOutlineCount; }

	/// @brief drawRect呼び出し回数を取得する
	[[nodiscard]] int rectCount() const noexcept { return m_rectCount; }

	/// @brief drawRectOutline呼び出し回数を取得する
	[[nodiscard]] int rectOutlineCount() const noexcept { return m_rectOutlineCount; }

	/// @brief drawLine呼び出し回数を取得する
	[[nodiscard]] int lineCount() const noexcept { return m_lineCount; }

	/// @brief drawTriangle呼び出し回数を取得する
	[[nodiscard]] int triangleCount() const noexcept { return m_triangleCount; }

	/// @brief drawPolygon呼び出し回数を取得する
	[[nodiscard]] int polygonCount() const noexcept { return m_polygonCount; }

	/// @brief drawBezier呼び出し回数を取得する
	[[nodiscard]] int bezierCount() const noexcept { return m_bezierCount; }

	/// @brief drawText呼び出し回数を取得する
	[[nodiscard]] int textCount() const noexcept { return m_textCount; }

	/// @brief beginFrame呼び出し回数を取得する
	[[nodiscard]] int beginFrameCount() const noexcept { return m_beginFrameCount; }

	/// @brief endFrame呼び出し回数を取得する
	[[nodiscard]] int endFrameCount() const noexcept { return m_endFrameCount; }

	/// @brief setViewMatrix呼び出し回数を取得する
	[[nodiscard]] int viewMatrixSetCount() const noexcept { return m_viewMatrixSetCount; }

	// ── 最後の描画パラメータ ──────────────────────────

	/// @brief 最後のdrawCircleの中心座標
	[[nodiscard]] Vec2f lastCircleCenter() const noexcept { return m_lastCircleCenter; }

	/// @brief 最後のdrawCircleの半径
	[[nodiscard]] float lastCircleRadius() const noexcept { return m_lastCircleRadius; }

	/// @brief 最後のdrawRectの位置
	[[nodiscard]] Vec2f lastRectPos() const noexcept { return m_lastRectPos; }

	/// @brief 最後のdrawRectのサイズ
	[[nodiscard]] Vec2f lastRectSize() const noexcept { return m_lastRectSize; }

	/// @brief 最後のdrawLineの始点
	[[nodiscard]] Vec2f lastLineFrom() const noexcept { return m_lastLineFrom; }

	/// @brief 最後のdrawLineの終点
	[[nodiscard]] Vec2f lastLineTo() const noexcept { return m_lastLineTo; }

	/// @brief 最後のdrawTextの位置
	[[nodiscard]] Vec2f lastTextPos() const noexcept { return m_lastTextPos; }

	/// @brief 最後のdrawTextのテキスト
	[[nodiscard]] const std::string& lastText() const noexcept { return m_lastText; }

	/// @brief 最後のフォントサイズ
	[[nodiscard]] float lastFontSize() const noexcept { return m_lastFontSize; }

	/// @brief 最後の描画色
	[[nodiscard]] Color lastColor() const noexcept { return m_lastColor; }

	/// @brief 最後の線の太さ
	[[nodiscard]] float lastThickness() const noexcept { return m_lastThickness; }

	/// @brief 現在のビュー行列
	[[nodiscard]] const Mat4f& viewMatrix() const noexcept { return m_viewMatrix; }

	// ── リセット ──────────────────────────────────────

	/// @brief 全カウンタと記録をリセットする
	void resetCounters() noexcept
	{
		m_totalDrawCalls = 0;
		m_circleCount = 0;
		m_circleOutlineCount = 0;
		m_rectCount = 0;
		m_rectOutlineCount = 0;
		m_lineCount = 0;
		m_triangleCount = 0;
		m_polygonCount = 0;
		m_bezierCount = 0;
		m_textCount = 0;
		m_beginFrameCount = 0;
		m_endFrameCount = 0;
		m_viewMatrixSetCount = 0;

		m_lastCircleCenter = {};
		m_lastCircleRadius = 0.0f;
		m_lastRectPos = {};
		m_lastRectSize = {};
		m_lastLineFrom = {};
		m_lastLineTo = {};
		m_lastTriA = {};
		m_lastTriB = {};
		m_lastTriC = {};
		m_lastTextPos = {};
		m_lastText.clear();
		m_lastFontSize = 0.0f;
		m_lastColor = {};
		m_lastThickness = 0.0f;
		m_lastPolygonPointCount = 0;
		m_lastBezierSegments = 0;
		m_viewMatrix = Mat4f::identity();
	}

	/// @brief スクリーンサイズを変更する
	/// @param size 新しいスクリーンサイズ
	void setScreenSize(Vec2f size) noexcept
	{
		m_screenSize = size;
	}

private:
	Vec2f m_screenSize{1280.0f, 720.0f};  ///< スクリーンサイズ

	// カウンタ
	int m_totalDrawCalls = 0;
	int m_circleCount = 0;
	int m_circleOutlineCount = 0;
	int m_rectCount = 0;
	int m_rectOutlineCount = 0;
	int m_lineCount = 0;
	int m_triangleCount = 0;
	int m_polygonCount = 0;
	int m_bezierCount = 0;
	int m_textCount = 0;
	int m_beginFrameCount = 0;
	int m_endFrameCount = 0;
	int m_viewMatrixSetCount = 0;

	// 最後の描画パラメータ
	Vec2f m_lastCircleCenter{};
	float m_lastCircleRadius = 0.0f;
	Vec2f m_lastRectPos{};
	Vec2f m_lastRectSize{};
	Vec2f m_lastLineFrom{};
	Vec2f m_lastLineTo{};
	Vec2f m_lastTriA{};
	Vec2f m_lastTriB{};
	Vec2f m_lastTriC{};
	Vec2f m_lastTextPos{};
	std::string m_lastText;
	float m_lastFontSize = 0.0f;
	Color m_lastColor{};
	float m_lastThickness = 0.0f;
	int m_lastPolygonPointCount = 0;
	int m_lastBezierSegments = 0;
	Mat4f m_viewMatrix = Mat4f::identity();
};

} // namespace sgc::graphics
