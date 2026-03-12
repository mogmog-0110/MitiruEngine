#pragma once

/// @file MeshCache.hpp
/// @brief メッシュアセットキャッシュ
///
/// 文字列IDでメッシュデータを管理するキャッシュ。
/// OBJ形式の文字列データからメッシュを構築し保持する。
///
/// @code
/// mitiru::asset::MeshCache cache;
/// cache.load("cube", objDataString);
/// const auto* mesh = cache.get("cube");
/// auto stats = cache.stats();
/// @endcode

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"

namespace mitiru::asset
{

/// @brief 頂点データ
struct CacheVertex
{
	sgc::Vec3f position{};  ///< 位置
	sgc::Vec3f normal{};    ///< 法線
};

/// @brief キャッシュ用メッシュデータ
struct CacheMesh
{
	std::vector<CacheVertex> vertices;  ///< 頂点配列
	std::vector<uint32_t> indices;      ///< インデックス配列
};

/// @brief メッシュキャッシュの統計情報
struct MeshCacheStats
{
	size_t count{0};          ///< メッシュ数
	size_t totalVertices{0};  ///< 総頂点数
	size_t totalIndices{0};   ///< 総インデックス数
};

/// @brief 簡易OBJパーサー（頂点と面のみ）
/// @param objData OBJ形式の文字列
/// @return パースされたメッシュ（失敗時はnullopt）
[[nodiscard]] inline std::optional<CacheMesh> parseSimpleObj(const std::string& objData)
{
	CacheMesh mesh;
	std::vector<sgc::Vec3f> positions;
	std::vector<sgc::Vec3f> normals;

	std::istringstream stream(objData);
	std::string line;

	while (std::getline(stream, line))
	{
		if (line.empty() || line[0] == '#') continue;

		std::istringstream lineStream(line);
		std::string prefix;
		lineStream >> prefix;

		if (prefix == "v")
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;
			lineStream >> x >> y >> z;
			positions.push_back({x, y, z});
		}
		else if (prefix == "vn")
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;
			lineStream >> x >> y >> z;
			normals.push_back({x, y, z});
		}
		else if (prefix == "f")
		{
			// 三角形の面（"f v1 v2 v3" または "f v1//vn1 v2//vn2 v3//vn3"）
			std::vector<uint32_t> faceIndices;
			std::string token;
			while (lineStream >> token)
			{
				// v//vn形式の解析
				const auto slashPos = token.find('/');
				int vIdx = 0;
				if (slashPos != std::string::npos)
				{
					vIdx = std::stoi(token.substr(0, slashPos)) - 1;
				}
				else
				{
					vIdx = std::stoi(token) - 1;
				}

				if (vIdx >= 0 && static_cast<size_t>(vIdx) < positions.size())
				{
					const auto vertexIndex = static_cast<uint32_t>(mesh.vertices.size());
					CacheVertex vert;
					vert.position = positions[static_cast<size_t>(vIdx)];
					mesh.vertices.push_back(vert);
					faceIndices.push_back(vertexIndex);
				}
			}

			// 三角形分割（ファントライアングレーション）
			for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
			{
				mesh.indices.push_back(faceIndices[0]);
				mesh.indices.push_back(faceIndices[i]);
				mesh.indices.push_back(faceIndices[i + 1]);
			}
		}
	}

	if (mesh.vertices.empty()) return std::nullopt;
	return mesh;
}

/// @brief メッシュアセットキャッシュ
class MeshCache
{
public:
	/// @brief OBJデータからメッシュをロードする
	/// @param id メッシュID
	/// @param objData OBJ形式の文字列データ
	/// @return ロード成功ならtrue
	bool load(const std::string& id, const std::string& objData)
	{
		auto mesh = parseSimpleObj(objData);
		if (!mesh) return false;
		m_meshes[id] = std::move(*mesh);
		return true;
	}

	/// @brief メッシュを取得する
	/// @param id メッシュID
	/// @return メッシュへのconstポインタ（存在しなければnullptr）
	[[nodiscard]] const CacheMesh* get(const std::string& id) const noexcept
	{
		auto it = m_meshes.find(id);
		if (it == m_meshes.end()) return nullptr;
		return &it->second;
	}

	/// @brief メッシュの有無を確認する
	/// @param id メッシュID
	/// @return 存在すればtrue
	[[nodiscard]] bool has(const std::string& id) const noexcept
	{
		return m_meshes.count(id) > 0;
	}

	/// @brief メッシュをアンロードする
	/// @param id メッシュID
	/// @return アンロード成功ならtrue
	bool unload(const std::string& id)
	{
		return m_meshes.erase(id) > 0;
	}

	/// @brief 全メッシュをクリアする
	void clear()
	{
		m_meshes.clear();
	}

	/// @brief キャッシュの統計情報を取得する
	/// @return 統計情報
	[[nodiscard]] MeshCacheStats stats() const noexcept
	{
		MeshCacheStats result;
		result.count = m_meshes.size();
		for (const auto& [id, mesh] : m_meshes)
		{
			result.totalVertices += mesh.vertices.size();
			result.totalIndices += mesh.indices.size();
		}
		return result;
	}

private:
	std::unordered_map<std::string, CacheMesh> m_meshes;  ///< メッシュ格納マップ
};

} // namespace mitiru::asset
