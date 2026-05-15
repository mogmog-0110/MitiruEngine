#pragma once

/// @file SoftwareRenderer3D.hpp
/// @brief ソフトウェア3Dレンダラー（ヘッドレス対応）
/// @details Screen上にワイヤーフレーム3Dを描画する。GPU不要。
///          Camera3DとMat4fを使用して3D→2D変換を行い、
///          Screenの2D描画APIでワイヤーフレームを出力する。
///
/// @code
/// mitiru::render::SoftwareRenderer3D renderer(1280.0f, 720.0f);
/// renderer.setCamera({0, 5, -10}, {0, 0, 0}, {0, 1, 0});
/// renderer.setProjection(1.047f, 0.1f, 100.0f);
/// renderer.drawBox(screen, {0, 0, 0}, {2, 2, 2}, sgc::Colorf{1, 1, 1, 1});
/// @endcode

#include <algorithm>
#include <cmath>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief 深度ソート用3D三角形
/// @details ペインターズアルゴリズムで奥から手前に描画するためのデータ構造。
struct Triangle3D
{
	sgc::Vec3f a;        ///< 頂点A（ワールド座標）
	sgc::Vec3f b;        ///< 頂点B（ワールド座標）
	sgc::Vec3f c;        ///< 頂点C（ワールド座標）
	sgc::Colorf color;   ///< 塗りつぶし色

	/// @brief 平均Z値を計算する（深度ソート用）
	/// @return 3頂点のZ座標の平均値
	[[nodiscard]] float avgZ() const noexcept
	{
		return (a.z + b.z + c.z) / 3.0f;
	}
};

/// @brief ソフトウェア3Dレンダラー
/// @details Mat4f::lookAt / Mat4f::perspective を使用してビュー・射影行列を構築し、
///          transformPointで3D座標をNDCに変換後、スクリーン座標にマッピングする。
///          ワイヤーフレーム描画専用（ラスタライズなし）。
class SoftwareRenderer3D
{
public:
	/// @brief コンストラクタ
	/// @param screenW スクリーン幅（ピクセル）
	/// @param screenH スクリーン高さ（ピクセル）
	SoftwareRenderer3D(float screenW, float screenH) noexcept
		: m_screenW(screenW)
		, m_screenH(screenH)
		, m_vp(sgc::Mat4f::identity())
	{
	}

	/// @brief ビュー行列を設定する（カメラ位置/注視点）
	/// @param eye カメラ位置
	/// @param target 注視点
	/// @param up 上方向ベクトル
	void setCamera(const sgc::Vec3f& eye, const sgc::Vec3f& target, const sgc::Vec3f& up)
	{
		m_view = sgc::Mat4f::lookAt(eye, target, up);
		m_vp = m_proj * m_view;
	}

	/// @brief 透視投影を設定する
	/// @param fovRad 垂直視野角（ラジアン）
	/// @param nearZ ニアクリップ面
	/// @param farZ ファークリップ面
	void setProjection(float fovRad, float nearZ, float farZ)
	{
		const float aspect = m_screenW / m_screenH;
		m_proj = sgc::Mat4f::perspective(fovRad, aspect, nearZ, farZ);
		m_vp = m_proj * m_view;
	}

	/// @brief 3D頂点をスクリーン座標に変換する
	/// @param worldPos ワールド空間の3D座標
	/// @return スクリーン空間の2D座標
	[[nodiscard]] sgc::Vec2f project(const sgc::Vec3f& worldPos) const
	{
		/// VP行列でクリップ空間に変換する
		const sgc::Vec4f clip = m_vp * sgc::Vec4f{worldPos.x, worldPos.y, worldPos.z, 1.0f};

		/// 透視除算でNDCに変換する
		const float w = (std::abs(clip.w) < 0.001f) ? 0.001f : clip.w;
		const float ndcX = clip.x / w;
		const float ndcY = clip.y / w;

		/// NDCからスクリーン座標に変換する（Y軸反転）
		const float sx = (ndcX + 1.0f) * 0.5f * m_screenW;
		const float sy = (1.0f - ndcY) * 0.5f * m_screenH;

		return {sx, sy};
	}

	/// @brief ワイヤーフレーム三角形を描画する
	/// @param screen 描画先サーフェス
	/// @param a 頂点A（ワールド座標）
	/// @param b 頂点B（ワールド座標）
	/// @param c 頂点C（ワールド座標）
	/// @param color 描画色
	void drawTriangle3D(Screen& screen,
		const sgc::Vec3f& a, const sgc::Vec3f& b, const sgc::Vec3f& c,
		const sgc::Colorf& color) const
	{
		const auto sa = project(a);
		const auto sb = project(b);
		const auto sc = project(c);
		screen.drawLine(sa, sb, color, 1.0f);
		screen.drawLine(sb, sc, color, 1.0f);
		screen.drawLine(sc, sa, color, 1.0f);
	}

	/// @brief ワイヤーフレームボックスを描画する
	/// @param screen 描画先サーフェス
	/// @param center ボックスの中心座標
	/// @param size ボックスのサイズ（幅, 高さ, 奥行き）
	/// @param color 描画色
	void drawBox(Screen& screen,
		const sgc::Vec3f& center, const sgc::Vec3f& size,
		const sgc::Colorf& color) const
	{
		const float hx = size.x * 0.5f;
		const float hy = size.y * 0.5f;
		const float hz = size.z * 0.5f;

		/// 8頂点を計算する
		const sgc::Vec3f v[8] = {
			{center.x - hx, center.y - hy, center.z - hz},
			{center.x + hx, center.y - hy, center.z - hz},
			{center.x + hx, center.y + hy, center.z - hz},
			{center.x - hx, center.y + hy, center.z - hz},
			{center.x - hx, center.y - hy, center.z + hz},
			{center.x + hx, center.y - hy, center.z + hz},
			{center.x + hx, center.y + hy, center.z + hz},
			{center.x - hx, center.y + hy, center.z + hz}
		};

		/// 12辺を描画するラムダ
		auto line = [&](int i, int j)
		{
			const auto si = project(v[i]);
			const auto sj = project(v[j]);
			screen.drawLine(si, sj, color, 1.0f);
		};

		/// 前面4辺
		line(0, 1); line(1, 2); line(2, 3); line(3, 0);
		/// 背面4辺
		line(4, 5); line(5, 6); line(6, 7); line(7, 4);
		/// 接続4辺
		line(0, 4); line(1, 5); line(2, 6); line(3, 7);
	}

	/// @brief 3D座標軸を描画する
	/// @param screen 描画先サーフェス
	/// @param length 軸の長さ
	void drawAxes(Screen& screen, float length) const
	{
		const auto o = project({0.0f, 0.0f, 0.0f});
		const auto xEnd = project({length, 0.0f, 0.0f});
		const auto yEnd = project({0.0f, length, 0.0f});
		const auto zEnd = project({0.0f, 0.0f, length});

		/// X軸 = 赤
		screen.drawLine(o, xEnd, {1.0f, 0.0f, 0.0f, 1.0f}, 2.0f);
		/// Y軸 = 緑
		screen.drawLine(o, yEnd, {0.0f, 1.0f, 0.0f, 1.0f}, 2.0f);
		/// Z軸 = 青
		screen.drawLine(o, zEnd, {0.0f, 0.0f, 1.0f, 1.0f}, 2.0f);
	}

	/// @brief 塗りつぶし三角形を3Dからスクリーンに投影して描画する
	/// @param screen 描画先サーフェス
	/// @param a 頂点A（ワールド座標）
	/// @param b 頂点B（ワールド座標）
	/// @param c 頂点C（ワールド座標）
	/// @param color 塗りつぶし色
	void drawFilledTriangle3D(Screen& screen,
		const sgc::Vec3f& a, const sgc::Vec3f& b, const sgc::Vec3f& c,
		const sgc::Colorf& color) const
	{
		const auto sa = project(a);
		const auto sb = project(b);
		const auto sc = project(c);
		screen.drawTriangle(sa, sb, sc, color);
	}

	/// @brief 塗りつぶしボックスを描画する（6面×2三角形 = 12三角形）
	/// @param screen 描画先サーフェス
	/// @param center ボックスの中心座標
	/// @param size ボックスのサイズ（幅, 高さ, 奥行き）
	/// @param color 描画色
	void drawFilledBox(Screen& screen,
		const sgc::Vec3f& center, const sgc::Vec3f& size,
		const sgc::Colorf& color) const
	{
		const float hx = size.x * 0.5f;
		const float hy = size.y * 0.5f;
		const float hz = size.z * 0.5f;

		const sgc::Vec3f v[8] = {
			{center.x - hx, center.y - hy, center.z - hz},
			{center.x + hx, center.y - hy, center.z - hz},
			{center.x + hx, center.y + hy, center.z - hz},
			{center.x - hx, center.y + hy, center.z - hz},
			{center.x - hx, center.y - hy, center.z + hz},
			{center.x + hx, center.y - hy, center.z + hz},
			{center.x + hx, center.y + hy, center.z + hz},
			{center.x - hx, center.y + hy, center.z + hz}
		};

		/// 各面を少し異なる明度で描画して立体感を出す
		auto face = [&](int a, int b, int c, int d, float shade)
		{
			const sgc::Colorf sc{color.r * shade, color.g * shade, color.b * shade, color.a};
			drawFilledTriangle3D(screen, v[a], v[b], v[c], sc);
			drawFilledTriangle3D(screen, v[a], v[c], v[d], sc);
		};

		face(0, 1, 2, 3, 0.8f);  ///< 前面
		face(5, 4, 7, 6, 0.7f);  ///< 背面
		face(4, 0, 3, 7, 0.6f);  ///< 左面
		face(1, 5, 6, 2, 0.9f);  ///< 右面
		face(3, 2, 6, 7, 1.0f);  ///< 上面
		face(4, 5, 1, 0, 0.5f);  ///< 下面
	}

	/// @brief 塗りつぶしピラミッドを描画する
	/// @param screen 描画先サーフェス
	/// @param base 底面の中心座標
	/// @param baseSize 底面のサイズ
	/// @param height ピラミッドの高さ
	/// @param color 描画色
	void drawFilledPyramid(Screen& screen,
		const sgc::Vec3f& base, float baseSize, float height,
		const sgc::Colorf& color) const
	{
		const float h = baseSize * 0.5f;

		const sgc::Vec3f v[5] = {
			{base.x - h, base.y, base.z - h},
			{base.x + h, base.y, base.z - h},
			{base.x + h, base.y, base.z + h},
			{base.x - h, base.y, base.z + h},
			{base.x, base.y + height, base.z}
		};

		/// 底面
		const sgc::Colorf bottomColor{color.r * 0.5f, color.g * 0.5f, color.b * 0.5f, color.a};
		drawFilledTriangle3D(screen, v[0], v[1], v[2], bottomColor);
		drawFilledTriangle3D(screen, v[0], v[2], v[3], bottomColor);

		/// 4つの側面
		const float shades[] = {0.7f, 0.9f, 0.8f, 1.0f};
		for (int i = 0; i < 4; ++i)
		{
			const int next = (i + 1) % 4;
			const sgc::Colorf sideColor{
				color.r * shades[i], color.g * shades[i], color.b * shades[i], color.a};
			drawFilledTriangle3D(screen, v[i], v[next], v[4], sideColor);
		}
	}

	/// @brief ワイヤーフレームの球体（アイコスフィア近似）を描画する
	/// @param screen 描画先サーフェス
	/// @param center 球体の中心
	/// @param radius 球体の半径
	/// @param rings 緯線の数
	/// @param segments 経線の数
	/// @param color 描画色
	void drawSphere(Screen& screen,
		const sgc::Vec3f& center, float radius,
		int rings, int segments,
		const sgc::Colorf& color) const
	{
		/// 緯線を描画する
		for (int i = 1; i < rings; ++i)
		{
			const float phi = 3.14159265f * static_cast<float>(i) / static_cast<float>(rings);
			const float y = center.y + radius * std::cos(phi);
			const float r = radius * std::sin(phi);

			for (int j = 0; j < segments; ++j)
			{
				const float theta0 = 2.0f * 3.14159265f * static_cast<float>(j)
					/ static_cast<float>(segments);
				const float theta1 = 2.0f * 3.14159265f * static_cast<float>(j + 1)
					/ static_cast<float>(segments);

				const sgc::Vec3f p0{center.x + r * std::cos(theta0), y,
					center.z + r * std::sin(theta0)};
				const sgc::Vec3f p1{center.x + r * std::cos(theta1), y,
					center.z + r * std::sin(theta1)};

				const auto s0 = project(p0);
				const auto s1 = project(p1);
				screen.drawLine(s0, s1, color, 1.0f);
			}
		}

		/// 経線を描画する
		for (int j = 0; j < segments; ++j)
		{
			const float theta = 2.0f * 3.14159265f * static_cast<float>(j)
				/ static_cast<float>(segments);

			for (int i = 0; i < rings; ++i)
			{
				const float phi0 = 3.14159265f * static_cast<float>(i)
					/ static_cast<float>(rings);
				const float phi1 = 3.14159265f * static_cast<float>(i + 1)
					/ static_cast<float>(rings);

				const sgc::Vec3f p0{
					center.x + radius * std::sin(phi0) * std::cos(theta),
					center.y + radius * std::cos(phi0),
					center.z + radius * std::sin(phi0) * std::sin(theta)};
				const sgc::Vec3f p1{
					center.x + radius * std::sin(phi1) * std::cos(theta),
					center.y + radius * std::cos(phi1),
					center.z + radius * std::sin(phi1) * std::sin(theta)};

				const auto s0 = project(p0);
				const auto s1 = project(p1);
				screen.drawLine(s0, s1, color, 1.0f);
			}
		}
	}

	/// @brief 塗りつぶし床面を描画する
	/// @param screen 描画先サーフェス
	/// @param center 床の中心座標
	/// @param extent 床の半径サイズ
	/// @param color 描画色
	void drawFloorPlane(Screen& screen,
		const sgc::Vec3f& center, float extent,
		const sgc::Colorf& color) const
	{
		const sgc::Vec3f v0{center.x - extent, center.y, center.z - extent};
		const sgc::Vec3f v1{center.x + extent, center.y, center.z - extent};
		const sgc::Vec3f v2{center.x + extent, center.y, center.z + extent};
		const sgc::Vec3f v3{center.x - extent, center.y, center.z + extent};

		drawFilledTriangle3D(screen, v0, v1, v2, color);
		drawFilledTriangle3D(screen, v0, v2, v3, color);
	}

	/// @brief ワイヤーフレームのグリッド（床）を描画する
	/// @param screen 描画先サーフェス
	/// @param extent グリッドの半径
	/// @param divisions 分割数
	/// @param color 描画色
	void drawGrid(Screen& screen, float extent, int divisions,
		const sgc::Colorf& color) const
	{
		const float step = (extent * 2.0f) / static_cast<float>(divisions);
		for (int i = 0; i <= divisions; ++i)
		{
			const float t = -extent + step * static_cast<float>(i);
			/// X方向の線
			const auto a = project({t, 0.0f, -extent});
			const auto b = project({t, 0.0f, extent});
			screen.drawLine(a, b, color, 1.0f);
			/// Z方向の線
			const auto c = project({-extent, 0.0f, t});
			const auto d = project({extent, 0.0f, t});
			screen.drawLine(c, d, color, 1.0f);
		}
	}

	/// @brief 深度ソート付きシーン描画（ペインターズアルゴリズム）
	/// @details 三角形を平均Z値で奥から手前にソートし、
	///          正しい前後関係で塗りつぶし描画する。
	///          Z-bufferの代替として簡易的な深度処理を実現する。
	/// @param screen 描画先サーフェス
	/// @param tris 描画する三角形リスト（ソートのため非const参照）
	///
	/// @code
	/// std::vector<mitiru::render::Triangle3D> tris;
	/// tris.push_back({{0,0,5}, {1,0,5}, {0,1,5}, {1,0,0,1}});
	/// tris.push_back({{0,0,2}, {1,0,2}, {0,1,2}, {0,1,0,1}});
	/// renderer.drawScene(screen, tris);  // 奥の赤→手前の緑
	/// @endcode
	void drawScene(Screen& screen, std::vector<Triangle3D>& tris) const
	{
		/// 平均Zで奥（小さい値）から手前（大きい値）にソートする
		std::sort(tris.begin(), tris.end(),
			[](const Triangle3D& lhs, const Triangle3D& rhs)
			{
				return lhs.avgZ() < rhs.avgZ();
			});

		/// ソート順に描画する（奥から手前へ）
		for (const auto& t : tris)
		{
			drawFilledTriangle3D(screen, t.a, t.b, t.c, t.color);
		}
	}

	/// @brief テクスチャ付き三角形を描画する（UV座標指定）
	/// @details 3D頂点をスクリーンに投影し、テクスチャの中心ピクセル色で
	///          塗りつぶし三角形を描画する。簡易テクスチャマッピングの概念実装。
	/// @param screen 描画先サーフェス
	/// @param a 頂点A（ワールド座標）
	/// @param b 頂点B（ワールド座標）
	/// @param c 頂点C（ワールド座標）
	/// @param tex テクスチャ
	/// @param u0 頂点AのU座標
	/// @param v0 頂点AのV座標
	/// @param u1 頂点BのU座標
	/// @param v1 頂点BのV座標
	/// @param u2 頂点CのU座標
	/// @param v2 頂点CのV座標
	void drawTexturedTriangle3D(Screen& screen,
		const sgc::Vec3f& a, const sgc::Vec3f& b, const sgc::Vec3f& c,
		const Texture& tex,
		float u0, float v0, float u1, float v1, float u2, float v2) const
	{
		if (!tex.valid()) return;

		/// UV座標の平均値でテクスチャからサンプリングする
		const float avgU = (u0 + u1 + u2) / 3.0f;
		const float avgV = (v0 + v1 + v2) / 3.0f;
		const int tx = std::clamp(static_cast<int>(avgU * static_cast<float>(tex.width())),
			0, tex.width() - 1);
		const int ty = std::clamp(static_cast<int>(avgV * static_cast<float>(tex.height())),
			0, tex.height() - 1);

		const std::uint32_t px = tex.pixelAt(tx, ty);
		const sgc::Colorf color{
			static_cast<float>((px >> 24) & 0xFF) / 255.0f,
			static_cast<float>((px >> 16) & 0xFF) / 255.0f,
			static_cast<float>((px >> 8) & 0xFF) / 255.0f,
			1.0f};

		const auto sa = project(a);
		const auto sb = project(b);
		const auto sc = project(c);
		screen.drawTriangle(sa, sb, sc, color);
	}

	/// @brief テクスチャ付きクアッドを描画する（2三角形で分割）
	/// @details 4頂点で構成されるクアッドをテクスチャ付きで描画する。
	///          内部で2つのテクスチャ付き三角形に分割して描画する。
	/// @param screen 描画先サーフェス
	/// @param v0 頂点0（ワールド座標）
	/// @param v1 頂点1（ワールド座標）
	/// @param v2 頂点2（ワールド座標）
	/// @param v3 頂点3（ワールド座標）
	/// @param tex テクスチャ
	void drawTexturedQuad3D(Screen& screen,
		const sgc::Vec3f& v0, const sgc::Vec3f& v1,
		const sgc::Vec3f& v2, const sgc::Vec3f& v3,
		const Texture& tex) const
	{
		drawTexturedTriangle3D(screen, v0, v1, v2, tex,
			0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
		drawTexturedTriangle3D(screen, v0, v2, v3, tex,
			0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
	}

	/// @brief テクスチャ付きボックスを描画する（6面）
	/// @param screen 描画先サーフェス
	/// @param center ボックスの中心座標
	/// @param size ボックスのサイズ
	/// @param tex テクスチャ
	void drawTexturedBox(Screen& screen,
		const sgc::Vec3f& center, const sgc::Vec3f& size,
		const Texture& tex) const
	{
		const float hx = size.x * 0.5f;
		const float hy = size.y * 0.5f;
		const float hz = size.z * 0.5f;

		const sgc::Vec3f v[8] = {
			{center.x - hx, center.y - hy, center.z - hz},
			{center.x + hx, center.y - hy, center.z - hz},
			{center.x + hx, center.y + hy, center.z - hz},
			{center.x - hx, center.y + hy, center.z - hz},
			{center.x - hx, center.y - hy, center.z + hz},
			{center.x + hx, center.y - hy, center.z + hz},
			{center.x + hx, center.y + hy, center.z + hz},
			{center.x - hx, center.y + hy, center.z + hz}
		};

		/// 各面をテクスチャ付きクアッドで描画する
		drawTexturedQuad3D(screen, v[0], v[1], v[2], v[3], tex);  ///< 前面
		drawTexturedQuad3D(screen, v[5], v[4], v[7], v[6], tex);  ///< 背面
		drawTexturedQuad3D(screen, v[4], v[0], v[3], v[7], tex);  ///< 左面
		drawTexturedQuad3D(screen, v[1], v[5], v[6], v[2], tex);  ///< 右面
		drawTexturedQuad3D(screen, v[3], v[2], v[6], v[7], tex);  ///< 上面
		drawTexturedQuad3D(screen, v[4], v[5], v[1], v[0], tex);  ///< 下面
	}

private:
	float m_screenW;             ///< スクリーン幅
	float m_screenH;             ///< スクリーン高さ
	sgc::Mat4f m_view;           ///< ビュー行列
	sgc::Mat4f m_proj;           ///< 射影行列
	sgc::Mat4f m_vp;             ///< ビュー×射影合成行列
};

} // namespace mitiru::render
