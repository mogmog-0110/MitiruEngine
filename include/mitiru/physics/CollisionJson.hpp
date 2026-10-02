#pragma once

/// @file CollisionJson.hpp
/// @brief `--collision` の地形 JSON (箱と三角形メッシュの列) を読む。
/// @details host の物理問い合わせ job (Jolt の静的 world) と、game DLL が自分で持つ地形
///          (`mitiru/action/CollisionLevelLoad.hpp`) が同じ読み方をするよう、ここ 1 か所に置く。
///          root は配列か `{"boxes":[...]}`。各要素は `{"min","max"}` の箱か `{"vertices","indices"}` の
///          三角形で、`layer` (0-31、省略時 0) を持てる。型の合わない要素は飛ばし、例外で落ちない。

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sgc/math/Vec3.hpp"

namespace mitiru::physics3d
{

/// @brief 地形の要素 1 つ。Box は min / max、Mesh は vertices / indices を使う
struct CollisionJsonShape
{
	enum class Kind : std::uint8_t
	{
		Box,
		Mesh,
	};
	Kind                       kind = Kind::Box;
	std::uint32_t              layer = 0;
	sgc::Vec3f                 min{};
	sgc::Vec3f                 max{};
	std::vector<sgc::Vec3f>    vertices;
	std::vector<std::uint32_t> indices;
};

enum class CollisionJsonError : std::uint8_t
{
	None,
	Open,   ///< ファイルを開けない
	Parse,  ///< JSON の構文エラー
	Shape,  ///< root が配列でも {"boxes":[...]} でもない
};

struct CollisionJsonFile
{
	std::vector<CollisionJsonShape> shapes;
	CollisionJsonError              error = CollisionJsonError::None;
};

namespace collision_json
{

/// @brief float に収まる有限値だけ受ける。範囲外の座標は物理の形状計算をおかしくする
[[nodiscard]] inline bool readCoord(const nlohmann::json& v, float& out)
{
	if (!v.is_number()) return false;
	const double d = v.get<double>();
	if (!std::isfinite(d) || std::fabs(d) > 1.0e7) return false;
	out = static_cast<float>(d);
	return true;
}

[[nodiscard]] inline bool readVec3(const nlohmann::json& j, sgc::Vec3f& out)
{
	if (!j.is_array() || j.size() < 3) return false;
	return readCoord(j[0], out.x) && readCoord(j[1], out.y) && readCoord(j[2], out.z);
}

[[nodiscard]] inline bool readBox(const nlohmann::json& e, CollisionJsonShape& out)
{
	if (!e.contains("min") || !e.contains("max") || !readVec3(e["min"], out.min) || !readVec3(e["max"], out.max))
		return false;
	out.kind = CollisionJsonShape::Kind::Box;
	return true;
}

[[nodiscard]] inline bool readMesh(const nlohmann::json& e, CollisionJsonShape& out)
{
	if (!e.contains("vertices") || !e.contains("indices") || !e["vertices"].is_array() || !e["indices"].is_array())
		return false;
	for (const auto& v : e["vertices"])
	{
		sgc::Vec3f p;
		if (!readVec3(v, p)) return false;
		out.vertices.push_back(p);
	}
	for (const auto& i : e["indices"])
	{
		if (!i.is_number_unsigned() || i.get<std::uint64_t>() > 0xFFFFFFFFull) return false;
		out.indices.push_back(static_cast<std::uint32_t>(i.get<std::uint64_t>()));
	}
	out.kind = CollisionJsonShape::Kind::Mesh;
	return true;
}

/// @brief `layer` (省略時 0) を読む。0-31 の整数でなければ false
[[nodiscard]] inline bool readLayer(const nlohmann::json& e, std::uint32_t& out)
{
	if (!e.contains("layer")) { out = 0; return true; }
	const auto& v = e["layer"];
	if (!v.is_number_unsigned() || v.get<std::uint64_t>() > 31u) return false;
	out = static_cast<std::uint32_t>(v.get<std::uint64_t>());
	return true;
}

} // namespace collision_json

/// @brief root から読める要素だけを取り出す。root の形が違えば error = Shape
[[nodiscard]] inline CollisionJsonFile parseCollisionJson(const nlohmann::json& root)
{
	CollisionJsonFile out;
	const nlohmann::json* list = nullptr;
	if (root.is_array()) { list = &root; }
	else if (root.is_object() && root.contains("boxes") && root["boxes"].is_array()) { list = &root["boxes"]; }
	if (list == nullptr) { out.error = CollisionJsonError::Shape; return out; }
	for (const auto& e : *list)
	{
		if (!e.is_object()) continue;
		CollisionJsonShape shape;
		if (collision_json::readLayer(e, shape.layer) &&
		    (collision_json::readBox(e, shape) || collision_json::readMesh(e, shape)))
		{
			out.shapes.push_back(std::move(shape));
		}
	}
	return out;
}

[[nodiscard]] inline CollisionJsonFile readCollisionJsonFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) { return {{}, CollisionJsonError::Open}; }
	nlohmann::json j;
	try { in >> j; }
	catch (...) { return {{}, CollisionJsonError::Parse}; }
	return parseCollisionJson(j);
}

} // namespace mitiru::physics3d
