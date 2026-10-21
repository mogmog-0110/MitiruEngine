#pragma once

// registerMesh3D / updateMesh3D で受け取るメッシュの置き場。CPU 側の Mesh を持ち、描画時に GPU バッファへ写す。

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Mesh.hpp>

namespace mitiru::render::dawn_detail
{

class GameMeshStore
{
public:
	bool registerMesh(std::uint32_t id, const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices, int indexCount)
	{
		if (vertices == nullptr || vertexCount <= 0 || indexCount < 0) { return false; }
		if (indices != nullptr && std::any_of(indices, indices + indexCount, [vertexCount](std::uint32_t i) {
			    return i >= static_cast<std::uint32_t>(vertexCount);
		    }))
		{
			return false;
		}
		// 頂点数と添字数が同じなら、GPU バッファとの対応を保つため、同じ Mesh に写す。
		if (const auto it = m_meshes.find(id); it != m_meshes.end() &&
		    it->second->overwrite(vertices, static_cast<std::size_t>(vertexCount), indices,
		                          indices != nullptr ? static_cast<std::size_t>(indexCount) : 0))
		{
			return true;
		}
		auto mesh = std::make_unique<Mesh>();
		mesh->setVertices(std::vector<Vertex3D>(vertices, vertices + vertexCount));
		if (indices != nullptr && indexCount > 0) { mesh->setIndices(std::vector<std::uint32_t>(indices, indices + indexCount)); }
		m_meshes[id] = std::move(mesh);
		return true;
	}

	void release(std::uint32_t id) { m_meshes.erase(id); }

	[[nodiscard]] const Mesh* find(std::uint32_t id) const
	{
		const auto it = m_meshes.find(id);
		return it != m_meshes.end() ? it->second.get() : nullptr;
	}

	bool updateVertices(std::uint32_t id, const Vertex3D* vertices, int vertexCount)
	{
		if (vertices == nullptr || vertexCount <= 0) { return false; }
		const auto it = m_meshes.find(id);
		return it != m_meshes.end() && it->second->overwriteVertices(vertices, static_cast<std::size_t>(vertexCount));
	}

	// 頂点数と添字数が毎フレーム変わる面。登録時の数を容量とし、先頭から書き込む。足りないときは 2 の冪に広げる。
	bool updateTopology(std::uint32_t id, const Vertex3D* vertices, int vertexCount, const std::uint32_t* indices, int indexCount)
	{
		if (vertexCount < 0 || indexCount < 0 || indexCount % 3 != 0) { return false; }
		if ((vertexCount > 0 && vertices == nullptr) || (indexCount > 0 && indices == nullptr)) { return false; }
		if (std::any_of(indices, indices + indexCount, [vertexCount](std::uint32_t i) { return i >= static_cast<std::uint32_t>(vertexCount); }))
		{
			return false;
		}
		auto it = m_meshes.find(id);
		const auto n = static_cast<std::size_t>(vertexCount);
		const auto m = static_cast<std::size_t>(indexCount);
		if (it == m_meshes.end() || n > it->second->vertices().size() || m > it->second->indices().size())
		{
			const std::size_t haveV = it == m_meshes.end() ? 0 : it->second->vertices().size();
			const std::size_t haveI = it == m_meshes.end() ? 0 : it->second->indices().size();
			const std::size_t capV = std::max(std::bit_ceil(std::max<std::size_t>(n, 64)), haveV);
			const std::size_t capI = std::max(std::bit_ceil(std::max<std::size_t>(m, 192)), haveI);
			auto mesh = std::make_unique<Mesh>();
			mesh->setVertices(std::vector<Vertex3D>(capV));
			mesh->setIndices(std::vector<std::uint32_t>(capI, 0u));
			it = m_meshes.insert_or_assign(id, std::move(mesh)).first;
		}
		return it->second->overwriteActive(vertices, n, indices, m, it->second->indices().size());
	}

private:
	std::unordered_map<std::uint32_t, std::unique_ptr<Mesh>> m_meshes;
};

} // namespace mitiru::render::dawn_detail
