#pragma once

/// @file DebugDraw.hpp
/// @brief ビジュアルデバッグ描画キュー
///
/// デバッグ用の幾何プリミティブをキューに蓄積し、
/// flush()でIRendererに一括描画する。
/// setEnabled(false)で描画出力を抑制できる。
///
/// @code
/// sgc::debug::DebugDraw debug;
/// debug.drawRect({{0,0},{100,50}}, sgc::Colorf::red());
/// debug.drawCircle({200,200}, 30.0f, sgc::Colorf::green());
/// debug.drawArrow({0,0}, {100,100}, sgc::Colorf::yellow());
/// debug.flush(renderer);  // IRendererに描画して内部キューをクリア
/// @endcode

#include <cmath>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

#include "sgc/graphics/IRenderer.hpp"
#include "sgc/math/Geometry.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::debug
{

/// @brief デバッグ描画コマンド: 矩形
struct RectCmd
{
	AABB2f rect;       ///< 矩形
	Colorf color;      ///< 描画色
	float thickness;   ///< 線の太さ
};

/// @brief デバッグ描画コマンド: 円
struct CircleCmd
{
	Vec2f center;      ///< 中心座標
	float radius;      ///< 半径
	Colorf color;      ///< 描画色
	float thickness;   ///< 線の太さ
};

/// @brief デバッグ描画コマンド: 線分
struct LineCmd
{
	Vec2f from;        ///< 始点
	Vec2f to;          ///< 終点
	Colorf color;      ///< 描画色
	float thickness;   ///< 線の太さ
};

/// @brief デバッグ描画コマンド: 矢印
struct ArrowCmd
{
	Vec2f from;        ///< 始点
	Vec2f to;          ///< 終点
	Colorf color;      ///< 描画色
	float thickness;   ///< 線の太さ
};

/// @brief デバッグ描画コマンド: レイ
struct RayCmd
{
	Vec2f origin;      ///< 原点
	Vec2f direction;   ///< 方向ベクトル
	float length;      ///< 長さ
	Colorf color;      ///< 描画色
};

/// @brief デバッグ描画コマンド: パス（折れ線）
struct PathCmd
{
	std::vector<Vec2f> points;  ///< 頂点列
	Colorf color;               ///< 描画色
	float thickness;            ///< 線の太さ
};

/// @brief デバッグ描画コマンド: 十字マーク
struct CrossCmd
{
	Vec2f center;      ///< 中心座標
	float size;        ///< 十字のサイズ（片側長さ）
	Colorf color;      ///< 描画色
};

/// @brief デバッグ描画コマンドの型
using DrawCommand = std::variant<RectCmd, CircleCmd, LineCmd, ArrowCmd, RayCmd, PathCmd, CrossCmd>;

/// @brief ビジュアルデバッグ描画キュー
///
/// デバッグ用のプリミティブをキューに蓄積し、flush()でIRendererに一括描画する。
/// setEnabled(false)でflush時の描画出力を抑制できる（キューへの追加は行われる）。
class DebugDraw
{
public:
	/// @brief 矩形枠を描画キューに追加する
	/// @param rect 矩形
	/// @param color 描画色
	/// @param thickness 線の太さ（デフォルト: 1.0f）
	void drawRect(const AABB2f& rect, const Colorf& color, float thickness = 1.0f)
	{
		m_commands.push_back(RectCmd{rect, color, thickness});
	}

	/// @brief 円枠を描画キューに追加する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	/// @param thickness 線の太さ（デフォルト: 1.0f）
	void drawCircle(const Vec2f& center, float radius, const Colorf& color, float thickness = 1.0f)
	{
		m_commands.push_back(CircleCmd{center, radius, color, thickness});
	}

	/// @brief 線分を描画キューに追加する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ（デフォルト: 1.0f）
	void drawLine(const Vec2f& from, const Vec2f& to, const Colorf& color, float thickness = 1.0f)
	{
		m_commands.push_back(LineCmd{from, to, color, thickness});
	}

	/// @brief 矢印を描画キューに追加する
	///
	/// 線分と先端の三角形（2本の線分）で矢印を表現する。
	///
	/// @param from 始点
	/// @param to 終点（矢印の先端）
	/// @param color 描画色
	/// @param thickness 線の太さ（デフォルト: 1.0f）
	void drawArrow(const Vec2f& from, const Vec2f& to, const Colorf& color, float thickness = 1.0f)
	{
		m_commands.push_back(ArrowCmd{from, to, color, thickness});
	}

	/// @brief レイを描画キューに追加する
	/// @param origin 原点
	/// @param direction 方向ベクトル（正規化推奨）
	/// @param length レイの長さ
	/// @param color 描画色
	void drawRay(const Vec2f& origin, const Vec2f& direction, float length, const Colorf& color)
	{
		m_commands.push_back(RayCmd{origin, direction, length, color});
	}

	/// @brief 折れ線パスを描画キューに追加する
	/// @param points 頂点列
	/// @param color 描画色
	/// @param thickness 線の太さ（デフォルト: 1.0f）
	void drawPath(std::span<const Vec2f> points, const Colorf& color, float thickness = 1.0f)
	{
		std::vector<Vec2f> pts(points.begin(), points.end());
		m_commands.push_back(PathCmd{std::move(pts), color, thickness});
	}

	/// @brief 十字マークを描画キューに追加する
	/// @param center 中心座標
	/// @param size 十字のサイズ（片側長さ）
	/// @param color 描画色
	void drawCross(const Vec2f& center, float size, const Colorf& color)
	{
		m_commands.push_back(CrossCmd{center, size, color});
	}

	/// @brief キュー内の全コマンドをIRendererに描画し、キューをクリアする
	/// @param renderer 描画先レンダラー
	///
	/// setEnabled(false)の場合、描画せずキューだけクリアする。
	void flush(IRenderer& renderer)
	{
		if (m_enabled)
		{
			for (const auto& cmd : m_commands)
			{
				executeCommand(renderer, cmd);
			}
		}
		m_commands.clear();
	}

	/// @brief キューを描画せずにクリアする
	void clear()
	{
		m_commands.clear();
	}

	/// @brief 描画の有効/無効を切り替える
	/// @param enabled trueで有効化
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
	}

	/// @brief 描画が有効かどうかを取得する
	/// @return 有効ならtrue
	[[nodiscard]] bool isEnabled() const noexcept
	{
		return m_enabled;
	}

	/// @brief キュー内の保留中コマンド数を取得する
	/// @return コマンド数
	[[nodiscard]] std::size_t pendingCount() const noexcept
	{
		return m_commands.size();
	}

private:
	std::vector<DrawCommand> m_commands;  ///< 描画コマンドキュー
	bool m_enabled = true;                ///< 描画有効フラグ

	/// @brief 単一コマンドをレンダラーに実行する
	/// @param renderer 描画先レンダラー
	/// @param cmd 実行するコマンド
	static void executeCommand(IRenderer& renderer, const DrawCommand& cmd)
	{
		std::visit([&renderer](const auto& c)
		{
			using T = std::decay_t<decltype(c)>;
			if constexpr (std::is_same_v<T, RectCmd>)
			{
				renderer.drawRectFrame(c.rect, c.thickness, c.color);
			}
			else if constexpr (std::is_same_v<T, CircleCmd>)
			{
				renderer.drawCircleFrame(c.center, c.radius, c.thickness, c.color);
			}
			else if constexpr (std::is_same_v<T, LineCmd>)
			{
				renderer.drawLine(c.from, c.to, c.thickness, c.color);
			}
			else if constexpr (std::is_same_v<T, ArrowCmd>)
			{
				// 矢印本体の線分
				renderer.drawLine(c.from, c.to, c.thickness, c.color);

				// 矢印先端の計算
				const float dx = c.to.x - c.from.x;
				const float dy = c.to.y - c.from.y;
				const float len = std::sqrt(dx * dx + dy * dy);
				if (len > 0.0f)
				{
					const float headLen = len * 0.2f;
					const float nx = dx / len;
					const float ny = dy / len;
					// 矢頭の2本の線
					const float angle = 0.4f;  // 約23度
					const float cosA = std::cos(angle);
					const float sinA = std::sin(angle);
					const Vec2f left
					{
						c.to.x - headLen * (nx * cosA - ny * sinA),
						c.to.y - headLen * (ny * cosA + nx * sinA)
					};
					const Vec2f right
					{
						c.to.x - headLen * (nx * cosA + ny * sinA),
						c.to.y - headLen * (ny * cosA - nx * sinA)
					};
					renderer.drawLine(c.to, left, c.thickness, c.color);
					renderer.drawLine(c.to, right, c.thickness, c.color);
				}
			}
			else if constexpr (std::is_same_v<T, RayCmd>)
			{
				const Vec2f end
				{
					c.origin.x + c.direction.x * c.length,
					c.origin.y + c.direction.y * c.length
				};
				renderer.drawLine(c.origin, end, 1.0f, c.color);
			}
			else if constexpr (std::is_same_v<T, PathCmd>)
			{
				for (std::size_t i = 0; i + 1 < c.points.size(); ++i)
				{
					renderer.drawLine(c.points[i], c.points[i + 1], c.thickness, c.color);
				}
			}
			else if constexpr (std::is_same_v<T, CrossCmd>)
			{
				// 水平線
				renderer.drawLine(
					{c.center.x - c.size, c.center.y},
					{c.center.x + c.size, c.center.y},
					1.0f, c.color);
				// 垂直線
				renderer.drawLine(
					{c.center.x, c.center.y - c.size},
					{c.center.x, c.center.y + c.size},
					1.0f, c.color);
			}
		}, cmd);
	}
};

} // namespace sgc::debug
