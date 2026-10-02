#include <mitiru/ui_rml/RmlRenderInterfaceDx12.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Internal.hpp>

#include <mitiru/gfx/dx12/Dx12FenceWait.hpp>

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mitiru::ui_rml
{

using dx12::RmlPso;
using dx12::RmlRootParam;
using dx12::RmlTargetStack;
using dx12::transition;

namespace
{

constexpr std::uint32_t kSrvCapacity = 4096;
constexpr UINT64 kRingBytesPerSlot = 4ull * 1024 * 1024;

UINT pickSampleCount(ID3D12Device* device)
{
	for (const UINT n : { 8u, 4u, 2u })
	{
		bool ok = true;
		for (const DXGI_FORMAT f : { dx12::kRmlColorFormat, dx12::kRmlStencilFormat })
		{
			D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = {};
			q.Format = f;
			q.SampleCount = n;
			ok = ok && SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q)))
			        && q.NumQualityLevels > 0;
		}
		if (ok) { return n; }
	}
	return 1;
}

D3D12_RESOURCE_DESC textureDesc(int w, int h)
{
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = static_cast<UINT64>(w);
	d.Height = static_cast<UINT>(h);
	d.DepthOrArraySize = 1;
	d.MipLevels = 1;
	d.Format = dx12::kRmlColorFormat;
	d.SampleDesc.Count = 1;
	return d;
}

} // namespace

RmlRenderInterfaceDx12::RmlRenderInterfaceDx12() = default;

RmlRenderInterfaceDx12::~RmlRenderInterfaceDx12()
{
	if (m_fence) { waitFence(m_fenceValue); }
	if (m_fenceEvent) { CloseHandle(m_fenceEvent); }
}

bool RmlRenderInterfaceDx12::initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
{
	if (device == nullptr || queue == nullptr) { m_error = "device or queue is null"; return false; }
	m_device = device;
	m_queue = queue;
	m_samples = pickSampleCount(device);
	if (!m_pipelines.create(device, m_samples)) { m_error = "pipelines: " + m_pipelines.error(); return false; }
	if (!m_srvPool.create(device, kSrvCapacity) || !m_targets.create(device, &m_srvPool, m_samples) ||
	    !m_ring.initialize(device, static_cast<UINT>(kSlots), kRingBytesPerSlot) || !createFrameObjects())
	{
		m_error = "descriptor heaps, upload ring or command list creation failed";
		return false;
	}
	m_ready = true;
	return true;
}

bool RmlRenderInterfaceDx12::createFrameObjects()
{
	for (auto& a : m_allocators)
	{
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.GetAddressOf())))) { return false; }
	}
	if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr,
	                                       IID_PPV_ARGS(m_list.GetAddressOf()))))
	{
		return false;
	}
	m_list->Close();
	m_list->SetName(L"RmlUi frame");
	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf())))) { return false; }
	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	return m_fenceEvent != nullptr;
}

void RmlRenderInterfaceDx12::waitFence(std::uint64_t value)
{
	(void)gfx::waitForFenceOrReport(m_fence.Get(), value, m_fenceEvent, "RmlUi 描画層");
}

std::uint64_t RmlRenderInterfaceDx12::releaseFence() const noexcept
{
	return m_frameOpen ? m_fenceValue + 1 : m_fenceValue;
}

bool RmlRenderInterfaceDx12::openCommandList()
{
	if (FAILED(m_allocators[m_slot]->Reset()) || FAILED(m_list->Reset(m_allocators[m_slot].Get(), nullptr))) { return false; }
	ID3D12DescriptorHeap* heaps[] = { m_srvPool.heap() };
	m_list->SetDescriptorHeaps(1, heaps);
	m_list->SetGraphicsRootSignature(m_pipelines.rootSignature());
	m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	return true;
}

bool RmlRenderInterfaceDx12::beginFrame(ID3D12Resource* target, int width, int height, Rml::Vector2i logical)
{
	if (!m_ready || m_frameOpen || target == nullptr || width <= 0 || height <= 0 || logical.x <= 0 || logical.y <= 0) { return false; }
	m_slot = static_cast<std::size_t>(m_frameSerial % kSlots);
	waitFence(m_slotFence[m_slot]);
	m_slotStaging[m_slot].clear();
	m_srvPool.reclaim(m_fence->GetCompletedValue());
	if (!m_targets.ensureSize(width, height, m_fenceValue)) { m_error = "layer targets allocation failed"; return false; }
	if (!openCommandList()) { m_error = "command list reset failed"; return false; }
	m_ring.beginFrame(static_cast<UINT>(m_slot));
	m_frameOpen = true;
	m_frameTarget = target;
	m_scale = { static_cast<float>(width) / static_cast<float>(logical.x), static_cast<float>(height) / static_cast<float>(logical.y) };
	m_projection = Rml::Matrix4f::ProjectOrtho(0.0f, static_cast<float>(logical.x), static_cast<float>(logical.y), 0.0f, -10000.0f, 10000.0f);
	m_transform = m_projection;
	m_boundPso = nullptr;
	m_boundRtv = {};
	m_clipMaskEnabled = false;
	m_stencilRef = 0;
	recordPendingUploads();
	m_targets.reset();
	(void)m_targets.push();
	setViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
	m_scissor = fullTarget();
	setScissor(m_scissor);
	initBaseLayer();
	return true;
}

// 描画先 (ゲームの絵) を最下層へ写す。以後の UI はその上に重なり、backdrop-filter はゲームの絵も読む。
void RmlRenderInterfaceDx12::initBaseLayer()
{
	Target& primary = m_targets.post(RmlTargetStack::Primary);
	transition(m_list.Get(), m_frameTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	transition(m_list.Get(), primary, D3D12_RESOURCE_STATE_COPY_DEST);
	dx12::copyRegion(m_list.Get(), primary.resource.Get(), m_frameTarget, 0, 0, primary.width, primary.height);
	transition(m_list.Get(), m_frameTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_list->ClearDepthStencilView(m_targets.dsv(), D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	drawFullscreen(RmlPso::LayerReplace, m_targets.layer(0), primary, baseConstants({ 0.0f, 0.0f }));
}

void RmlRenderInterfaceDx12::endFrame()
{
	if (!m_frameOpen) { return; }
	blitLayerToPrimary(0);
	Target& primary = m_targets.post(RmlTargetStack::Primary);
	transition(m_list.Get(), primary, D3D12_RESOURCE_STATE_COPY_SOURCE);
	transition(m_list.Get(), m_frameTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	dx12::copyRegion(m_list.Get(), m_frameTarget, primary.resource.Get(), 0, 0, primary.width, primary.height);
	transition(m_list.Get(), m_frameTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_list->Close();
	ID3D12CommandList* lists[] = { m_list.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	m_queue->Signal(m_fence.Get(), ++m_fenceValue);
	m_slotFence[m_slot] = m_fenceValue;
	++m_frameSerial;
	m_frameOpen = false;
	m_frameTarget = nullptr;
}

// 幾何 ───────────────────────────────────────────────────────────

Rml::CompiledGeometryHandle RmlRenderInterfaceDx12::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	// RmlUi は ReleaseGeometry まで元の配列を動かさないと約束している。写しは持たず、描く時に
	// フレームの upload 領域へ詰める (同じフレームで 2 度目に描く時は詰めた物を使い回す)。
	auto g = std::make_unique<Geometry>();
	g->vertices = vertices;
	g->indices = indices;
	return static_cast<Rml::CompiledGeometryHandle>(m_geometries.add(std::move(g)));
}

void RmlRenderInterfaceDx12::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	(void)m_geometries.take(geometry);
}

bool RmlRenderInterfaceDx12::drawGeometry(Geometry& g)
{
	if (g.indices.empty() || g.vertices.empty()) { return false; }
	if (g.uploadedFrame != m_frameSerial + 1)
	{
		const auto vb = m_ring.upload(g.vertices.data(), g.vertices.size() * sizeof(Rml::Vertex), 16);
		const auto ib = m_ring.upload(g.indices.data(), g.indices.size() * sizeof(int), 16);
		if (!vb.valid() || !ib.valid()) { return false; }
		g.vbv = { vb.gpuAddr, static_cast<UINT>(vb.size), static_cast<UINT>(sizeof(Rml::Vertex)) };
		g.ibv = { ib.gpuAddr, static_cast<UINT>(ib.size), DXGI_FORMAT_R32_UINT };
		g.uploadedFrame = m_frameSerial + 1;
	}
	m_list->IASetVertexBuffers(0, 1, &g.vbv);
	m_list->IASetIndexBuffer(&g.ibv);
	m_list->DrawIndexedInstanced(static_cast<UINT>(g.indices.size()), 1, 0, 0, 0);
	return true;
}

void RmlRenderInterfaceDx12::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	Geometry* g = m_geometries.get(geometry);
	Texture* t = m_textures.get(texture);
	if (!m_frameOpen || g == nullptr || (texture != 0 && t == nullptr)) { return; }
	bindTopLayer();
	if (t != nullptr)
	{
		setPso(clipped(RmlPso::Texture, RmlPso::TextureClip));
		bindTexture(RmlRootParam::Texture, t->srv);
	}
	else
	{
		setPso(clipped(RmlPso::Color, RmlPso::ColorClip));
	}
	pushConstants(baseConstants(translation));
	(void)drawGeometry(*g);
}

// テクスチャ ─────────────────────────────────────────────────────

Rml::TextureHandle RmlRenderInterfaceDx12::addTexture(gfx::GpuResource resource, int w, int h, D3D12_RESOURCE_STATES state)
{
	auto t = std::make_unique<Texture>();
	t->srv = m_srvPool.allocate();
	if (t->srv == dx12::RmlDescriptorPool::kInvalid)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi texture slots exhausted (%u)", kSrvCapacity);
		return {};
	}
	m_device->CreateShaderResourceView(resource.Get(), nullptr, m_srvPool.cpu(t->srv));
	t->resource = std::move(resource);
	t->width = w;
	t->height = h;
	t->state = state;
	return static_cast<Rml::TextureHandle>(m_textures.add(std::move(t)));
}

Rml::TextureHandle RmlRenderInterfaceDx12::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
	if (!m_ready || dimensions.x <= 0 || dimensions.y <= 0 ||
	    source.size() < static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4)
	{
		return {};
	}
	gfx::GpuResource tex;
	if (FAILED(gfx::createGpuResource(m_device, D3D12_HEAP_TYPE_DEFAULT, textureDesc(dimensions.x, dimensions.y),
	                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, tex)))
	{
		return {};
	}
	gfx::GpuResource staging;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	if (!dx12::fillStaging(m_device, tex.Get(), source.data(), dimensions.x, dimensions.y, staging, footprint)) { return {}; }
	const Rml::TextureHandle handle = addTexture(std::move(tex), dimensions.x, dimensions.y, D3D12_RESOURCE_STATE_COPY_DEST);
	Texture* t = m_textures.get(handle);
	if (t == nullptr) { return {}; }
	t->staging = std::move(staging);
	t->footprint = footprint;
	m_pendingUploads.push_back(t);
	if (m_frameOpen) { recordPendingUploads(); }
	return handle;
}

void RmlRenderInterfaceDx12::recordUpload(Texture& t)
{
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = t.resource.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = t.staging.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = t.footprint;
	m_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	transition(m_list.Get(), t.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	t.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	m_slotStaging[m_slot].push_back(std::move(t.staging));
}

void RmlRenderInterfaceDx12::recordPendingUploads()
{
	for (Texture* t : m_pendingUploads) { recordUpload(*t); }
	m_pendingUploads.clear();
}

Rml::TextureHandle RmlRenderInterfaceDx12::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
	std::vector<Rml::byte> file;
	if (!dx12::readWholeFile(source, file)) { return {}; }
	int w = 0, h = 0, n = 0;
	stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &n, 4);
	if (pixels == nullptr)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "image could not be decoded (PNG / JPEG only): %s", source.c_str());
		return {};
	}
	std::vector<Rml::byte> rgba(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
	stbi_image_free(pixels);
	dx12::premultiply(rgba);
	dimensions = { w, h };
	return GenerateTexture(rgba, dimensions);
}

void RmlRenderInterfaceDx12::ReleaseTexture(Rml::TextureHandle texture)
{
	std::unique_ptr<Texture> t = m_textures.take(texture);
	if (!t) { return; }
	std::erase(m_pendingUploads, t.get());
	m_srvPool.release(t->srv, releaseFence());
}

// 切り抜き・変換・クリップマスク ─────────────────────────────────

RmlRenderInterfaceDx12::Rect RmlRenderInterfaceDx12::fullTarget() const noexcept
{
	return Rect::FromPositionSize({ 0, 0 }, { m_targets.width(), m_targets.height() });
}

RmlRenderInterfaceDx12::Rect RmlRenderInterfaceDx12::toPhysical(const Rect& logical) const
{
	if (!logical.Valid()) { return fullTarget(); }
	const auto fx = [&](int v) { return static_cast<float>(v) * m_scale.x; };
	const auto fy = [&](int v) { return static_cast<float>(v) * m_scale.y; };
	const int x0 = std::clamp(static_cast<int>(std::floor(fx(logical.Left()))), 0, m_targets.width());
	const int y0 = std::clamp(static_cast<int>(std::floor(fy(logical.Top()))), 0, m_targets.height());
	const int x1 = std::clamp(static_cast<int>(std::ceil(fx(logical.Right()))), x0, m_targets.width());
	const int y1 = std::clamp(static_cast<int>(std::ceil(fy(logical.Bottom()))), y0, m_targets.height());
	return Rect::FromCorners({ x0, y0 }, { x1, y1 });
}

void RmlRenderInterfaceDx12::EnableScissorRegion(bool enable)
{
	// 有効にする時は直後に SetScissorRegion が来るので、ここでは外す時だけを扱う。
	if (!enable && m_frameOpen)
	{
		m_scissor = fullTarget();
		setScissor(m_scissor);
	}
}

void RmlRenderInterfaceDx12::SetScissorRegion(Rml::Rectanglei region)
{
	if (!m_frameOpen) { return; }
	m_scissor = toPhysical(region);
	setScissor(m_scissor);
}

void RmlRenderInterfaceDx12::SetTransform(const Rml::Matrix4f* transform)
{
	m_transform = transform ? m_projection * (*transform) : m_projection;
}

void RmlRenderInterfaceDx12::EnableClipMask(bool enable)
{
	m_clipMaskEnabled = enable;
}

void RmlRenderInterfaceDx12::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                                              Rml::Vector2f translation)
{
	Geometry* g = m_geometries.get(geometry);
	if (!m_frameOpen || g == nullptr) { return; }
	bindTopLayer();
	std::uint8_t writeRef = 1, testRef = 1;
	RmlPso pso = RmlPso::MaskReplace;
	switch (operation)
	{
	case Rml::ClipMaskOperation::Set:
		m_list->ClearDepthStencilView(m_targets.dsv(), D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		break;
	case Rml::ClipMaskOperation::SetInverse:
		m_list->ClearDepthStencilView(m_targets.dsv(), D3D12_CLEAR_FLAG_STENCIL, 1.0f, 1, 0, nullptr);
		writeRef = 0;
		break;
	case Rml::ClipMaskOperation::Intersect:
		pso = RmlPso::MaskIncrement;
		writeRef = m_stencilRef;
		testRef = static_cast<std::uint8_t>(m_stencilRef + 1);
		break;
	}
	m_list->OMSetStencilRef(writeRef);
	setPso(pso);
	pushConstants(baseConstants(translation));
	(void)drawGeometry(*g);
	m_stencilRef = testRef;
	m_list->OMSetStencilRef(m_stencilRef);
}

// 描画の下回り ───────────────────────────────────────────────────

dx12::RmlPso RmlRenderInterfaceDx12::clipped(RmlPso plain, RmlPso withClip) const noexcept
{
	return m_clipMaskEnabled ? withClip : plain;
}

void RmlRenderInterfaceDx12::bindTarget(Target& t, bool withStencil)
{
	transition(m_list.Get(), t, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if (m_boundRtv.ptr == t.rtv.ptr) { return; }
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_targets.dsv();
	m_list->OMSetRenderTargets(1, &t.rtv, FALSE, withStencil ? &dsv : nullptr);
	m_list->OMSetStencilRef(m_stencilRef);
	m_boundRtv = t.rtv;
}

void RmlRenderInterfaceDx12::bindTopLayer()
{
	bindTarget(m_targets.layer(m_targets.topIndex()), true);
}

void RmlRenderInterfaceDx12::setPso(RmlPso pso)
{
	ID3D12PipelineState* p = m_pipelines.get(pso);
	if (p != m_boundPso)
	{
		m_list->SetPipelineState(p);
		m_boundPso = p;
	}
}

void RmlRenderInterfaceDx12::setScissor(const Rect& r)
{
	const D3D12_RECT rect = { r.Left(), r.Top(), r.Right(), r.Bottom() };
	m_list->RSSetScissorRects(1, &rect);
}

void RmlRenderInterfaceDx12::setViewport(float x, float y, float w, float h)
{
	const D3D12_VIEWPORT vp = { x, y, w, h, 0.0f, 1.0f };
	m_list->RSSetViewports(1, &vp);
}

void RmlRenderInterfaceDx12::bindTexture(RmlRootParam param, std::uint32_t srv)
{
	m_list->SetGraphicsRootDescriptorTable(static_cast<UINT>(param), m_srvPool.gpu(srv));
}

void RmlRenderInterfaceDx12::pushConstants(const dx12::RmlDrawConstants& c)
{
	const auto a = m_ring.upload(&c, sizeof(c), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	if (a.valid()) { m_list->SetGraphicsRootConstantBufferView(static_cast<UINT>(RmlRootParam::Constants), a.gpuAddr); }
}

dx12::RmlDrawConstants RmlRenderInterfaceDx12::baseConstants(Rml::Vector2f translation) const
{
	dx12::RmlDrawConstants c = {};
	std::memcpy(c.transform, m_transform.data(), sizeof(c.transform));
	c.translate[0] = translation.x;
	c.translate[1] = translation.y;
	c.uvScale[0] = c.uvScale[1] = 1.0f;
	c.opacity = 1.0f;
	return c;
}

void RmlRenderInterfaceDx12::drawFullscreen(RmlPso pso, Target& dst, Target& src, const dx12::RmlDrawConstants& c)
{
	transition(m_list.Get(), src, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	bindTarget(dst, dx12::drawsToLayer(pso));
	setPso(pso);
	bindTexture(RmlRootParam::Texture, src.srv);
	pushConstants(c);
	m_list->DrawInstanced(3, 1, 0, 0);
}

} // namespace mitiru::ui_rml
