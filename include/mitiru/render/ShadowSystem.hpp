#pragma once

/// @file ShadowSystem.hpp
/// @brief カスケードシャドウマップシステム
/// @details 複数カスケードを用いた高品質シャドウマッピング。
///          PCFソフトシャドウとVSM（分散シャドウマップ）をサポートする。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/debug/Log.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Mesh.hpp>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace mitiru::render
{

/// @brief シャドウマップフィルタリング方式
enum class ShadowFilterMode
{
	Hard,   ///< ハードシャドウ（フィルタリングなし）
	PCF,    ///< PCF（Percentage-Closer Filtering）
	VSM     ///< VSM（Variance Shadow Map）
};

/// @brief カスケードシャドウマップ設定
/// @details カスケード数・解像度・フィルタリングパラメータを定義する。
struct ShadowConfig
{
	int cascadeCount = 4;              ///< カスケード数 [1,4]
	int mapSize = 2048;                ///< シャドウマップ解像度（正方形）
	ShadowFilterMode filterMode = ShadowFilterMode::PCF; ///< フィルタリング方式
	int pcfRadius = 1;                 ///< PCFフィルタ半径（1=3x3, 2=5x5）
	float bias = 0.005f;               ///< 深度バイアス
	float normalBias = 0.02f;          ///< 法線方向バイアス
	float vsmBlurRadius = 2.0f;        ///< VSMガウシアンブラー半径
	std::array<float, 4> splitDistances{0.05f, 0.15f, 0.4f, 1.0f}; ///< カスケード分割距離（ニアからの比率）
};

/// @brief カスケード情報
/// @details 各カスケードのライト空間行列・深度バッファ・パラメータを保持する。
struct CascadeInfo
{
	sgc::Mat4f lightViewProj;          ///< ライト空間ビュー射影行列
	float splitNear = 0.0f;            ///< このカスケードの開始深度
	float splitFar = 0.0f;             ///< このカスケードの終了深度
	float bias = 0.005f;               ///< 個別バイアス
	std::vector<float> depthBuffer;    ///< 深度バッファ（ソフトウェア実装用）
	std::vector<float> momentBuffer;   ///< VSM: モーメントバッファ（depth, depth^2）

#ifdef _WIN32
	Microsoft::WRL::ComPtr<ID3D11Texture2D> shadowTexture;      ///< GPU深度テクスチャ
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;          ///< 深度ステンシルビュー
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;        ///< シェーダーリソースビュー
#endif
};

/// @brief カスケードシャドウマップ
/// @details ディレクショナルライト用のカスケードシャドウマッピングシステム。
///          カメラの視錐台を複数のカスケードに分割し、
///          各カスケードに異なる解像度のシャドウマップを割り当てる。
///
/// @code
/// mitiru::render::CascadedShadowMap csm;
/// csm.initialize({.cascadeCount = 4, .mapSize = 2048});
///
/// csm.updateCascades(camera, lightDir);
/// for (int i = 0; i < csm.cascadeCount(); ++i)
/// {
///     csm.beginShadowPass(i);
///     // メッシュを描画...
///     csm.endShadowPass();
/// }
///
/// float shadow = csm.sampleShadow(worldPos, cameraDepth);
/// @endcode
class CascadedShadowMap
{
public:
	/// @brief デフォルトコンストラクタ
	CascadedShadowMap() noexcept = default;

	/// @brief カスケードシャドウマップを初期化する
	/// @param cfg 設定
	void initialize(const ShadowConfig& cfg = {})
	{
		if (cfg.mapSize < 1)
		{
			MITIRU_LOG_ERROR("CascadedShadowMap",
				"mapSize must be >= 1");
			return;
		}

		m_config = cfg;
		m_config.cascadeCount = std::clamp(cfg.cascadeCount, 1, 4);

		const std::size_t bufSize =
			static_cast<std::size_t>(cfg.mapSize) * cfg.mapSize;

		m_cascades.resize(static_cast<std::size_t>(m_config.cascadeCount));
		for (auto& cascade : m_cascades)
		{
			cascade.depthBuffer.assign(bufSize, 1.0f);
			cascade.bias = cfg.bias;

			if (cfg.filterMode == ShadowFilterMode::VSM)
			{
				cascade.momentBuffer.assign(bufSize * 2, 0.0f);
			}
		}

		m_initialized = true;
		MITIRU_LOG_INFO("CascadedShadowMap",
			"initialized: cascades=" + std::to_string(m_config.cascadeCount)
			+ " mapSize=" + std::to_string(m_config.mapSize));
	}

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief カスケード数を取得する
	[[nodiscard]] int cascadeCount() const noexcept
	{
		return m_config.cascadeCount;
	}

	/// @brief 設定を取得する
	[[nodiscard]] const ShadowConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief カスケード情報を取得する
	/// @param index カスケードインデックス
	/// @return カスケード情報への参照
	[[nodiscard]] const CascadeInfo& cascade(int index) const
	{
		return m_cascades.at(static_cast<std::size_t>(index));
	}

	/// @brief カメラとライト方向からカスケードを更新する
	/// @param cameraView カメラビュー行列
	/// @param cameraProj カメラプロジェクション行列
	/// @param lightDir ライト方向ベクトル
	/// @param cameraNear カメラのニアクリップ面
	/// @param cameraFar カメラのファークリップ面
	void updateCascades(const sgc::Mat4f& cameraView,
	                    const sgc::Mat4f& cameraProj,
	                    const sgc::Vec3f& lightDir,
	                    float cameraNear,
	                    float cameraFar)
	{
		if (!m_initialized) return;

		const sgc::Vec3f dir = lightDir.normalized();

		/// カメラのビュー射影逆行列を計算する
		const sgc::Mat4f viewProjInv = (cameraProj * cameraView).inverse();

		for (int i = 0; i < m_config.cascadeCount; ++i)
		{
			const float prevSplit = (i == 0) ? 0.0f
				: m_config.splitDistances[static_cast<std::size_t>(i - 1)];
			const float curSplit =
				m_config.splitDistances[static_cast<std::size_t>(i)];

			const float splitNear = cameraNear + prevSplit * (cameraFar - cameraNear);
			const float splitFar = cameraNear + curSplit * (cameraFar - cameraNear);

			auto& cascade = m_cascades[static_cast<std::size_t>(i)];
			cascade.splitNear = splitNear;
			cascade.splitFar = splitFar;

			/// カスケード用のフラスタムコーナーを計算する
			const auto corners = computeFrustumCorners(
				viewProjInv, splitNear, splitFar, cameraNear, cameraFar);

			/// フラスタムの中心を計算する
			sgc::Vec3f center{0.0f, 0.0f, 0.0f};
			for (const auto& c : corners)
			{
				center = center + c;
			}
			center = center * (1.0f / 8.0f);

			/// フラスタムの半径を計算する（球に外接）
			float radius = 0.0f;
			for (const auto& c : corners)
			{
				const float dist = (c - center).length();
				radius = std::max(radius, dist);
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			/// ライトビュー行列を構築する
			const sgc::Vec3f lightPos = center - dir * radius;

			sgc::Vec3f up{0.0f, 1.0f, 0.0f};
			if (std::abs(dir.y) > 0.99f)
			{
				up = {0.0f, 0.0f, 1.0f};
			}

			const sgc::Mat4f lightView = sgc::Mat4f::lookAt(lightPos, center, up);
			const sgc::Mat4f lightProj = sgc::Mat4f::orthographic(
				-radius, radius, -radius, radius, 0.0f, radius * 2.0f);

			cascade.lightViewProj = lightProj * lightView;

			/// 深度バッファをクリアする
			std::fill(cascade.depthBuffer.begin(), cascade.depthBuffer.end(), 1.0f);
			if (m_config.filterMode == ShadowFilterMode::VSM)
			{
				std::fill(cascade.momentBuffer.begin(),
				          cascade.momentBuffer.end(), 0.0f);
			}
		}
	}

	/// @brief 指定カスケードのシャドウパスを開始する
	/// @param cascadeIndex カスケードインデックス [0, cascadeCount)
	void beginShadowPass(int cascadeIndex)
	{
		if (!m_initialized) return;
		if (cascadeIndex < 0 || cascadeIndex >= m_config.cascadeCount)
		{
			MITIRU_LOG_WARN("CascadedShadowMap",
				"beginShadowPass: invalid cascade index "
				+ std::to_string(cascadeIndex));
			return;
		}

		m_activeCascade = cascadeIndex;
		MITIRU_LOG_TRACE("CascadedShadowMap",
			"beginShadowPass cascade=" + std::to_string(cascadeIndex));
	}

	/// @brief メッシュをシャドウ深度バッファに記録する
	/// @param mesh メッシュデータ
	/// @param worldTransform ワールド変換行列
	void recordMesh(const Mesh& mesh, const sgc::Mat4f& worldTransform)
	{
		if (!m_initialized || m_activeCascade < 0) return;
		if (mesh.vertexCount() == 0) return;

		auto& cascade =
			m_cascades[static_cast<std::size_t>(m_activeCascade)];
		const auto& verts = mesh.vertices();
		const auto& indices = mesh.indices();

		if (!indices.empty())
		{
			for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				rasterizeShadowTriangle(
					verts[indices[i]].position,
					verts[indices[i + 1]].position,
					verts[indices[i + 2]].position,
					worldTransform, cascade);
			}
		}
		else
		{
			for (std::size_t i = 0; i + 2 < verts.size(); i += 3)
			{
				rasterizeShadowTriangle(
					verts[i].position,
					verts[i + 1].position,
					verts[i + 2].position,
					worldTransform, cascade);
			}
		}
	}

	/// @brief シャドウパスを終了する
	void endShadowPass()
	{
		if (!m_initialized || m_activeCascade < 0) return;

		/// VSMモードの場合、ガウシアンブラーを適用する
		if (m_config.filterMode == ShadowFilterMode::VSM)
		{
			applyGaussianBlur(
				m_cascades[static_cast<std::size_t>(m_activeCascade)]);
		}

		m_activeCascade = -1;
	}

	/// @brief ワールド空間の点のシャドウ係数を取得する
	/// @param worldPos ワールド空間位置
	/// @param cameraDepth カメラからの深度（カスケード選択用）
	/// @return シャドウ係数 [0=完全な影, 1=影なし]
	[[nodiscard]] float sampleShadow(const sgc::Vec3f& worldPos,
	                                 float cameraDepth) const noexcept
	{
		if (!m_initialized) return 1.0f;

		/// 適切なカスケードを選択する
		const int cascadeIdx = selectCascade(cameraDepth);
		if (cascadeIdx < 0) return 1.0f;

		const auto& cascade =
			m_cascades[static_cast<std::size_t>(cascadeIdx)];

		/// ワールド座標をライト空間NDCに変換する
		const sgc::Vec4f clip = cascade.lightViewProj * sgc::Vec4f{
			worldPos.x, worldPos.y, worldPos.z, 1.0f
		};

		if (std::abs(clip.w) < 1e-6f) return 1.0f;

		const float ndcX = clip.x / clip.w;
		const float ndcY = clip.y / clip.w;
		const float ndcZ = clip.z / clip.w;

		if (ndcX < -1.0f || ndcX > 1.0f ||
		    ndcY < -1.0f || ndcY > 1.0f)
		{
			return 1.0f;
		}

		const float u = (ndcX + 1.0f) * 0.5f;
		const float v = (1.0f - ndcY) * 0.5f;
		const float depth = (ndcZ + 1.0f) * 0.5f;

		switch (m_config.filterMode)
		{
		case ShadowFilterMode::Hard:
			return sampleHard(cascade, u, v, depth);

		case ShadowFilterMode::PCF:
			return samplePCF(cascade, u, v, depth);

		case ShadowFilterMode::VSM:
			return sampleVSM(cascade, u, v, depth);
		}

		return 1.0f;
	}

	/// @brief 深度バッファを取得する（テスト用）
	/// @param cascadeIndex カスケードインデックス
	[[nodiscard]] const std::vector<float>& depthBuffer(int cascadeIndex) const
	{
		return m_cascades.at(static_cast<std::size_t>(cascadeIndex)).depthBuffer;
	}

private:
	/// @brief カメラ深度から適切なカスケードを選択する
	[[nodiscard]] int selectCascade(float cameraDepth) const noexcept
	{
		for (int i = 0; i < m_config.cascadeCount; ++i)
		{
			const auto& cascade = m_cascades[static_cast<std::size_t>(i)];
			if (cameraDepth <= cascade.splitFar)
			{
				return i;
			}
		}
		return m_config.cascadeCount - 1;
	}

	/// @brief UV座標から深度バッファインデックスを計算する
	[[nodiscard]] int uvToIndex(float u, float v) const noexcept
	{
		const int x = static_cast<int>(u * static_cast<float>(m_config.mapSize - 1));
		const int y = static_cast<int>(v * static_cast<float>(m_config.mapSize - 1));

		if (x < 0 || x >= m_config.mapSize ||
		    y < 0 || y >= m_config.mapSize)
		{
			return -1;
		}
		return y * m_config.mapSize + x;
	}

	/// @brief ハードシャドウサンプリング
	[[nodiscard]] float sampleHard(const CascadeInfo& cascade,
	                               float u, float v, float depth) const noexcept
	{
		const int idx = uvToIndex(u, v);
		if (idx < 0) return 1.0f;

		return (depth > cascade.depthBuffer[static_cast<std::size_t>(idx)]
			+ cascade.bias) ? 0.0f : 1.0f;
	}

	/// @brief PCFソフトシャドウサンプリング（3x3, 5x5等）
	[[nodiscard]] float samplePCF(const CascadeInfo& cascade,
	                              float u, float v, float depth) const noexcept
	{
		const int radius = m_config.pcfRadius;
		const float texelSize = 1.0f / static_cast<float>(m_config.mapSize);

		float shadow = 0.0f;
		int sampleCount = 0;

		for (int dy = -radius; dy <= radius; ++dy)
		{
			for (int dx = -radius; dx <= radius; ++dx)
			{
				const float su = u + static_cast<float>(dx) * texelSize;
				const float sv = v + static_cast<float>(dy) * texelSize;

				const int idx = uvToIndex(su, sv);
				if (idx < 0)
				{
					shadow += 1.0f;
				}
				else
				{
					const float shadowDepth =
						cascade.depthBuffer[static_cast<std::size_t>(idx)];
					shadow += (depth > shadowDepth + cascade.bias) ? 0.0f : 1.0f;
				}
				++sampleCount;
			}
		}

		return shadow / static_cast<float>(sampleCount);
	}

	/// @brief VSMサンプリング（Chebyshevの不等式を使用）
	[[nodiscard]] float sampleVSM(const CascadeInfo& cascade,
	                              float u, float v, float depth) const noexcept
	{
		const int idx = uvToIndex(u, v);
		if (idx < 0) return 1.0f;

		const std::size_t momentIdx = static_cast<std::size_t>(idx) * 2;
		if (momentIdx + 1 >= cascade.momentBuffer.size()) return 1.0f;

		const float moment1 = cascade.momentBuffer[momentIdx];     // E[z]
		const float moment2 = cascade.momentBuffer[momentIdx + 1]; // E[z^2]

		/// 完全にシャドウの外側
		if (depth <= moment1) return 1.0f;

		/// Chebyshevの不等式
		const float variance = std::max(moment2 - moment1 * moment1, 0.0001f);
		const float d = depth - moment1;
		const float pMax = variance / (variance + d * d);

		return std::max(pMax, 0.0f);
	}

	/// @brief 三角形をシャドウ深度バッファにラスタライズする
	void rasterizeShadowTriangle(
		const sgc::Vec3f& a,
		const sgc::Vec3f& b,
		const sgc::Vec3f& c,
		const sgc::Mat4f& world,
		CascadeInfo& cascade)
	{
		const sgc::Mat4f& lvp = cascade.lightViewProj;

		const sgc::Vec4f ca = lvp * (world * sgc::Vec4f{a.x, a.y, a.z, 1.0f});
		const sgc::Vec4f cb = lvp * (world * sgc::Vec4f{b.x, b.y, b.z, 1.0f});
		const sgc::Vec4f cc = lvp * (world * sgc::Vec4f{c.x, c.y, c.z, 1.0f});

		if (std::abs(ca.w) < 1e-6f || std::abs(cb.w) < 1e-6f ||
		    std::abs(cc.w) < 1e-6f)
		{
			return;
		}

		const float ax = ca.x / ca.w, ay = ca.y / ca.w;
		const float az = (ca.z / ca.w + 1.0f) * 0.5f;
		const float bx = cb.x / cb.w, by = cb.y / cb.w;
		const float bz = (cb.z / cb.w + 1.0f) * 0.5f;
		const float cx = cc.x / cc.w, cy = cc.y / cc.w;
		const float cz = (cc.z / cc.w + 1.0f) * 0.5f;

		const float res = static_cast<float>(m_config.mapSize);
		const int pax = static_cast<int>((ax + 1.0f) * 0.5f * res);
		const int pay = static_cast<int>((1.0f - ay) * 0.5f * res);
		const int pbx = static_cast<int>((bx + 1.0f) * 0.5f * res);
		const int pby = static_cast<int>((1.0f - by) * 0.5f * res);
		const int pcx = static_cast<int>((cx + 1.0f) * 0.5f * res);
		const int pcy = static_cast<int>((1.0f - cy) * 0.5f * res);

		const int minX = std::max(0, std::min({pax, pbx, pcx}));
		const int maxX = std::min(m_config.mapSize - 1, std::max({pax, pbx, pcx}));
		const int minY = std::max(0, std::min({pay, pby, pcy}));
		const int maxY = std::min(m_config.mapSize - 1, std::max({pay, pby, pcy}));

		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				const float px = static_cast<float>(x) + 0.5f;
				const float py = static_cast<float>(y) + 0.5f;

				const float denom = static_cast<float>(
					(pby - pcy) * (pax - pcx) + (pcx - pbx) * (pay - pcy));
				if (std::abs(denom) < 1e-6f) continue;

				const float wa = static_cast<float>(
					(pby - pcy) * (px - static_cast<float>(pcx)) +
					(pcx - pbx) * (py - static_cast<float>(pcy))) / denom;
				const float wb = static_cast<float>(
					(pcy - pay) * (px - static_cast<float>(pcx)) +
					(pax - pcx) * (py - static_cast<float>(pcy))) / denom;
				const float wc = 1.0f - wa - wb;

				if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;

				const float depth = wa * az + wb * bz + wc * cz;
				const std::size_t idx =
					static_cast<std::size_t>(y * m_config.mapSize + x);

				if (depth < cascade.depthBuffer[idx])
				{
					cascade.depthBuffer[idx] = depth;
				}

				/// VSMの場合、モーメントも記録する
				if (m_config.filterMode == ShadowFilterMode::VSM)
				{
					const std::size_t mIdx = idx * 2;
					cascade.momentBuffer[mIdx] = depth;
					cascade.momentBuffer[mIdx + 1] = depth * depth;
				}
			}
		}
	}

	/// @brief フラスタムコーナーをNDCから計算する
	/// @return 8つのコーナー座標
	[[nodiscard]] static std::array<sgc::Vec3f, 8> computeFrustumCorners(
		const sgc::Mat4f& viewProjInv,
		float splitNear, float splitFar,
		float cameraNear, float cameraFar)
	{
		/// NDC Z値を分割距離から計算する
		const float range = cameraFar - cameraNear;
		const float nearZ = (range > 0.0f)
			? (2.0f * (splitNear - cameraNear) / range - 1.0f) : -1.0f;
		const float farZ = (range > 0.0f)
			? (2.0f * (splitFar - cameraNear) / range - 1.0f) : 1.0f;

		/// NDC空間の8コーナー
		const std::array<sgc::Vec4f, 8> ndcCorners{{
			{-1.0f, -1.0f, nearZ, 1.0f},
			{ 1.0f, -1.0f, nearZ, 1.0f},
			{ 1.0f,  1.0f, nearZ, 1.0f},
			{-1.0f,  1.0f, nearZ, 1.0f},
			{-1.0f, -1.0f, farZ, 1.0f},
			{ 1.0f, -1.0f, farZ, 1.0f},
			{ 1.0f,  1.0f, farZ, 1.0f},
			{-1.0f,  1.0f, farZ, 1.0f},
		}};

		std::array<sgc::Vec3f, 8> corners;
		for (std::size_t i = 0; i < 8; ++i)
		{
			const sgc::Vec4f world = viewProjInv * ndcCorners[i];
			if (std::abs(world.w) > 1e-6f)
			{
				corners[i] = sgc::Vec3f{
					world.x / world.w,
					world.y / world.w,
					world.z / world.w
				};
			}
		}

		return corners;
	}

	/// @brief VSM用ガウシアンブラーを適用する
	void applyGaussianBlur(CascadeInfo& cascade)
	{
		if (cascade.momentBuffer.empty()) return;

		const int size = m_config.mapSize;
		const int radius = static_cast<int>(m_config.vsmBlurRadius);
		if (radius <= 0) return;

		/// ガウシアンカーネルを生成する
		std::vector<float> kernel(static_cast<std::size_t>(radius * 2 + 1));
		float sum = 0.0f;
		const float sigma = m_config.vsmBlurRadius * 0.5f;
		for (int i = -radius; i <= radius; ++i)
		{
			const float val = std::exp(
				-static_cast<float>(i * i) / (2.0f * sigma * sigma));
			kernel[static_cast<std::size_t>(i + radius)] = val;
			sum += val;
		}
		for (auto& k : kernel)
		{
			k /= sum;
		}

		/// 水平パス
		std::vector<float> temp(cascade.momentBuffer.size());
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				float m1 = 0.0f;
				float m2 = 0.0f;
				for (int dx = -radius; dx <= radius; ++dx)
				{
					const int sx = std::clamp(x + dx, 0, size - 1);
					const std::size_t idx =
						static_cast<std::size_t>(y * size + sx) * 2;
					const float w = kernel[static_cast<std::size_t>(dx + radius)];
					m1 += cascade.momentBuffer[idx] * w;
					m2 += cascade.momentBuffer[idx + 1] * w;
				}
				const std::size_t outIdx =
					static_cast<std::size_t>(y * size + x) * 2;
				temp[outIdx] = m1;
				temp[outIdx + 1] = m2;
			}
		}

		/// 垂直パス
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				float m1 = 0.0f;
				float m2 = 0.0f;
				for (int dy = -radius; dy <= radius; ++dy)
				{
					const int sy = std::clamp(y + dy, 0, size - 1);
					const std::size_t idx =
						static_cast<std::size_t>(sy * size + x) * 2;
					const float w = kernel[static_cast<std::size_t>(dy + radius)];
					m1 += temp[idx] * w;
					m2 += temp[idx + 1] * w;
				}
				const std::size_t outIdx =
					static_cast<std::size_t>(y * size + x) * 2;
				cascade.momentBuffer[outIdx] = m1;
				cascade.momentBuffer[outIdx + 1] = m2;
			}
		}
	}

	ShadowConfig m_config;                    ///< 設定
	std::vector<CascadeInfo> m_cascades;      ///< カスケード情報配列
	int m_activeCascade = -1;                 ///< 現在アクティブなカスケードインデックス
	bool m_initialized = false;               ///< 初期化済みフラグ
};

} // namespace mitiru::render
