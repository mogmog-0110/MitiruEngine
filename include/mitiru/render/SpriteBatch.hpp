#pragma once

/// @file SpriteBatch.hpp
/// @brief バッチ型 2D スプライトレンダラー
/// @details 複数のスプライト・矩形描画を頂点バッファに蓄積し、
///          一括で GPU に送信することでドローコール数を削減する。

#include <cmath>
#include <cstdint>
#include <vector>

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Quad2D.hpp>
#include <mitiru/render/Vertex2D.hpp>

#if defined(_MSC_VER)
#define MITIRU_SB_INLINE __forceinline
#else
#define MITIRU_SB_INLINE inline __attribute__((always_inline))
#endif

namespace mitiru::render
{

/// @brief バッチ型 2D スプライトレンダラー
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
	/// @brief 期待スプライト数を事前確保する (C4)。呼ばなければ既定の `kDefaultReserveSprites`
	/// 分だけ最初の begin() で確保される。エンティティ数が既知の呼び出し側が begin() より
	/// 前に 1 回呼べば、フレーム前半の再確保を避けられる。
	void reserveSprites(std::size_t expectedSprites) noexcept
	{
		m_vertices.reserve(expectedSprites * 4);
		m_indices.reserve(expectedSprites * 6);
		if (m_quadMode) { m_quads.reserve(expectedSprites); }
	}

	/// @brief 軸に平行な四角形を 1 枚 48 バイトのレコードで積む形にする (GPU が 4 隅を作る描画先専用)
	/// @details 既定は従来の頂点 + 添字。レコードにした分は vertices()/indices() に現れないので、
	///          読むときは materialize() で従来の形へ展開する。円・線・グラデーションなどは従来どおり頂点で積み、
	///          レコードと描いた順を保つ。切り替えは空のとき (begin() の直後) にする
	void setQuadMode(bool enabled)
	{
		m_quadMode = enabled;
		if (enabled && m_quads.capacity() == 0) { m_quads.reserve(kDefaultReserveSprites); }
	}
	[[nodiscard]] bool quadMode() const noexcept { return m_quadMode; }
	/// 積んだ物が何も無いか (レコードも頂点も)
	[[nodiscard]] bool empty() const noexcept { return m_vertices.empty() && m_quads.empty(); }
	[[nodiscard]] bool hasQuads() const noexcept { return !m_quads.empty(); }
	[[nodiscard]] const QuadStore& quads() const noexcept { return m_quads; }
	/// 区間 (レコードの列と三角形の列が描いた順に交互に並ぶ)。レコードを積んでいなければ空
	[[nodiscard]] const std::vector<BatchSegment>& segments() const noexcept { return m_segments; }

	/// @brief レコードを従来の 4 頂点 + 6 添字へ展開し、三角形の列と描いた順のまま 1 本の頂点・添字の列にする
	/// @details 3D の上に重ねる HUD の蓄積や CPU 描画のように、レコードを読めない所へ渡す前に呼ぶ。呼んだ後は quads() が空になる
	void materialize()
	{
		if (m_quads.empty()) { m_segments.clear(); m_inQuadSegment = false; return; }
		std::vector<Vertex2D> outV;
		std::vector<std::uint32_t> outI;
		outV.reserve(m_vertices.size() + m_quads.size() * 4);
		outI.reserve(m_indices.size() + m_quads.size() * 6);
		const std::size_t segCount = m_segments.size();
		for (std::size_t s = 0; s < segCount; ++s)
		{
			const BatchSegment& seg = m_segments[s];
			const bool last = s + 1 == segCount;
			const std::size_t quadEnd = last ? m_quads.size() : m_segments[s + 1].quadFirst;
			const std::size_t indexEnd = last ? m_indices.size() : m_segments[s + 1].indexFirst;
			const std::size_t vertexEnd = last ? m_vertices.size() : m_segments[s + 1].vertexFirst;
			if (seg.kind == BatchSegment::Kind::Quads)
			{
				for (std::size_t q = seg.quadFirst; q < quadEnd; ++q)
				{
					const Quad2D& r = m_quads[q];
					const auto base = static_cast<std::uint32_t>(outV.size());
					outV.emplace_back(sgc::Vec2f{r.x0, r.y0}, sgc::Vec2f{r.u0, r.v0}, r.color);
					outV.emplace_back(sgc::Vec2f{r.x1, r.y0}, sgc::Vec2f{r.u1, r.v0}, r.color);
					outV.emplace_back(sgc::Vec2f{r.x1, r.y1}, sgc::Vec2f{r.u1, r.v1}, r.color);
					outV.emplace_back(sgc::Vec2f{r.x0, r.y1}, sgc::Vec2f{r.u0, r.v1}, r.color);
					const std::uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
					outI.insert(outI.end(), idx, idx + 6);
				}
			}
			else
			{
				const auto base = static_cast<std::uint32_t>(outV.size());
				outV.insert(outV.end(), m_vertices.begin() + static_cast<std::ptrdiff_t>(seg.vertexFirst),
				            m_vertices.begin() + static_cast<std::ptrdiff_t>(vertexEnd));
				for (std::size_t i = seg.indexFirst; i < indexEnd; ++i)
				{
					outI.push_back(m_indices[i] - static_cast<std::uint32_t>(seg.vertexFirst) + base);
				}
			}
		}
		m_vertices.swap(outV);
		m_indices.swap(outI);
		m_quads.clear();
		m_segments.clear();
		m_inQuadSegment = false;
	}

	/// @brief バッチ蓄積を開始する
	/// @note 前回のバッチデータはクリアされる
	void begin() noexcept
	{
		if (m_vertices.capacity() == 0) { reserveSprites(kDefaultReserveSprites); }
		m_vertices.clear();
		m_indices.clear();
		m_quads.clear();
		m_segments.clear();
		m_inQuadSegment = false;
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
			/// 中心を基準に回転した 4 頂点を計算する
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

	/// @brief 矩形+4 頂点個別カラー（グラデーション矩形用）
	/// @param rect 矩形
	/// @param tl 左上色
	/// @param tr 右上色
	/// @param br 右下色
	/// @param bl 左下色
	void drawRectGradient4(const sgc::Rectf& rect,
	                       const sgc::Colorf& tl, const sgc::Colorf& tr,
	                       const sgc::Colorf& br, const sgc::Colorf& bl)
	{
		if (!m_recording) return;
		const sgc::Vec2f corners[4] = {
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()},
			{rect.x(), rect.y() + rect.height()},
		};
		const sgc::Colorf colors[4] = {tl, tr, br, bl};
		pushQuadColored(corners, colors);
		++m_drawCallCount;
	}

	/// @brief 4 頂点個別カラーのクアッド描画（変換済み座標）
	void drawQuadGradient4(const sgc::Vec2f corners[4],
	                       const sgc::Colorf& tl, const sgc::Colorf& tr,
	                       const sgc::Colorf& br, const sgc::Colorf& bl)
	{
		if (!m_recording) return;
		const sgc::Colorf colors[4] = {tl, tr, br, bl};
		pushQuadColored(corners, colors);
		++m_drawCallCount;
	}

	/// @brief 変換済み 4 頂点 + UV のテクスチャ付きクワッドを蓄積する
	/// @details corners は変換適用済みのスクリーン座標、uvs は [0,1] テクスチャ座標。
	///          実テクスチャのバインドは RenderPipeline2D::submitTexturedBatch が行う
	///          （このバッチは単一テクスチャ run として submit される前提）。
	/// @param corners 4 頂点の位置（TL, TR, BR, BL 順）
	/// @param uvs 4 頂点のテクスチャ座標（corners と同順）
	/// @param color 乗算色（ティント）
	void drawSpriteQuad(const sgc::Vec2f corners[4],
	                    const sgc::Vec2f uvs[4],
	                    const sgc::Colorf& color)
	{
		if (!m_recording) return;
		pushQuadRaw(corners, uvs, color);
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

	/// 色だけで塗る図形 (円・線・三角形) の頂点を足すための、頂点・インデックス配列への口。
	/// 図形を矩形と同じ配列に積むので、描いた順がそのまま保たれる。呼ぶたびに描画 1 回と数える
	struct Geometry
	{
		std::vector<Vertex2D>& vertices;
		std::vector<std::uint32_t>& indices;
	};

	[[nodiscard]] Geometry beginShape()
	{
		++m_drawCallCount;
		if (m_quadMode) { openSegment(BatchSegment::Kind::Triangles); }
		return Geometry{m_vertices, m_indices};
	}

	/// @brief バッチをフラッシュし蓄積を終了する
	/// @details GPU 描画は RenderPipeline2D が submitBatch()で行う。
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
	/// @brief 矩形をクワッド（4 頂点+6 インデックス）として蓄積する
	/// @param rect 描画先矩形
	/// @param uvRect テクスチャ座標矩形
	/// @param color 頂点色
	MITIRU_SB_INLINE void pushQuad(const sgc::Rectf& rect,
	              const sgc::Rectf& uvRect,
	              const sgc::Colorf& color)
	{
		if (m_quadMode)
		{
			// 隅の値は下の頂点と同じ式で出す (GPU が作る頂点を今までの頂点と一致させるため)
			pushRecord(rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height(),
			           uvRect.x(), uvRect.y(), uvRect.x() + uvRect.width(), uvRect.y() + uvRect.height(), color);
			return;
		}
		const auto x0 = rect.x();
		const auto y0 = rect.y();
		const auto x1 = rect.x() + rect.width();
		const auto y1 = rect.y() + rect.height();

		const auto u0 = uvRect.x();
		const auto v0 = uvRect.y();
		const auto u1 = uvRect.x() + uvRect.width();
		const auto v1 = uvRect.y() + uvRect.height();

		const Vertex2D quad[4] =
		{
			{sgc::Vec2f{x0, y0}, sgc::Vec2f{u0, v0}, color},
			{sgc::Vec2f{x1, y0}, sgc::Vec2f{u1, v0}, color},
			{sgc::Vec2f{x1, y1}, sgc::Vec2f{u1, v1}, color},
			{sgc::Vec2f{x0, y1}, sgc::Vec2f{u0, v1}, color},
		};
		appendQuad(quad);
	}

	/// @brief 任意の 4 頂点をクワッドとして蓄積する（回転済み）
	/// @param corners 4 頂点の位置配列
	/// @param uvs 4 頂点のテクスチャ座標配列
	/// @param color 頂点色
	void pushQuadRaw(const sgc::Vec2f corners[4],
	                 const sgc::Vec2f uvs[4],
	                 const sgc::Colorf& color)
	{
		// 軸に平行な矩形 (4 隅が左上・右上・右下・左下の順で、辺が軸に沿う) だけレコードにできる。回転した物は頂点で積む
		if (m_quadMode && corners[1].y == corners[0].y && corners[1].x == corners[2].x && corners[3].y == corners[2].y
		    && corners[3].x == corners[0].x && uvs[1].y == uvs[0].y && uvs[1].x == uvs[2].x && uvs[3].y == uvs[2].y
		    && uvs[3].x == uvs[0].x)
		{
			pushRecord(corners[0].x, corners[0].y, corners[2].x, corners[2].y, uvs[0].x, uvs[0].y, uvs[2].x, uvs[2].y, color);
			return;
		}
		const Vertex2D quad[4] =
		{
			{corners[0], uvs[0], color},
			{corners[1], uvs[1], color},
			{corners[2], uvs[2], color},
			{corners[3], uvs[3], color},
		};
		appendQuad(quad);
	}

	/// @brief 4 頂点個別カラーのクワッドを蓄積する（グラデーション用）
	void pushQuadColored(const sgc::Vec2f corners[4],
	                     const sgc::Colorf colors[4])
	{
		const sgc::Vec2f uv{0.0f, 0.0f};
		const Vertex2D quad[4] =
		{
			{corners[0], uv, colors[0]},
			{corners[1], uv, colors[1]},
			{corners[2], uv, colors[2]},
			{corners[3], uv, colors[3]},
		};
		appendQuad(quad);
	}

	/// 4 頂点と 6 添字を、容量の確認 1 回ずつで足す。1 頂点ずつ emplace_back すると 1 枚で 10 回の確認が並び、
	/// 画像を何万枚も積む場面でその確認が積み込みの大半になる
	void appendQuad(const Vertex2D (&quad)[4])
	{
		if (m_quadMode) { openSegment(BatchSegment::Kind::Triangles); }
		const auto base = static_cast<std::uint32_t>(m_vertices.size());
		m_vertices.insert(m_vertices.end(), quad, quad + 4);
		const std::uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
		m_indices.insert(m_indices.end(), idx, idx + 6);
	}

	/// 軸に平行な四角形を 1 レコードとして積む。直前が三角形の列なら区間を切る
	MITIRU_SB_INLINE void pushRecord(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const sgc::Colorf& color)
	{
		if (!m_inQuadSegment) { openSegment(BatchSegment::Kind::Quads); }
		Quad2D* q = m_quads.push();
#if defined(_M_X64) || defined(__SSE2__)
		// 48 バイトを 16 バイトの書き込み 3 回で済ませる (Colorf は float 4 つが並ぶだけの形)
		static_assert(sizeof(sgc::Colorf) == 16 && sizeof(Quad2D) == 48, "Quad2D の並びに依存する");
		float* out = reinterpret_cast<float*>(q);
		_mm_storeu_ps(out, _mm_set_ps(y1, x1, y0, x0));
		_mm_storeu_ps(out + 4, _mm_set_ps(v1, u1, v0, u0));
		_mm_storeu_ps(out + 8, _mm_set_ps(color.a, color.b, color.g, color.r));
#else
		q->x0 = x0; q->y0 = y0; q->x1 = x1; q->y1 = y1;
		q->u0 = u0; q->v0 = v0; q->u1 = u1; q->v1 = v1;
		q->color = color;
#endif
	}

	/// 末尾の区間が kind でなければ、今の位置から新しい区間を始める
	void openSegment(BatchSegment::Kind kind)
	{
		if (!m_segments.empty() && m_segments.back().kind == kind) { m_inQuadSegment = kind == BatchSegment::Kind::Quads; return; }
		m_inQuadSegment = kind == BatchSegment::Kind::Quads;
		m_segments.push_back(BatchSegment{kind, static_cast<std::uint32_t>(m_quads.size()),
		                                  static_cast<std::uint32_t>(m_indices.size()),
		                                  static_cast<std::uint32_t>(m_vertices.size())});
	}

	static constexpr std::size_t kDefaultReserveSprites = 256;  ///< reserveSprites() 未呼び出し時の既定値 (C4)
	std::vector<Vertex2D> m_vertices;       ///< 蓄積された頂点データ
	std::vector<std::uint32_t> m_indices;   ///< 蓄積されたインデックスデータ
	int m_drawCallCount = 0;                ///< 描画コール数
	bool m_recording = false;               ///< 蓄積中フラグ
	bool m_quadMode = false;                ///< true なら軸に平行な四角形をレコードで積む (v61)
	bool m_inQuadSegment = false;           ///< 末尾の区間がレコードの列か (区間の配列を引かずに判定する)
	QuadStore m_quads;                      ///< 四角形のレコード (v61)
	std::vector<BatchSegment> m_segments;   ///< レコードと三角形の列の区間 (v61)
};

} // namespace mitiru::render
