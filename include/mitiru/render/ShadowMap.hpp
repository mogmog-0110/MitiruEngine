#pragma once

/// @file ShadowMap.hpp
/// @brief ディレクショナルライト用シャドウマップ
/// @details CPUソフトウェア実装による深度バッファを使ったシャドウマップ。
///          ライト空間のビュー射影行列を計算し、メッシュの深度を記録して
///          ワールド空間の点が影の中にあるかどうかを判定する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <mitiru/render/Light.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/debug/Log.hpp>

namespace mitiru::render
{

/// @brief シャドウマップ設定
/// @details 解像度・正射影サイズ・クリップ面を定義する。
struct ShadowMapConfig
{
	int resolution = 2048;   ///< 深度バッファの解像度（正方形）
	float orthoSize = 50.0f; ///< 正射影の半幅（ライト空間単位）
	float nearPlane = 0.1f;  ///< ニアクリップ面
	float farPlane = 100.0f; ///< ファークリップ面
};

/// @brief ディレクショナルライト用シャドウマップ（ソフトウェア実装）
/// @details beginShadowPass() → recordMesh() → endShadowPass() の順に呼び出す。
///          isInShadow() でワールド空間の点が影の中にあるかを判定する。
///
/// @code
/// mitiru::render::ShadowMap shadow;
/// shadow.initialize({1024, 40.0f, 0.1f, 100.0f});
///
/// auto sun = mitiru::render::Light::directional({0, -1, 0.5f});
/// shadow.beginShadowPass(sun);
/// shadow.recordMesh(mesh, worldMatrix);
/// shadow.endShadowPass();
///
/// bool inShadow = shadow.isInShadow({0, 0, 0});
/// @endcode
class ShadowMap
{
public:
	/// @brief デフォルトコンストラクタ
	ShadowMap() noexcept = default;

	/// @brief シャドウマップを初期化する
	/// @param cfg 設定
	/// @throws std::invalid_argument 解像度が1未満の場合
	void initialize(const ShadowMapConfig& cfg)
	{
		if (cfg.resolution < 1)
		{
			throw std::invalid_argument("ShadowMap: resolution must be >= 1");
		}

		m_config = cfg;

		const std::size_t size =
			static_cast<std::size_t>(cfg.resolution) * cfg.resolution;

		m_depthBuffer.assign(size, 1.0f);
		m_initialized = true;
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 設定を取得する
	[[nodiscard]] const ShadowMapConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief シャドウパスを開始する
	/// @param light ディレクショナルライト（方向を使用）
	/// @details ライト空間のビュー射影行列を計算し、深度バッファをリセットする。
	void beginShadowPass(const Light& light)
	{
		MITIRU_LOG_TRACE("ShadowMap",
			"beginShadowPass, res=" + std::to_string(m_config.resolution));
		computeLightViewProjection(light);
		clearDepthBuffer();
	}

	/// @brief メッシュをシャドウ深度バッファに記録する
	/// @param mesh メッシュデータ
	/// @param worldTransform ワールド変換行列
	/// @details インデックスがある場合はインデックス描画、ない場合は頂点を順番に処理する。
	void recordMesh(const Mesh& mesh, const sgc::Mat4f& worldTransform)
	{
		if (!m_initialized || mesh.vertexCount() == 0)
		{
			return;
		}

		const auto& verts = mesh.vertices();
		const auto& indices = mesh.indices();

		if (!indices.empty())
		{
			/// インデックスを3つずつ取り出して三角形を処理する
			for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				rasterizeTriangle(
					verts[indices[i]].position,
					verts[indices[i + 1]].position,
					verts[indices[i + 2]].position,
					worldTransform);
			}
		}
		else
		{
			/// 頂点を3つずつ取り出して三角形を処理する
			for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
			{
				rasterizeTriangle(
					verts[i].position,
					verts[i + 1].position,
					verts[i + 2].position,
					worldTransform);
			}
		}
	}

	/// @brief シャドウパスを終了する
	/// @details 現在は何もしない（GPUパス移植時のフック用）。
	void endShadowPass() noexcept
	{
		/// 将来のGPUパス移植のために予約
	}

	/// @brief ライト空間ビュー射影行列を取得する
	[[nodiscard]] const sgc::Mat4f& lightVP() const noexcept
	{
		return m_lightViewProjection;
	}

	/// @brief 深度バッファからUV座標でサンプリングする
	/// @param u 水平UV [0, 1]
	/// @param v 垂直UV [0, 1]
	/// @return 深度値 [0, 1]（範囲外は1.0）
	[[nodiscard]] float sampleDepth(float u, float v) const noexcept
	{
		if (!m_initialized) return 1.0f;

		const int x = static_cast<int>(u * static_cast<float>(m_config.resolution - 1));
		const int y = static_cast<int>(v * static_cast<float>(m_config.resolution - 1));

		if (x < 0 || x >= m_config.resolution ||
		    y < 0 || y >= m_config.resolution)
		{
			return 1.0f;
		}

		const std::size_t idx =
			static_cast<std::size_t>(y * m_config.resolution + x);
		return m_depthBuffer[idx];
	}

	/// @brief ワールド空間の点が影の中にあるかを判定する
	/// @param worldPos ワールド空間の位置
	/// @param bias 深度バイアス（シャドウアクネ防止）
	/// @return 影の中にあればtrue
	[[nodiscard]] bool isInShadow(
		const sgc::Vec3f& worldPos,
		float bias = 0.005f) const noexcept
	{
		if (!m_initialized) return false;

		/// ワールド座標をライト空間NDCに変換する
		const sgc::Vec4f clip = m_lightViewProjection * sgc::Vec4f{
			worldPos.x, worldPos.y, worldPos.z, 1.0f
		};

		if (std::abs(clip.w) < 1e-6f) return false;

		const float ndcX = clip.x / clip.w;
		const float ndcY = clip.y / clip.w;
		const float ndcZ = clip.z / clip.w;

		/// NDC範囲外はシャドウなし
		if (ndcX < -1.0f || ndcX > 1.0f ||
		    ndcY < -1.0f || ndcY > 1.0f)
		{
			return false;
		}

		/// NDC→UV変換 [-1,1] → [0,1]
		const float u = (ndcX + 1.0f) * 0.5f;
		const float v = (1.0f - ndcY) * 0.5f;

		/// NDCのZを深度 [0,1] に変換する（[-1,1]→[0,1]）
		const float depth = (ndcZ + 1.0f) * 0.5f;

		const float shadowDepth = sampleDepth(u, v);

		return depth > shadowDepth + bias;
	}

	/// @brief 深度バッファのデータを取得する（テスト用）
	[[nodiscard]] const std::vector<float>& depthBuffer() const noexcept
	{
		return m_depthBuffer;
	}

private:
	/// @brief 深度バッファをリセットする（最大深度1.0で初期化）
	void clearDepthBuffer()
	{
		std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), 1.0f);
	}

	/// @brief ライト空間のビュー射影行列を計算する
	/// @param light ディレクショナルライト
	void computeLightViewProjection(const Light& light)
	{
		/// ライト方向を正規化する
		const sgc::Vec3f dir = light.direction.normalized();

		/// ライト位置をシーン中心から逆方向に配置する
		const sgc::Vec3f lightPos = dir * (-m_config.orthoSize);

		/// 上方向ベクトルを選択する（dirとほぼ平行な場合は別軸を使用）
		sgc::Vec3f up{0.0f, 1.0f, 0.0f};
		if (std::abs(dir.y) > 0.99f)
		{
			up = {0.0f, 0.0f, 1.0f};
		}

		const sgc::Mat4f view = sgc::Mat4f::lookAt(
			lightPos,
			sgc::Vec3f{0.0f, 0.0f, 0.0f},
			up);

		const float s = m_config.orthoSize;
		const sgc::Mat4f proj = sgc::Mat4f::orthographic(
			-s, s, -s, s,
			m_config.nearPlane,
			m_config.farPlane);

		m_lightViewProjection = proj * view;
	}

	/// @brief 三角形をライト空間深度バッファにラスタライズする
	/// @param a 頂点A（ローカル空間）
	/// @param b 頂点B（ローカル空間）
	/// @param c 頂点C（ローカル空間）
	/// @param world ワールド変換行列
	void rasterizeTriangle(
		const sgc::Vec3f& a,
		const sgc::Vec3f& b,
		const sgc::Vec3f& c,
		const sgc::Mat4f& world)
	{
		/// ワールド→ライトクリップ空間へ変換する
		const sgc::Vec4f ca = m_lightViewProjection * (world * sgc::Vec4f{a.x, a.y, a.z, 1.0f});
		const sgc::Vec4f cb = m_lightViewProjection * (world * sgc::Vec4f{b.x, b.y, b.z, 1.0f});
		const sgc::Vec4f cc = m_lightViewProjection * (world * sgc::Vec4f{c.x, c.y, c.z, 1.0f});

		if (std::abs(ca.w) < 1e-6f || std::abs(cb.w) < 1e-6f || std::abs(cc.w) < 1e-6f)
		{
			return;
		}

		/// NDC座標に変換する
		const float ax = ca.x / ca.w, ay = ca.y / ca.w, az = (ca.z / ca.w + 1.0f) * 0.5f;
		const float bx = cb.x / cb.w, by = cb.y / cb.w, bz = (cb.z / cb.w + 1.0f) * 0.5f;
		const float cx = cc.x / cc.w, cy = cc.y / cc.w, cz = (cc.z / cc.w + 1.0f) * 0.5f;

		/// NDC→ピクセル座標へ変換する
		const float res = static_cast<float>(m_config.resolution);
		const int pax = static_cast<int>((ax + 1.0f) * 0.5f * res);
		const int pay = static_cast<int>((1.0f - ay) * 0.5f * res);
		const int pbx = static_cast<int>((bx + 1.0f) * 0.5f * res);
		const int pby = static_cast<int>((1.0f - by) * 0.5f * res);
		const int pcx = static_cast<int>((cx + 1.0f) * 0.5f * res);
		const int pcy = static_cast<int>((1.0f - cy) * 0.5f * res);

		/// バウンディングボックスを計算してラスタライズする
		const int minX = std::max(0, std::min({pax, pbx, pcx}));
		const int maxX = std::min(m_config.resolution - 1, std::max({pax, pbx, pcx}));
		const int minY = std::max(0, std::min({pay, pby, pcy}));
		const int maxY = std::min(m_config.resolution - 1, std::max({pay, pby, pcy}));

		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				/// 重心座標を計算して三角形内判定する
				const float px = static_cast<float>(x) + 0.5f;
				const float py = static_cast<float>(y) + 0.5f;

				const float denom =
					static_cast<float>((pby - pcy) * (pax - pcx) + (pcx - pbx) * (pay - pcy));

				if (std::abs(denom) < 1e-6f) continue;

				const float wa =
					static_cast<float>((pby - pcy) * (px - static_cast<float>(pcx)) +
					                   (pcx - pbx) * (py - static_cast<float>(pcy))) / denom;
				const float wb =
					static_cast<float>((pcy - pay) * (px - static_cast<float>(pcx)) +
					                   (pax - pcx) * (py - static_cast<float>(pcy))) / denom;
				const float wc = 1.0f - wa - wb;

				if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;

				/// 深度を補間する
				const float depth = wa * az + wb * bz + wc * cz;

				/// 深度バッファを更新する
				const std::size_t idx =
					static_cast<std::size_t>(y * m_config.resolution + x);

				if (depth < m_depthBuffer[idx])
				{
					m_depthBuffer[idx] = depth;
				}
			}
		}
	}

	/// @brief 設定
	ShadowMapConfig m_config;
	/// @brief ライト空間ビュー射影行列
	sgc::Mat4f m_lightViewProjection;
	/// @brief 深度バッファ（解像度×解像度のfloat配列）
	std::vector<float> m_depthBuffer;
	/// @brief 初期化済みフラグ
	bool m_initialized = false;
};

} // namespace mitiru::render
