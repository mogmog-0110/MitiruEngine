#pragma once

/// @file OcclusionCuller.hpp
/// @brief ソフトウェアオクルージョンカリング
/// @details 深度バッファのHi-Zピラミッドを構築し、AABBのオクルージョンテストを行う。
///          GPUベースのHi-Zよりもシンプルだが、CPU上で動作するため
///          ドローコール送信前にカリング可能。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::render
{

/// @brief AABB（軸整列バウンディングボックス）
struct CullAABB
{
	float minX, minY, minZ;
	float maxX, maxY, maxZ;
};

/// @brief オクルージョンカリング結果
struct CullResult
{
	int totalObjects = 0;     ///< テスト対象オブジェクト数
	int visibleObjects = 0;   ///< 可視オブジェクト数
	int culledObjects = 0;    ///< カリングされたオブジェクト数
};

/// @brief ソフトウェアオクルージョンカラー（Hi-Z ピラミッド方式）
class OcclusionCuller
{
public:
	/// @brief 深度バッファのサイズを設定する
	/// @param width 幅
	/// @param height 高さ
	void resize(int width, int height)
	{
		m_width = width;
		m_height = height;
		m_depthBuffer.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 1.0f);
		buildMipChain();
	}

	/// @brief 深度バッファを更新する（フレーム先頭で呼ぶ）
	/// @param depthData 深度バッファデータ（float, 0=near, 1=far）
	/// @param width 幅
	/// @param height 高さ
	void updateDepth(const float* depthData, int width, int height)
	{
		if (width != m_width || height != m_height)
		{
			resize(width, height);
		}
		std::copy(depthData, depthData + static_cast<size_t>(width) * static_cast<size_t>(height),
		          m_depthBuffer.begin());
		buildHiZ();
	}

	/// @brief AABBがオクルードされているかテストする
	/// @param aabb テスト対象のAABB
	/// @param viewProj ビュー×プロジェクション行列 float[16]
	/// @return true: オクルードされている（非表示）、false: 可視
	[[nodiscard]] bool isOccluded(const CullAABB& aabb, const float viewProj[16]) const
	{
		// AABBの8頂点をスクリーン空間に射影
		float screenMinX = 1e30f, screenMinY = 1e30f, screenMinZ = 1e30f;
		float screenMaxX = -1e30f, screenMaxY = -1e30f;

		const float corners[8][3] = {
			{aabb.minX, aabb.minY, aabb.minZ}, {aabb.maxX, aabb.minY, aabb.minZ},
			{aabb.maxX, aabb.maxY, aabb.minZ}, {aabb.minX, aabb.maxY, aabb.minZ},
			{aabb.minX, aabb.minY, aabb.maxZ}, {aabb.maxX, aabb.minY, aabb.maxZ},
			{aabb.maxX, aabb.maxY, aabb.maxZ}, {aabb.minX, aabb.maxY, aabb.maxZ},
		};

		bool allBehind = true;
		for (const auto& c : corners)
		{
			// viewProj * position
			const float x = viewProj[0]*c[0] + viewProj[4]*c[1] + viewProj[8]*c[2] + viewProj[12];
			const float y = viewProj[1]*c[0] + viewProj[5]*c[1] + viewProj[9]*c[2] + viewProj[13];
			const float z = viewProj[2]*c[0] + viewProj[6]*c[1] + viewProj[10]*c[2] + viewProj[14];
			const float w = viewProj[3]*c[0] + viewProj[7]*c[1] + viewProj[11]*c[2] + viewProj[15];

			if (w <= 0.0f) { continue; }
			allBehind = false;

			const float ndcX = x / w;
			const float ndcY = y / w;
			const float ndcZ = z / w;

			const float sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(m_width);
			const float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(m_height);

			screenMinX = std::min(screenMinX, sx);
			screenMinY = std::min(screenMinY, sy);
			screenMaxX = std::max(screenMaxX, sx);
			screenMaxY = std::max(screenMaxY, sy);
			screenMinZ = std::min(screenMinZ, ndcZ);
		}

		if (allBehind) { return true; }

		// スクリーン外なら可視扱い（保守的）
		if (screenMaxX < 0 || screenMinX >= static_cast<float>(m_width)
			|| screenMaxY < 0 || screenMinY >= static_cast<float>(m_height))
		{
			return false;
		}

		// Hi-Zピラミッドでテスト
		return testHiZ(screenMinX, screenMinY, screenMaxX, screenMaxY, screenMinZ);
	}

	/// @brief 複数AABBを一括テストする
	[[nodiscard]] CullResult testBatch(
		const std::vector<CullAABB>& aabbs,
		const float viewProj[16]) const
	{
		CullResult result;
		result.totalObjects = static_cast<int>(aabbs.size());
		for (const auto& aabb : aabbs)
		{
			if (isOccluded(aabb, viewProj))
			{
				++result.culledObjects;
			}
			else
			{
				++result.visibleObjects;
			}
		}
		return result;
	}

private:
	int m_width = 0;
	int m_height = 0;
	std::vector<float> m_depthBuffer;

	// Hi-Zピラミッド（ミップレベル）
	struct MipLevel
	{
		int width = 0, height = 0;
		std::vector<float> data;
	};
	std::vector<MipLevel> m_hiZ;

	void buildMipChain()
	{
		m_hiZ.clear();
		int w = m_width, h = m_height;
		while (w > 1 || h > 1)
		{
			w = std::max(w / 2, 1);
			h = std::max(h / 2, 1);
			MipLevel level;
			level.width = w;
			level.height = h;
			level.data.resize(static_cast<size_t>(w) * static_cast<size_t>(h), 1.0f);
			m_hiZ.push_back(std::move(level));
		}
	}

	void buildHiZ()
	{
		if (m_hiZ.empty()) { return; }

		// Level 0: depth buffer → first mip (max filter)
		auto& mip0 = m_hiZ[0];
		for (int y = 0; y < mip0.height; ++y)
		{
			for (int x = 0; x < mip0.width; ++x)
			{
				const int sx = x * 2, sy = y * 2;
				float maxZ = 0.0f;
				for (int dy = 0; dy < 2 && (sy + dy) < m_height; ++dy)
				{
					for (int dx = 0; dx < 2 && (sx + dx) < m_width; ++dx)
					{
						const float z = m_depthBuffer[static_cast<size_t>((sy + dy) * m_width + (sx + dx))];
						maxZ = std::max(maxZ, z);
					}
				}
				mip0.data[static_cast<size_t>(y * mip0.width + x)] = maxZ;
			}
		}

		// Subsequent levels
		for (size_t i = 1; i < m_hiZ.size(); ++i)
		{
			const auto& prev = m_hiZ[i - 1];
			auto& cur = m_hiZ[i];
			for (int y = 0; y < cur.height; ++y)
			{
				for (int x = 0; x < cur.width; ++x)
				{
					const int sx = x * 2, sy = y * 2;
					float maxZ = 0.0f;
					for (int dy = 0; dy < 2 && (sy + dy) < prev.height; ++dy)
					{
						for (int dx = 0; dx < 2 && (sx + dx) < prev.width; ++dx)
						{
							maxZ = std::max(maxZ, prev.data[static_cast<size_t>(
								(sy + dy) * prev.width + (sx + dx))]);
						}
					}
					cur.data[static_cast<size_t>(y * cur.width + x)] = maxZ;
				}
			}
		}
	}

	[[nodiscard]] bool testHiZ(float minX, float minY, float maxX, float maxY, float testZ) const
	{
		// 適切なミップレベルを選択（AABBのスクリーンサイズに基づく）
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		const float maxDim = std::max(boxW, boxH);

		int level = 0;
		float levelSize = static_cast<float>(std::max(m_width, m_height)) * 0.5f;
		while (level + 1 < static_cast<int>(m_hiZ.size()) && levelSize > maxDim)
		{
			++level;
			levelSize *= 0.5f;
		}

		if (level >= static_cast<int>(m_hiZ.size())) { return false; }

		const auto& mip = m_hiZ[static_cast<size_t>(level)];
		const float scaleX = static_cast<float>(mip.width) / static_cast<float>(m_width);
		const float scaleY = static_cast<float>(mip.height) / static_cast<float>(m_height);

		const int ix = std::clamp(static_cast<int>(minX * scaleX), 0, mip.width - 1);
		const int iy = std::clamp(static_cast<int>(minY * scaleY), 0, mip.height - 1);

		const float hiZDepth = mip.data[static_cast<size_t>(iy * mip.width + ix)];

		// testZ > hiZDepth: オブジェクトはHi-Zの最遠値より奥 → オクルード
		return testZ > hiZDepth;
	}
};

} // namespace mitiru::render
