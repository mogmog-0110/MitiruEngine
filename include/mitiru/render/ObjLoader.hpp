#pragma once

/// @file ObjLoader.hpp
/// @brief Wavefront OBJメッシュローダー
/// @details OBJ形式の3Dモデルファイルをパースし、Meshオブジェクトに変換する。
///          三角形化されたメッシュのみサポートする。
///          対応フォーマット: v, vn, vt, f（v, v/vt, v/vt/vn, v//vn）

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

namespace detail
{

/// @brief 頂点インデックスの組み合わせ（重複排除キー）
struct ObjVertexKey
{
	int posIdx = -1;     ///< 頂点位置インデックス
	int texIdx = -1;     ///< テクスチャ座標インデックス
	int normIdx = -1;    ///< 法線インデックス

	/// @brief 等価比較
	bool operator==(const ObjVertexKey& other) const noexcept
	{
		return posIdx == other.posIdx &&
			texIdx == other.texIdx &&
			normIdx == other.normIdx;
	}
};

/// @brief ObjVertexKeyのハッシュ関数
struct ObjVertexKeyHash
{
	std::size_t operator()(const ObjVertexKey& key) const noexcept
	{
		/// FNV-1aベースのハッシュ結合
		auto h = static_cast<std::size_t>(key.posIdx);
		h ^= static_cast<std::size_t>(key.texIdx + 1) * 2654435761u;
		h ^= static_cast<std::size_t>(key.normIdx + 1) * 40499u;
		return h;
	}
};

/// @brief フェイス頂点文字列をパースする
/// @param token "v", "v/vt", "v/vt/vn", "v//vn" 形式の文字列
/// @return パースされた頂点キー
[[nodiscard]] inline ObjVertexKey parseFaceVertex(const std::string& token)
{
	ObjVertexKey key;

	/// スラッシュの位置を探す
	const auto slash1 = token.find('/');
	if (slash1 == std::string::npos)
	{
		/// "v" 形式
		key.posIdx = std::stoi(token) - 1;
		return key;
	}

	/// 位置インデックス
	key.posIdx = std::stoi(token.substr(0, slash1)) - 1;

	const auto slash2 = token.find('/', slash1 + 1);
	if (slash2 == std::string::npos)
	{
		/// "v/vt" 形式
		key.texIdx = std::stoi(token.substr(slash1 + 1)) - 1;
		return key;
	}

	/// "v/vt/vn" または "v//vn" 形式
	const auto texStr = token.substr(slash1 + 1, slash2 - slash1 - 1);
	if (!texStr.empty())
	{
		key.texIdx = std::stoi(texStr) - 1;
	}
	key.normIdx = std::stoi(token.substr(slash2 + 1)) - 1;

	return key;
}

} // namespace detail

/// @brief OBJ文字列からメッシュを読み込む
/// @param objData OBJ形式の文字列データ
/// @return パース成功時はMesh、失敗時はnullopt
///
/// @code
/// const char* objStr = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
/// auto mesh = mitiru::render::loadObjFromString(objStr);
/// if (mesh) { /* メッシュ使用 */ }
/// @endcode
[[nodiscard]] inline std::optional<Mesh> loadObjFromString(std::string_view objData)
{
	if (objData.empty())
	{
		return std::nullopt;
	}

	std::vector<sgc::Vec3f> positions;
	std::vector<sgc::Vec3f> normals;
	std::vector<sgc::Vec2f> texCoords;

	std::vector<Vertex3D> vertices;
	std::vector<uint32_t> indices;
	std::unordered_map<detail::ObjVertexKey, uint32_t,
		detail::ObjVertexKeyHash> vertexCache;

	const std::string objStr{objData};
	std::istringstream stream(objStr);
	std::string line;

	while (std::getline(stream, line))
	{
		/// 空行・コメントをスキップ
		if (line.empty() || line[0] == '#')
		{
			continue;
		}

		std::istringstream lineStream(line);
		std::string prefix;
		lineStream >> prefix;

		if (prefix == "v")
		{
			/// 頂点位置
			float x = 0.0f, y = 0.0f, z = 0.0f;
			lineStream >> x >> y >> z;
			positions.push_back({x, y, z});
		}
		else if (prefix == "vn")
		{
			/// 頂点法線
			float nx = 0.0f, ny = 0.0f, nz = 0.0f;
			lineStream >> nx >> ny >> nz;
			normals.push_back({nx, ny, nz});
		}
		else if (prefix == "vt")
		{
			/// テクスチャ座標
			float u = 0.0f, v = 0.0f;
			lineStream >> u >> v;
			texCoords.push_back({u, v});
		}
		else if (prefix == "f")
		{
			/// フェイス（三角形のみサポート）
			std::vector<std::string> faceTokens;
			std::string token;
			while (lineStream >> token)
			{
				faceTokens.push_back(token);
			}

			if (faceTokens.size() < 3)
			{
				continue;
			}

			/// 三角形ファン分割（4頂点以上のポリゴンも処理可能）
			for (std::size_t i = 1; i + 1 < faceTokens.size(); ++i)
			{
				const std::string triTokens[3] = {
					faceTokens[0],
					faceTokens[i],
					faceTokens[i + 1]
				};

				for (const auto& triToken : triTokens)
				{
					auto key = detail::parseFaceVertex(triToken);

					/// キャッシュで重複頂点を排除する
					auto it = vertexCache.find(key);
					if (it != vertexCache.end())
					{
						indices.push_back(it->second);
					}
					else
					{
						Vertex3D vert;

						if (key.posIdx >= 0 &&
							key.posIdx < static_cast<int>(positions.size()))
						{
							vert.position = positions[
								static_cast<std::size_t>(key.posIdx)];
						}

						if (key.normIdx >= 0 &&
							key.normIdx < static_cast<int>(normals.size()))
						{
							vert.normal = normals[
								static_cast<std::size_t>(key.normIdx)];
						}

						if (key.texIdx >= 0 &&
							key.texIdx < static_cast<int>(texCoords.size()))
						{
							vert.texCoord = texCoords[
								static_cast<std::size_t>(key.texIdx)];
						}

						const auto idx = static_cast<uint32_t>(vertices.size());
						vertices.push_back(vert);
						vertexCache[key] = idx;
						indices.push_back(idx);
					}
				}
			}
		}
	}

	if (vertices.empty() || indices.empty())
	{
		return std::nullopt;
	}

	Mesh mesh;
	mesh.setVertices(std::move(vertices));
	mesh.setIndices(std::move(indices));
	return mesh;
}

/// @brief OBJファイルからメッシュを読み込む
/// @param filePath OBJファイルのパス
/// @return パース成功時はMesh、失敗時はnullopt
///
/// @code
/// auto mesh = mitiru::render::loadObjFromFile("assets/model.obj");
/// @endcode
[[nodiscard]] inline std::optional<Mesh> loadObjFromFile(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file.is_open())
	{
		return std::nullopt;
	}

	std::string content(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());

	return loadObjFromString(content);
}

} // namespace mitiru::render
