#pragma once
// nav ページの地図。真上から見下ろした絵を CPU で RGBA に塗る。画面の右が +x、下が +z (右手系で +y から見下ろす向き)。
// ナビメッシュの三角形は焼いたファイル (.navmesh / .navcache) から読み、agent と障害物は gameMemory の値から取る。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::tool
{

/// ナビメッシュを真上から見た形。tris は三角形ごとに (x, z) を 3 組、edges は外周の辺ごとに (x, z) を 2 組。
struct NavFootprint
{
	std::vector<float> tris;
	std::vector<float> edges;
	[[nodiscard]] std::size_t triangleCount() const noexcept { return tris.size() / 6; }
};

/// ファイルを読んで形を返す。失敗なら理由 (Detour を持たないビルドでもここで理由を返す)。
[[nodiscard]] std::string loadNavFootprint(const std::string& path, NavFootprint& out);

struct MapAgent
{
	float x = 0.0f;
	float z = 0.0f;
	float vx = 0.0f;
	float vz = 0.0f;
	bool stuck = false;    ///< failed / offMesh / inactive (中を抜いた丸で描く)
	bool moving = false;
};

struct MapObstacle
{
	float cx = 0.0f;
	float cz = 0.0f;
	float hx = 0.0f;
	float hz = 0.0f;
	float yaw = 0.0f;      ///< Y 軸回り (rad、右手系。DynamicNavMesh と同じ回し方)
	bool cylinder = false;
	bool enabled = false;
};

struct MapImage
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;
	float gridMeters = 0.0f;   ///< 格子 1 マスの大きさ
	float minX = 0.0f;         ///< 左上の世界座標と 1 m の画素数 (テストが位置を測るため)
	float minZ = 0.0f;
	float pixelsPerMeter = 0.0f;
};

[[nodiscard]] MapImage renderNavMap(const NavFootprint& mesh, const std::vector<MapAgent>& agents,
                                    const std::vector<MapObstacle>& obstacles, int width);

} // namespace mitiru::tool
