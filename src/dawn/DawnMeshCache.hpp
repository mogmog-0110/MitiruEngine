#pragma once

// CPU の Mesh を GPU の頂点・添字バッファとして保持するキャッシュの宣言。Mesh のアドレスをキーにし、revision が変わったときだけ転送する。使われなくなったバッファは一定のフレーム数が過ぎると破棄する。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Mesh.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"

namespace mitiru::render::dawn_detail
{

class DawnMeshCache
{
public:
	explicit DawnMeshCache(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	/// 頂点がない Mesh には nullptr を返す。返した GpuMesh は、そのフレームの終わりまで有効。
	[[nodiscard]] GpuMesh* get(const Mesh& mesh, std::uint64_t frame)
	{
		if (mesh.vertexCount() == 0) { return nullptr; }
		if (frame != m_frame)
		{
			// 前のフレームの描画は提出済みで、破棄した古い版を参照するものはもうない。
			m_retired.clear();
			m_frame = frame;
		}
		Entry& e = m_meshes[&mesh];
		if (!e.gpu) { e.gpu = std::make_unique<GpuMesh>(); }
		e.gpu->lastUsed = frame;
		if (e.gpu->vertexBuffer && e.gpu->revision == mesh.revision())
		{
			e.handedOutFrame = frame;
			return e.gpu.get();
		}
		// 描画は GpuMesh を参照したまま記録し、バッファはフレームの終わりにまとめる。同じフレームですでに渡した版へ WriteBuffer すると、先に記録した描画まで後の頂点で描かれる。1 つの Mesh を updateMesh3D で更新しながら複数回描く場合に起きる。
		// 渡した版は退避し、新しい版を別のバッファへ書く。
		if (e.handedOutFrame == frame && e.gpu->vertexBuffer)
		{
			auto fresh = std::make_unique<GpuMesh>();
			fresh->lastUsed = frame;
			m_retired.push_back(std::move(e.gpu));
			e.gpu = std::move(fresh);
		}
		upload(mesh, *e.gpu);
		e.handedOutFrame = frame;
		return e.gpu.get();
	}

	void evictIdle(std::uint64_t frame)
	{
		for (auto it = m_meshes.begin(); it != m_meshes.end();)
		{
			it = (frame - it->second.gpu->lastUsed > kMeshIdleFrames) ? m_meshes.erase(it) : std::next(it);
		}
	}

private:
	[[nodiscard]] wgpu::Buffer makeBuffer(std::uint64_t bytes, wgpu::BufferUsage usage) const
	{
		wgpu::BufferDescriptor bd{};
		bd.size = (bytes + 3) & ~std::uint64_t(3);
		bd.usage = usage | wgpu::BufferUsage::CopyDst;
		return m_ctx.device.CreateBuffer(&bd);
	}

	void upload(const Mesh& mesh, GpuMesh& g) const
	{
		const std::uint64_t vbytes = static_cast<std::uint64_t>(mesh.vertexCount()) * sizeof(Vertex3D);
		const std::uint64_t ibytes = static_cast<std::uint64_t>(mesh.indexCount()) * sizeof(std::uint32_t);
		if (!g.vertexBuffer || g.vertexBytes != vbytes)
		{
			g.vertexBuffer = makeBuffer(vbytes, wgpu::BufferUsage::Vertex);
			g.vertexBytes = vbytes;
		}
		m_ctx.queue.WriteBuffer(g.vertexBuffer, 0, mesh.vertices().data(), vbytes);
		if (ibytes > 0)
		{
			if (!g.indexBuffer || g.indexBytes != ibytes)
			{
				g.indexBuffer = makeBuffer(ibytes, wgpu::BufferUsage::Index);
				g.indexBytes = ibytes;
			}
			m_ctx.queue.WriteBuffer(g.indexBuffer, 0, mesh.indices().data(), ibytes);
		}
		g.indexCount = static_cast<std::uint32_t>(mesh.indexCount());
		g.vertexCount = static_cast<std::uint32_t>(mesh.vertexCount());
		g.revision = mesh.revision();
	}

	gfx::DawnContext& m_ctx;
	struct Entry
	{
		std::unique_ptr<GpuMesh> gpu;
		std::uint64_t handedOutFrame = ~std::uint64_t(0);
	};

	std::unordered_map<const Mesh*, Entry> m_meshes;
	std::vector<std::unique_ptr<GpuMesh>> m_retired;
	std::uint64_t m_frame = ~std::uint64_t(0);
};

} // namespace mitiru::render::dawn_detail
