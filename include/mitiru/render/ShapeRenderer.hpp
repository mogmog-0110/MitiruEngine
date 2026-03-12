#pragma once

/// @file ShapeRenderer.hpp
/// @brief プリミティブシェイプレンダラー
/// @details 円・線分・三角形・ポリゴンなどの基本図形を頂点生成して描画する。

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex2D.hpp>

namespace mitiru::render
{

/// @brief プリミティブシェイプレンダラー
/// @details 基本図形を頂点データとして生成する。
///          Phase 1 ではGPU送信はスタブ。
///
/// @code
/// mitiru::render::ShapeRenderer shapes;
/// shapes.drawCircle({400, 300}, 50.0f, sgc::Colorf::green(), 32);
/// shapes.drawLine({0, 0}, {100, 100}, sgc::Colorf::white(), 2.0f);
/// shapes.flush();
/// @endcode
class ShapeRenderer
{
public:
	/// @brief 円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	/// @param segments 分割数（多いほど滑らか）
	void drawCircle(const sgc::Vec2f& center,
	                float radius,
	                const sgc::Colorf& color,
	                int segments = 32)
	{
		if (segments < 3)
		{
			return;
		}

		/// 中心頂点を追加
		const auto centerIdx = static_cast<std::uint32_t>(m_vertices.size());
		m_vertices.emplace_back(center, color);

		/// 円周上の頂点を計算する
		const float angleStep = 2.0f * PI / static_cast<float>(segments);

		for (int i = 0; i <= segments; ++i)
		{
			const float angle = angleStep * static_cast<float>(i);
			const sgc::Vec2f pos
			{
				center.x + radius * std::cos(angle),
				center.y + radius * std::sin(angle)
			};
			m_vertices.emplace_back(pos, color);
		}

		/// 三角形ファンとしてインデックスを生成する
		for (int i = 0; i < segments; ++i)
		{
			m_indices.push_back(centerIdx);
			m_indices.push_back(centerIdx + 1 + static_cast<std::uint32_t>(i));
			m_indices.push_back(centerIdx + 2 + static_cast<std::uint32_t>(i));
		}

		++m_drawCallCount;
	}

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawLine(const sgc::Vec2f& from,
	              const sgc::Vec2f& to,
	              const sgc::Colorf& color,
	              float thickness = 1.0f)
	{
		/// 線分に垂直な方向を計算する
		const sgc::Vec2f dir{to.x - from.x, to.y - from.y};
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);

		if (len < 1e-6f)
		{
			return;
		}

		const float halfThick = thickness * 0.5f;
		const sgc::Vec2f perp{-dir.y / len * halfThick, dir.x / len * halfThick};

		const auto baseIdx = static_cast<std::uint32_t>(m_vertices.size());

		/// 太さを持つ矩形として4頂点を生成する
		m_vertices.emplace_back(sgc::Vec2f{from.x + perp.x, from.y + perp.y}, color);
		m_vertices.emplace_back(sgc::Vec2f{from.x - perp.x, from.y - perp.y}, color);
		m_vertices.emplace_back(sgc::Vec2f{to.x - perp.x, to.y - perp.y}, color);
		m_vertices.emplace_back(sgc::Vec2f{to.x + perp.x, to.y + perp.y}, color);

		m_indices.push_back(baseIdx);
		m_indices.push_back(baseIdx + 1);
		m_indices.push_back(baseIdx + 2);
		m_indices.push_back(baseIdx);
		m_indices.push_back(baseIdx + 2);
		m_indices.push_back(baseIdx + 3);

		++m_drawCallCount;
	}

	/// @brief 三角形を描画する
	/// @param p0 頂点0
	/// @param p1 頂点1
	/// @param p2 頂点2
	/// @param color 描画色
	void drawTriangle(const sgc::Vec2f& p0,
	                  const sgc::Vec2f& p1,
	                  const sgc::Vec2f& p2,
	                  const sgc::Colorf& color)
	{
		const auto baseIdx = static_cast<std::uint32_t>(m_vertices.size());

		m_vertices.emplace_back(p0, color);
		m_vertices.emplace_back(p1, color);
		m_vertices.emplace_back(p2, color);

		m_indices.push_back(baseIdx);
		m_indices.push_back(baseIdx + 1);
		m_indices.push_back(baseIdx + 2);

		++m_drawCallCount;
	}

	/// @brief ポリゴンを描画する（凸ポリゴンを三角形ファンで分割）
	/// @param points 頂点座標の配列（凸ポリゴン前提）
	/// @param color 描画色
	void drawPolygon(std::span<const sgc::Vec2f> points,
	                 const sgc::Colorf& color)
	{
		if (points.size() < 3)
		{
			return;
		}

		const auto baseIdx = static_cast<std::uint32_t>(m_vertices.size());

		for (const auto& pt : points)
		{
			m_vertices.emplace_back(pt, color);
		}

		/// 三角形ファンで分割する
		for (std::size_t i = 1; i + 1 < points.size(); ++i)
		{
			m_indices.push_back(baseIdx);
			m_indices.push_back(baseIdx + static_cast<std::uint32_t>(i));
			m_indices.push_back(baseIdx + static_cast<std::uint32_t>(i + 1));
		}

		++m_drawCallCount;
	}

	/// @brief 蓄積データをフラッシュする
	/// @details GPU描画はRenderPipeline2DがsubmitBatch()で行う。
	///          flush()はデータをクリアするのみ。
	void flush() noexcept
	{
		m_vertices.clear();
		m_indices.clear();
		m_drawCallCount = 0;
	}

	/// @brief 蓄積された頂点数を取得する
	[[nodiscard]] std::size_t vertexCount() const noexcept
	{
		return m_vertices.size();
	}

	/// @brief 蓄積されたインデックス数を取得する
	[[nodiscard]] std::size_t indexCount() const noexcept
	{
		return m_indices.size();
	}

	/// @brief 描画コール数を取得する
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 蓄積された頂点データを取得する
	[[nodiscard]] const std::vector<Vertex2D>& vertices() const noexcept
	{
		return m_vertices;
	}

	/// @brief 蓄積されたインデックスデータを取得する
	[[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept
	{
		return m_indices;
	}

private:
	/// @brief 円周率定数
	static constexpr float PI = 3.14159265358979323846f;

	std::vector<Vertex2D> m_vertices;       ///< 蓄積された頂点データ
	std::vector<std::uint32_t> m_indices;   ///< 蓄積されたインデックスデータ
	int m_drawCallCount = 0;                ///< 描画コール数
};

} // namespace mitiru::render
