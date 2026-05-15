#pragma once

/// @file MeshCache.hpp
/// @brief OBJバイナリキャッシュ付きメッシュローダー
/// @details OBJファイルを初回パース後に.mbin形式で保存し、
///          次回以降は高速バイナリロードで読み込む。

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include <mitiru/core/BinarySerializer.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/ObjLoaderTiny.hpp>

namespace mitiru::render
{

/// @brief OBJバイナリキャッシュ付きメッシュローダー
/// @details 初回: OBJをパースし.mbinとして保存。以降: .mbinから高速ロード。
///
/// @code
/// mitiru::render::MeshCache cache;
/// const auto* mesh = cache.loadMesh("assets/models/strawberry.obj");
/// if (mesh) { /* 描画 */ }
/// @endcode
class MeshCache
{
public:
	MeshCache() = default;

	/// @brief メッシュをロードする（キャッシュ優先）
	/// @param objPath OBJファイルのパス
	/// @return メッシュへのポインタ（失敗時nullptr、ライフタイムはMeshCacheに従う）
	[[nodiscard]] const Mesh* loadMesh(const std::string& objPath)
	{
		// メモリキャッシュヒット
		auto it = m_cache.find(objPath);
		if (it != m_cache.end())
		{
			return it->second.get();
		}

		const std::string mbinPath = deriveMbinPath(objPath);

		std::unique_ptr<Mesh> mesh;

		if (isBinaryCacheValid(objPath, mbinPath))
		{
			mesh = loadFromBinary(mbinPath);
		}

		if (!mesh)
		{
			mesh = loadFromObj(objPath);
			if (mesh)
			{
				saveToBinary(*mesh, mbinPath);
			}
		}

		if (!mesh)
		{
			return nullptr;
		}

		auto* ptr = mesh.get();
		m_cache.emplace(objPath, std::move(mesh));
		return ptr;
	}

	/// @brief メモリキャッシュをクリアする
	void clear() noexcept { m_cache.clear(); }

	/// @brief キャッシュ済みメッシュ数を返す
	[[nodiscard]] std::size_t size() const noexcept { return m_cache.size(); }

private:
	std::unordered_map<std::string, std::unique_ptr<Mesh>> m_cache;

	/// @brief OBJパスから.mbinパスを導出する
	[[nodiscard]] static std::string deriveMbinPath(const std::string& objPath)
	{
		// .obj -> .mbin (拡張子置換)
		auto dotPos = objPath.rfind('.');
		if (dotPos != std::string::npos)
		{
			return objPath.substr(0, dotPos) + ".mbin";
		}
		return objPath + ".mbin";
	}

	/// @brief バイナリキャッシュが有効か判定する
	/// @details .mbinが存在し、かつ.objより新しい場合にtrue
	[[nodiscard]] static bool isBinaryCacheValid(const std::string& objPath,
	                                              const std::string& mbinPath)
	{
		namespace fs = std::filesystem;
		std::error_code ec;

		if (!fs::exists(mbinPath, ec))
		{
			return false;
		}

		auto objTime = fs::last_write_time(objPath, ec);
		if (ec) return false;

		auto mbinTime = fs::last_write_time(mbinPath, ec);
		if (ec) return false;

		return mbinTime >= objTime;
	}

	/// @brief OBJファイルからメッシュをロードする
	[[nodiscard]] static std::unique_ptr<Mesh> loadFromObj(const std::string& objPath)
	{
		auto opt = loadObjWithMaterials(objPath);
		if (!opt.has_value())
		{
			return nullptr;
		}
		return std::make_unique<Mesh>(std::move(opt.value()));
	}

	/// @brief メッシュをバイナリファイルに保存する
	static void saveToBinary(const Mesh& mesh, const std::string& mbinPath)
	{
		BinaryWriter writer(mbinPath);
		if (!writer.isOpen()) return;

		// Chunk ID: "MESH"
		writer.writeRaw("MESH", 4);

		// 頂点数・インデックス数
		const auto& verts = mesh.vertices();
		const auto& idxs = mesh.indices();
		writer.writeU32(static_cast<uint32_t>(verts.size()));
		writer.writeU32(static_cast<uint32_t>(idxs.size()));

		// 頂点データ（position, normal, texCoord, color）
		for (const auto& v : verts)
		{
			writer.writeF32(v.position.x);
			writer.writeF32(v.position.y);
			writer.writeF32(v.position.z);
			writer.writeF32(v.normal.x);
			writer.writeF32(v.normal.y);
			writer.writeF32(v.normal.z);
			writer.writeF32(v.texCoord.x);
			writer.writeF32(v.texCoord.y);
			writer.writeF32(v.color.r);
			writer.writeF32(v.color.g);
			writer.writeF32(v.color.b);
			writer.writeF32(v.color.a);
		}

		// インデックスデータ
		for (uint32_t idx : idxs)
		{
			writer.writeU32(idx);
		}
	}

	/// @brief バイナリファイルからメッシュをロードする
	[[nodiscard]] static std::unique_ptr<Mesh> loadFromBinary(const std::string& mbinPath)
	{
		BinaryReader reader(mbinPath);
		if (!reader.isOpen()) return nullptr;

		// Chunk ID確認
		char chunk[4] = {};
		// BinaryReaderはMBINヘッダを既に消費済み。次の4バイトがチャンクID。
		// readU32を2回使ってchunkを読む（4バイト一括）
		uint32_t chunkId = reader.readU32();
		std::memcpy(chunk, &chunkId, 4);
		if (std::memcmp(chunk, "MESH", 4) != 0)
		{
			return nullptr;
		}

		uint32_t vertCount = reader.readU32();
		uint32_t idxCount = reader.readU32();

		std::vector<Vertex3D> verts;
		verts.reserve(vertCount);

		for (uint32_t i = 0; i < vertCount; ++i)
		{
			Vertex3D v;
			v.position.x = reader.readF32();
			v.position.y = reader.readF32();
			v.position.z = reader.readF32();
			v.normal.x = reader.readF32();
			v.normal.y = reader.readF32();
			v.normal.z = reader.readF32();
			v.texCoord.x = reader.readF32();
			v.texCoord.y = reader.readF32();
			v.color.r = reader.readF32();
			v.color.g = reader.readF32();
			v.color.b = reader.readF32();
			v.color.a = reader.readF32();
			verts.push_back(v);
		}

		std::vector<uint32_t> idxs;
		idxs.reserve(idxCount);
		for (uint32_t i = 0; i < idxCount; ++i)
		{
			idxs.push_back(reader.readU32());
		}

		auto mesh = std::make_unique<Mesh>();
		mesh->setVertices(std::move(verts));
		mesh->setIndices(std::move(idxs));
		return mesh;
	}
};

} // namespace mitiru::render
