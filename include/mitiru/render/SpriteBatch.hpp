#pragma once

/// @file SpriteBatch.hpp
/// @brief バッチ型2Dスプライトレンダラー
/// @details 複数のスプライト・矩形描画を頂点バッファに蓄積し、
///          一括でGPUに送信することでドローコール数を削減する。

#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex2D.hpp>

namespace mitiru::render
{

/// @brief バッチ型2Dスプライトレンダラー
/// @details begin() で蓄積開始、描画コマンドで頂点を蓄積し、
///          end() でバッチをフラッシュする。
///
/// @code
/// mitiru::render::SpriteBatch batch;
/// batch.begin();
/// batch.drawRect(sgc::Rectf{10, 10, 100, 50}, sgc::Colorf::red());
/// batch.drawSprite(texture, destRect, srcRect, sgc::Colorf::white(), 0.0f);
/// batch.end();
/// @endcode
class SpriteBatch
{
public:
	/// @brief バッチ蓄積を開始する
	/// @note 前回のバッチデータはクリアされる
	void begin() noexcept
	{
		m_vertices.clear();
		m_indices.clear();
		m_drawCallCount = 0;
		m_recording = true;
	}

	/// @brief スプライトを描画する
	/// @param textureId テクスチャ識別子（Phase 1 では未使用）
	/// @param destRect 描画先矩形（スクリーン座標）
	/// @param srcRect ソース矩形（テクスチャ座標 [0,1]）
	/// @param color 乗算色
	/// @param rotation 回転角度（ラジアン）
	void drawSprite(std::uint32_t textureId,
	                const sgc::Rectf& destRect,
	                const sgc::Rectf& srcRect,
	                const sgc::Colorf& color,
	                float rotation = 0.0f)
	{
		static_cast<void>(textureId);

		if (!m_recording)
		{
			return;
		}

		/// 回転なしの場合は単純な矩形
		if (rotation == 0.0f)
		{
			pushQuad(destRect, srcRect, color);
		}
		else
		{
			/// 中心を基準に回転した4頂点を計算する
			const auto cx = destRect.x() + destRect.width() * 0.5f;
			const auto cy = destRect.y() + destRect.height() * 0.5f;
			const auto hw = destRect.width() * 0.5f;
			const auto hh = destRect.height() * 0.5f;
			const auto cosR = std::cos(rotation);
			const auto sinR = std::sin(rotation);

			/// 左上・右上・右下・左下の回転後位置
			const sgc::Vec2f corners[4] =
			{
				{cx + (-hw * cosR - (-hh) * sinR), cy + (-hw * sinR + (-hh) * cosR)},
				{cx + ( hw * cosR - (-hh) * sinR), cy + ( hw * sinR + (-hh) * cosR)},
				{cx + ( hw * cosR -   hh  * sinR), cy + ( hw * sinR +   hh  * cosR)},
				{cx + (-hw * cosR -   hh  * sinR), cy + (-hw * sinR +   hh  * cosR)},
			};

			const sgc::Vec2f uvs[4] =
			{
				{srcRect.x(), srcRect.y()},
				{srcRect.x() + srcRect.width(), srcRect.y()},
				{srcRect.x() + srcRect.width(), srcRect.y() + srcRect.height()},
				{srcRect.x(), srcRect.y() + srcRect.height()},
			};

			pushQuadRaw(corners, uvs, color);
		}

		++m_drawCallCount;
	}

	/// @brief 塗りつぶし矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	void drawRect(const sgc::Rectf& rect, const sgc::Colorf& color)
	{
		if (!m_recording)
		{
			return;
		}

		const sgc::Rectf uvRect{0.0f, 0.0f, 1.0f, 1.0f};
		pushQuad(rect, uvRect, color);
		++m_drawCallCount;
	}

	/// @brief 矩形枠を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	/// @param thickness 枠線の太さ
	void drawRectFrame(const sgc::Rectf& rect,
	                   const sgc::Colorf& color,
	                   float thickness = 1.0f)
	{
		if (!m_recording)
		{
			return;
		}

		const auto x = rect.x();
		const auto y = rect.y();
		const auto w = rect.width();
		const auto h = rect.height();
		const sgc::Rectf uvRect{0.0f, 0.0f, 1.0f, 1.0f};

		/// 上辺
		pushQuad(sgc::Rectf{x, y, w, thickness}, uvRect, color);
		/// 下辺
		pushQuad(sgc::Rectf{x, y + h - thickness, w, thickness}, uvRect, color);
		/// 左辺
		pushQuad(sgc::Rectf{x, y + thickness, thickness, h - thickness * 2.0f}, uvRect, color);
		/// 右辺
		pushQuad(sgc::Rectf{x + w - thickness, y + thickness, thickness, h - thickness * 2.0f}, uvRect, color);

		++m_drawCallCount;
	}

	/// @brief バッチをフラッシュし蓄積を終了する
	/// @details GPU描画はRenderPipeline2DがsubmitBatch()で行う。
	///          end()は蓄積を停止するのみで、データは保持される。
	void end() noexcept
	{
		m_recording = false;
	}

	/// @brief 蓄積された頂点数を取得する
	/// @return 頂点数
	[[nodiscard]] std::size_t vertexCount() const noexcept
	{
		return m_vertices.size();
	}

	/// @brief 蓄積されたインデックス数を取得する
	/// @return インデックス数
	[[nodiscard]] std::size_t indexCount() const noexcept
	{
		return m_indices.size();
	}

	/// @brief フレーム内の描画コール数を取得する
	/// @return 描画コール数
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 蓄積された頂点データを取得する
	/// @return 頂点配列への定数参照
	[[nodiscard]] const std::vector<Vertex2D>& vertices() const noexcept
	{
		return m_vertices;
	}

	/// @brief 蓄積されたインデックスデータを取得する
	/// @return インデックス配列への定数参照
	[[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept
	{
		return m_indices;
	}

private:
	/// @brief 矩形をクワッド（4頂点+6インデックス）として蓄積する
	/// @param rect 描画先矩形
	/// @param uvRect テクスチャ座標矩形
	/// @param color 頂点色
	void pushQuad(const sgc::Rectf& rect,
	              const sgc::Rectf& uvRect,
	              const sgc::Colorf& color)
	{
		const auto baseIndex = static_cast<std::uint32_t>(m_vertices.size());

		const auto x0 = rect.x();
		const auto y0 = rect.y();
		const auto x1 = rect.x() + rect.width();
		const auto y1 = rect.y() + rect.height();

		const auto u0 = uvRect.x();
		const auto v0 = uvRect.y();
		const auto u1 = uvRect.x() + uvRect.width();
		const auto v1 = uvRect.y() + uvRect.height();

		m_vertices.emplace_back(sgc::Vec2f{x0, y0}, sgc::Vec2f{u0, v0}, color);
		m_vertices.emplace_back(sgc::Vec2f{x1, y0}, sgc::Vec2f{u1, v0}, color);
		m_vertices.emplace_back(sgc::Vec2f{x1, y1}, sgc::Vec2f{u1, v1}, color);
		m_vertices.emplace_back(sgc::Vec2f{x0, y1}, sgc::Vec2f{u0, v1}, color);

		m_indices.push_back(baseIndex);
		m_indices.push_back(baseIndex + 1);
		m_indices.push_back(baseIndex + 2);
		m_indices.push_back(baseIndex);
		m_indices.push_back(baseIndex + 2);
		m_indices.push_back(baseIndex + 3);
	}

	/// @brief 任意の4頂点をクワッドとして蓄積する（回転済み）
	/// @param corners 4頂点の位置配列
	/// @param uvs 4頂点のテクスチャ座標配列
	/// @param color 頂点色
	void pushQuadRaw(const sgc::Vec2f corners[4],
	                 const sgc::Vec2f uvs[4],
	                 const sgc::Colorf& color)
	{
		const auto baseIndex = static_cast<std::uint32_t>(m_vertices.size());

		for (int i = 0; i < 4; ++i)
		{
			m_vertices.emplace_back(corners[i], uvs[i], color);
		}

		m_indices.push_back(baseIndex);
		m_indices.push_back(baseIndex + 1);
		m_indices.push_back(baseIndex + 2);
		m_indices.push_back(baseIndex);
		m_indices.push_back(baseIndex + 2);
		m_indices.push_back(baseIndex + 3);
	}

	std::vector<Vertex2D> m_vertices;       ///< 蓄積された頂点データ
	std::vector<std::uint32_t> m_indices;   ///< 蓄積されたインデックスデータ
	int m_drawCallCount = 0;                ///< 描画コール数
	bool m_recording = false;               ///< 蓄積中フラグ
};

} // namespace mitiru::render
