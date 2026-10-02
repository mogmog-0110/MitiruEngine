#pragma once
// RmlRenderInterfaceDx12 の実装 (src/ui_rml/) だけが使う部品。利用側は include しない。

#ifdef _WIN32

#include <mitiru/ui_rml/RmlRenderInterfaceDx12.hpp>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <cstring>
#include <vector>

namespace mitiru::ui_rml
{

struct RmlRenderInterfaceDx12::Geometry
{
	Rml::Span<const Rml::Vertex> vertices;
	Rml::Span<const int> indices;
	std::uint64_t uploadedFrame = 0;   ///< この値 - 1 のフレームで upload 領域に詰めた
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	D3D12_INDEX_BUFFER_VIEW ibv = {};
};

struct RmlRenderInterfaceDx12::Texture
{
	gfx::GpuResource resource;
	gfx::GpuResource staging;          ///< 転送をまだ積んでいない間だけ持つ
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	std::uint32_t srv = dx12::RmlDescriptorPool::kInvalid;
	int width = 0;
	int height = 0;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

namespace dx12
{

[[nodiscard]] constexpr bool drawsToLayer(RmlPso p) noexcept { return p <= RmlPso::LayerReplaceClip; }

inline void copyRegion(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst, ID3D12Resource* src, int x, int y, int w, int h)
{
	D3D12_TEXTURE_COPY_LOCATION d = {};
	d.pResource = dst;
	d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION s = {};
	s.pResource = src;
	s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	const D3D12_BOX box = { static_cast<UINT>(x), static_cast<UINT>(y), 0,
	                        static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1 };
	cl->CopyTextureRegion(&d, 0, 0, 0, &s, &box);
}

/// 画素を upload ヒープの行揃え (256 バイト) に並べ直して置く。
inline bool fillStaging(ID3D12Device* device, ID3D12Resource* texture, const Rml::byte* rgba, int w, int h,
                        gfx::GpuResource& staging, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
{
	const D3D12_RESOURCE_DESC desc = texture->GetDesc();
	UINT64 total = 0;
	device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
	if (FAILED(gfx::createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_STATE_GENERIC_READ, staging))) { return false; }
	void* mapped = nullptr;
	const D3D12_RANGE noRead = { 0, 0 };
	if (FAILED(staging->Map(0, &noRead, &mapped))) { return false; }
	const std::size_t row = static_cast<std::size_t>(w) * 4;
	for (int y = 0; y < h; ++y)
	{
		std::memcpy(static_cast<std::byte*>(mapped) + footprint.Offset + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch,
		            rgba + static_cast<std::size_t>(y) * row, row);
	}
	staging->Unmap(0, nullptr);
	return true;
}

/// 画像も書体も RmlUi のファイル窓口から読む (窓口は日本語のパスを開けるものに差し替えてある)。
inline bool readWholeFile(const Rml::String& path, std::vector<Rml::byte>& out)
{
	Rml::FileInterface* fi = Rml::GetFileInterface();
	const Rml::FileHandle f = fi ? fi->Open(path) : 0;
	if (!f) { return false; }
	out.resize(fi->Length(f));
	const std::size_t got = out.empty() ? 0 : fi->Read(out.data(), out.size(), f);
	fi->Close(f);
	return got == out.size() && !out.empty();
}

/// RmlUi は乗算済みアルファの画素を期待する。
inline void premultiply(std::vector<Rml::byte>& rgba)
{
	for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
	{
		const unsigned a = rgba[i + 3];
		for (std::size_t c = 0; c < 3; ++c) { rgba[i + c] = static_cast<Rml::byte>(rgba[i + c] * a / 255u); }
	}
}

} // namespace dx12

} // namespace mitiru::ui_rml

#endif // _WIN32
