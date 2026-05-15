#pragma once

/// @file ClusteredLighting.hpp
/// @brief クラスター型ライティング
/// @details フラスタムを3Dグリッドに分割し、各クラスターに影響するライトを割り当てる。
///          大量ライト（100+）でも高速なライト-フラグメント対応付けを実現する。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::render
{

/// @brief ポイントライト情報
struct ClusterLight
{
	float position[3] = {0, 0, 0};
	float radius = 10.0f;
	float color[3] = {1, 1, 1};
	float intensity = 1.0f;
};

/// @brief クラスター設定
struct ClusterConfig
{
	int gridX = 16;      ///< X方向分割数
	int gridY = 9;       ///< Y方向分割数
	int gridZ = 24;      ///< Z方向分割数（深度）
	float nearPlane = 0.1f;
	float farPlane = 100.0f;
	int maxLightsPerCluster = 32;
};

/// @brief 1つのクラスターのライトインデックスリスト
struct Cluster
{
	int lightCount = 0;
	std::vector<int> lightIndices;
};

/// @brief クラスター型ライティングマネージャー
class ClusteredLighting
{
public:
	/// @brief 設定を適用する
	void configure(const ClusterConfig& config)
	{
		m_config = config;
		const int total = config.gridX * config.gridY * config.gridZ;
		m_clusters.resize(static_cast<size_t>(total));
		for (auto& c : m_clusters)
		{
			c.lightIndices.reserve(static_cast<size_t>(config.maxLightsPerCluster));
		}
	}

	/// @brief ライトリストを設定する
	void setLights(const std::vector<ClusterLight>& lights)
	{
		m_lights = lights;
	}

	/// @brief ライトを追加する
	void addLight(const ClusterLight& light)
	{
		m_lights.push_back(light);
	}

	/// @brief クラスターへのライト割り当てを実行する
	/// @param viewMatrix ビュー行列 float[16] (row-major)
	/// @param projMatrix プロジェクション行列 float[16]
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	void buildClusters(const float viewMatrix[16],
	                   [[maybe_unused]] const float projMatrix[16],
	                   int screenWidth, int screenHeight)
	{
		// クリア
		for (auto& c : m_clusters)
		{
			c.lightCount = 0;
			c.lightIndices.clear();
		}

		const float clusterW = static_cast<float>(screenWidth) / static_cast<float>(m_config.gridX);
		const float clusterH = static_cast<float>(screenHeight) / static_cast<float>(m_config.gridY);

		// 対数深度スライス
		const float logNear = std::log(m_config.nearPlane);
		const float logRange = std::log(m_config.farPlane) - logNear;

		for (int lightIdx = 0; lightIdx < static_cast<int>(m_lights.size()); ++lightIdx)
		{
			const auto& light = m_lights[static_cast<size_t>(lightIdx)];

			// ライト位置をビュー空間に変換
			const float vx = viewMatrix[0] * light.position[0] + viewMatrix[4] * light.position[1]
				+ viewMatrix[8] * light.position[2] + viewMatrix[12];
			const float vy = viewMatrix[1] * light.position[0] + viewMatrix[5] * light.position[1]
				+ viewMatrix[9] * light.position[2] + viewMatrix[13];
			const float vz = viewMatrix[2] * light.position[0] + viewMatrix[6] * light.position[1]
				+ viewMatrix[10] * light.position[2] + viewMatrix[14];

			const float depth = -vz; // ビュー空間ではZ負方向が前

			if (depth + light.radius < m_config.nearPlane
				|| depth - light.radius > m_config.farPlane)
			{
				continue; // フラスタム外
			}

			// ライトが影響するクラスター範囲を計算
			const float screenX = (vx / depth + 1.0f) * 0.5f * static_cast<float>(screenWidth);
			const float screenY = (vy / depth + 1.0f) * 0.5f * static_cast<float>(screenHeight);
			const float screenR = light.radius / depth * static_cast<float>(screenWidth) * 0.5f;

			const int minX = std::clamp(static_cast<int>((screenX - screenR) / clusterW), 0, m_config.gridX - 1);
			const int maxX = std::clamp(static_cast<int>((screenX + screenR) / clusterW), 0, m_config.gridX - 1);
			const int minY = std::clamp(static_cast<int>((screenY - screenR) / clusterH), 0, m_config.gridY - 1);
			const int maxY = std::clamp(static_cast<int>((screenY + screenR) / clusterH), 0, m_config.gridY - 1);

			const float depthMin = std::max(depth - light.radius, m_config.nearPlane);
			const float depthMax = std::min(depth + light.radius, m_config.farPlane);
			const int minZ = std::clamp(static_cast<int>(
				(std::log(depthMin) - logNear) / logRange * static_cast<float>(m_config.gridZ)),
				0, m_config.gridZ - 1);
			const int maxZ = std::clamp(static_cast<int>(
				(std::log(depthMax) - logNear) / logRange * static_cast<float>(m_config.gridZ)),
				0, m_config.gridZ - 1);

			// クラスターにライトを割り当て
			for (int z = minZ; z <= maxZ; ++z)
			{
				for (int y = minY; y <= maxY; ++y)
				{
					for (int x = minX; x <= maxX; ++x)
					{
						const int idx = z * m_config.gridX * m_config.gridY
							+ y * m_config.gridX + x;
						auto& cluster = m_clusters[static_cast<size_t>(idx)];
						if (cluster.lightCount < m_config.maxLightsPerCluster)
						{
							cluster.lightIndices.push_back(lightIdx);
							++cluster.lightCount;
						}
					}
				}
			}
		}
	}

	/// @brief 指定クラスターのライトリストを取得する
	[[nodiscard]] const Cluster& getCluster(int x, int y, int z) const
	{
		const int idx = z * m_config.gridX * m_config.gridY + y * m_config.gridX + x;
		return m_clusters[static_cast<size_t>(idx)];
	}

	/// @brief クラスター数を取得する
	[[nodiscard]] int totalClusters() const noexcept
	{
		return m_config.gridX * m_config.gridY * m_config.gridZ;
	}

	/// @brief ライト数を取得する
	[[nodiscard]] int lightCount() const noexcept { return static_cast<int>(m_lights.size()); }

	/// @brief クラスター設定を取得する
	[[nodiscard]] const ClusterConfig& config() const noexcept { return m_config; }

	/// @brief ライトリストをクリアする
	void clearLights() { m_lights.clear(); }

private:
	ClusterConfig m_config;
	std::vector<ClusterLight> m_lights;
	std::vector<Cluster> m_clusters;
};

} // namespace mitiru::render
