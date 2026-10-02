#include <mitiru/ui_rml/dx12/RmlDx12Pipelines.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Targets.hpp>

namespace mitiru::ui_rml::dx12
{

namespace
{

D3D12_RESOURCE_DESC texture2D(int w, int h, DXGI_FORMAT format, UINT samples, D3D12_RESOURCE_FLAGS flags)
{
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	d.Width = static_cast<UINT64>(w);
	d.Height = static_cast<UINT>(h);
	d.DepthOrArraySize = 1;
	d.MipLevels = 1;
	d.Format = format;
	d.SampleDesc.Count = samples;
	d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	d.Flags = flags;
	return d;
}

} // namespace

void transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
	if (r == nullptr || from == to) { return; }
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = r;
	b.Transition.StateBefore = from;
	b.Transition.StateAfter = to;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cl->ResourceBarrier(1, &b);
}

void transition(ID3D12GraphicsCommandList* cl, RmlTarget& t, D3D12_RESOURCE_STATES to)
{
	transition(cl, t.resource.Get(), t.state, to);
	t.state = to;
}

bool RmlTargetStack::create(ID3D12Device* device, RmlDescriptorPool* srvPool, UINT samples)
{
	m_device = device;
	m_srvPool = srvPool;
	m_samples = samples;
	m_layers.reserve(kMaxLayers);
	return m_rtvHeap.create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxLayers + PostCount)
	    && m_dsvHeap.create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
}

void RmlTargetStack::releaseAll(std::uint64_t fenceValue)
{
	for (RmlTarget& t : m_post)
	{
		m_srvPool->release(t.srv, fenceValue);
		t = RmlTarget{};
	}
	m_layers.clear();
	m_stencil.Reset();
	m_count = 0;
	m_postOrder = { 0, 1, 2, 3 };
}

bool RmlTargetStack::ensureSize(int width, int height, std::uint64_t fenceValue)
{
	if (width == m_width && height == m_height && m_stencil) { return true; }
	releaseAll(fenceValue);
	m_width = width;
	m_height = height;
	if (!createStencil()) { return false; }
	for (std::size_t i = 0; i < PostCount; ++i)
	{
		if (!createPost(m_post[i], kMaxLayers + static_cast<std::uint32_t>(i))) { return false; }
	}
	return true;
}

int RmlTargetStack::push()
{
	if (m_count >= kMaxLayers) { return -1; }
	if (m_count == m_layers.size())
	{
		m_layers.emplace_back();
		if (!createLayer(m_layers.back(), m_count))
		{
			m_layers.pop_back();
			return -1;
		}
	}
	return static_cast<int>(m_count++);
}

bool RmlTargetStack::createLayer(RmlTarget& t, std::uint32_t rtvIndex)
{
	const auto desc = texture2D(m_width, m_height, kRmlColorFormat, m_samples, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = kRmlColorFormat;
	if (FAILED(gfx::createGpuResource(m_device, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
	                                  &clear, t.resource)))
	{
		return false;
	}
	t.resource->SetName(L"RmlUi layer");
	t.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
	t.rtv = m_rtvHeap.at(rtvIndex);
	t.width = m_width;
	t.height = m_height;
	t.samples = m_samples;
	m_device->CreateRenderTargetView(t.resource.Get(), nullptr, t.rtv);
	return true;
}

bool RmlTargetStack::createPost(RmlTarget& t, std::uint32_t rtvIndex)
{
	const auto desc = texture2D(m_width, m_height, kRmlColorFormat, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = kRmlColorFormat;
	if (FAILED(gfx::createGpuResource(m_device, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	                                  &clear, t.resource)))
	{
		return false;
	}
	t.resource->SetName(L"RmlUi postprocess");
	t.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	t.rtv = m_rtvHeap.at(rtvIndex);
	t.srv = m_srvPool->allocate();
	t.width = m_width;
	t.height = m_height;
	if (t.srv == RmlDescriptorPool::kInvalid) { return false; }
	m_device->CreateRenderTargetView(t.resource.Get(), nullptr, t.rtv);
	m_device->CreateShaderResourceView(t.resource.Get(), nullptr, m_srvPool->cpu(t.srv));
	return true;
}

bool RmlTargetStack::createStencil()
{
	const auto desc = texture2D(m_width, m_height, kRmlStencilFormat, m_samples, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = kRmlStencilFormat;
	clear.DepthStencil = { 1.0f, 0 };
	if (FAILED(gfx::createGpuResource(m_device, D3D12_HEAP_TYPE_DEFAULT, desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
	                                  &clear, m_stencil)))
	{
		return false;
	}
	m_stencil->SetName(L"RmlUi clip mask");
	m_device->CreateDepthStencilView(m_stencil.Get(), nullptr, m_dsvHeap.at(0));
	return true;
}

} // namespace mitiru::ui_rml::dx12
