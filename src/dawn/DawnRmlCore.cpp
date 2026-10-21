#include "DawnRmlRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <RmlUi/Core/Log.h>
#include <stb_image.h>

#include <mitiru/debug/WarnOnce.hpp>

#include "DawnDeviceTarget.hpp"
#include "DawnShaderSources.inc"

namespace mitiru::ui_rml::dawn_ui
{

namespace
{

constexpr wgpu::TextureFormat kStencilFormat = wgpu::TextureFormat::Stencil8;

enum class Blend : std::uint8_t { Over, Replace, NoColor };
enum class Stencil : std::uint8_t { Off, Test, WriteReplace, WriteIncrement };

struct PsoDef
{
	Pso id;
	const char* vs;
	const char* fs;
	bool layer;   // ステンシルつきの層の絵へ描くか
	Blend blend;
	Stencil stencil;
};

constexpr PsoDef kPsoDefs[] = {
	{Pso::Color,            "vsGeometry",   "fsColor",       true,  Blend::Over,    Stencil::Off},
	{Pso::ColorClip,        "vsGeometry",   "fsColor",       true,  Blend::Over,    Stencil::Test},
	{Pso::Texture,          "vsGeometry",   "fsTexture",     true,  Blend::Over,    Stencil::Off},
	{Pso::TextureClip,      "vsGeometry",   "fsTexture",     true,  Blend::Over,    Stencil::Test},
	{Pso::Gradient,         "vsGeometry",   "fsGradient",    true,  Blend::Over,    Stencil::Off},
	{Pso::GradientClip,     "vsGeometry",   "fsGradient",    true,  Blend::Over,    Stencil::Test},
	{Pso::MaskReplace,      "vsGeometry",   "fsColor",       true,  Blend::NoColor, Stencil::WriteReplace},
	{Pso::MaskIncrement,    "vsGeometry",   "fsColor",       true,  Blend::NoColor, Stencil::WriteIncrement},
	{Pso::LayerOver,        "vsFullscreen", "fsCopy",        true,  Blend::Over,    Stencil::Off},
	{Pso::LayerOverClip,    "vsFullscreen", "fsCopy",        true,  Blend::Over,    Stencil::Test},
	{Pso::LayerReplace,     "vsFullscreen", "fsCopy",        true,  Blend::Replace, Stencil::Off},
	{Pso::LayerReplaceClip, "vsFullscreen", "fsCopy",        true,  Blend::Replace, Stencil::Test},
	{Pso::LayerClear,       "vsFullscreen", "fsClear",       true,  Blend::Replace, Stencil::Off},
	{Pso::PostCopy,         "vsFullscreen", "fsCopy",        false, Blend::Replace, Stencil::Off},
	{Pso::PostCopyOver,     "vsFullscreen", "fsCopy",        false, Blend::Over,    Stencil::Off},
	{Pso::PostSample,       "vsFullscreen", "fsSample",      false, Blend::Replace, Stencil::Off},
	{Pso::PostColorMatrix,  "vsFullscreen", "fsColorMatrix", false, Blend::Replace, Stencil::Off},
	{Pso::PostBlendMask,    "vsFullscreen", "fsBlendMask",   false, Blend::Replace, Stencil::Off},
	{Pso::PostBlur,         "vsFullscreen", "fsBlur",        false, Blend::Replace, Stencil::Off},
	{Pso::PostDropShadow,   "vsFullscreen", "fsDropShadow",  false, Blend::Replace, Stencil::Off},
	{Pso::PostClear,        "vsFullscreen", "fsClear",       false, Blend::Replace, Stencil::Off},
};
static_assert(std::size(kPsoDefs) == static_cast<std::size_t>(Pso::Count));

bool readFile(const std::string& utf8, std::vector<Rml::byte>& out)
{
	std::ifstream f(std::filesystem::path(std::u8string(utf8.begin(), utf8.end())), std::ios::binary);
	if (!f) { return false; }
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return !out.empty();
}

// RmlUi は乗算済みアルファの画素を期待する
void premultiply(std::vector<Rml::byte>& rgba)
{
	for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
	{
		const unsigned a = rgba[i + 3];
		for (std::size_t c = 0; c < 3; ++c) { rgba[i + c] = static_cast<Rml::byte>(rgba[i + c] * a / 255u); }
	}
}

wgpu::StencilFaceState stencilFace(Stencil s)
{
	wgpu::StencilFaceState f{};
	f.compare = (s == Stencil::Test) ? wgpu::CompareFunction::Equal : wgpu::CompareFunction::Always;
	f.failOp = wgpu::StencilOperation::Keep;
	f.depthFailOp = wgpu::StencilOperation::Keep;
	f.passOp = (s == Stencil::WriteReplace)  ? wgpu::StencilOperation::Replace
	         : (s == Stencil::WriteIncrement) ? wgpu::StencilOperation::IncrementClamp
	                                          : wgpu::StencilOperation::Keep;
	return f;
}

} // namespace

bool DawnRmlRenderer::createPipelines(const wgpu::ShaderModule& module)
{
	const auto& dev = m_ctx.device;
	wgpu::PipelineLayoutDescriptor pl{};
	pl.bindGroupLayoutCount = 1;
	pl.bindGroupLayouts = &m_layout;
	wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&pl);

	static const wgpu::VertexAttribute attrs[3] = {
		{nullptr, wgpu::VertexFormat::Float32x2, offsetof(Rml::Vertex, position), 0},
		{nullptr, wgpu::VertexFormat::Unorm8x4, offsetof(Rml::Vertex, colour), 1},
		{nullptr, wgpu::VertexFormat::Float32x2, offsetof(Rml::Vertex, tex_coord), 2},
	};
	wgpu::VertexBufferLayout vb{};
	vb.arrayStride = sizeof(Rml::Vertex);
	vb.attributeCount = 3;
	vb.attributes = attrs;

	// 乗算済みアルファの over 合成
	wgpu::BlendState over{};
	over.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
	over.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};

	for (const PsoDef& def : kPsoDefs)
	{
		wgpu::ColorTargetState target{};
		target.format = m_device.target().format;
		if (def.blend == Blend::Over) { target.blend = &over; }
		if (def.blend == Blend::NoColor) { target.writeMask = wgpu::ColorWriteMask::None; }
		wgpu::FragmentState fs{};
		fs.module = module;
		fs.entryPoint = def.fs;
		fs.targetCount = 1;
		fs.targets = &target;
		wgpu::DepthStencilState ds{};
		ds.format = kStencilFormat;
		ds.stencilFront = ds.stencilBack = stencilFace(def.stencil);
		ds.stencilReadMask = 0xFF;
		ds.stencilWriteMask = (def.stencil == Stencil::WriteReplace || def.stencil == Stencil::WriteIncrement) ? 0xFF : 0;
		wgpu::RenderPipelineDescriptor rp{};
		rp.layout = layout;
		rp.vertex.module = module;
		rp.vertex.entryPoint = def.vs;
		if (def.vs[2] == 'G') { rp.vertex.bufferCount = 1; rp.vertex.buffers = &vb; }   // vsGeometry だけが頂点バッファを使う
		rp.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
		rp.primitive.cullMode = wgpu::CullMode::None;
		if (def.layer) { rp.depthStencil = &ds; }
		rp.fragment = &fs;
		wgpu::RenderPipeline p = dev.CreateRenderPipeline(&rp);
		if (!p) { return false; }
		m_pipelines[static_cast<std::size_t>(def.id)] = std::move(p);
	}
	return true;
}

bool DawnRmlRenderer::initialize(std::string& error)
{
	const auto& dev = m_ctx.device;
	wgpu::BufferDescriptor ub{};
	ub.size = kUniformStride * kMaxDraws;
	ub.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_uniform = dev.CreateBuffer(&ub);

	wgpu::SamplerDescriptor sd{};
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::Repeat;
	sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
	sd.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
	m_wrap = dev.CreateSampler(&sd);
	sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
	m_clamp = dev.CreateSampler(&sd);

	wgpu::BindGroupLayoutEntry le[5]{};
	le[0].binding = 0;
	le[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
	le[0].buffer.type = wgpu::BufferBindingType::Uniform;
	le[0].buffer.hasDynamicOffset = true;
	le[0].buffer.minBindingSize = sizeof(DrawConstants);
	for (std::uint32_t i = 1; i <= 2; ++i)
	{
		le[i].binding = i;
		le[i].visibility = wgpu::ShaderStage::Fragment;
		le[i].texture.sampleType = wgpu::TextureSampleType::Float;
	}
	for (std::uint32_t i = 3; i <= 4; ++i)
	{
		le[i].binding = i;
		le[i].visibility = wgpu::ShaderStage::Fragment;
		le[i].sampler.type = wgpu::SamplerBindingType::Filtering;
	}
	wgpu::BindGroupLayoutDescriptor bgl{};
	bgl.entryCount = 5;
	bgl.entries = le;
	m_layout = dev.CreateBindGroupLayout(&bgl);

	const std::uint8_t white[4] = {255, 255, 255, 255};
	m_white = makeTexture(1, 1, white);
	wgpu::ShaderModule module = m_ctx.makeShader(dawnShaderSource_ui(), "mitiru.ui");
	if (!module || !m_white || !createPipelines(module))
	{
		error = "UI のパイプラインを作れない";
		return false;
	}
	// 白の束では、自分の画像をマスクにも設定するため、白で設定し直す
	m_white->bindGroup = makeGroup(m_white->view, m_white->view);
	return true;
}

wgpu::Buffer DawnRmlRenderer::makeBuffer(const void* data, std::size_t bytes, wgpu::BufferUsage usage) const
{
	const std::size_t padded = (bytes + 3) & ~std::size_t(3);
	wgpu::BufferDescriptor bd{};
	bd.size = padded;
	bd.usage = usage | wgpu::BufferUsage::CopyDst;
	wgpu::Buffer buffer = m_ctx.device.CreateBuffer(&bd);
	std::vector<std::uint8_t> tmp(padded, 0);
	std::memcpy(tmp.data(), data, bytes);
	m_ctx.queue.WriteBuffer(buffer, 0, tmp.data(), padded);
	return buffer;
}

wgpu::BindGroup DawnRmlRenderer::makeGroup(const wgpu::TextureView& tex, const wgpu::TextureView& mask) const
{
	wgpu::BindGroupEntry be[5]{};
	be[0].binding = 0;
	be[0].buffer = m_uniform;
	be[0].size = sizeof(DrawConstants);
	be[1].binding = 1;
	be[1].textureView = tex;
	be[2].binding = 2;
	be[2].textureView = mask;
	be[3].binding = 3;
	be[3].sampler = m_wrap;
	be[4].binding = 4;
	be[4].sampler = m_clamp;
	wgpu::BindGroupDescriptor bg{};
	bg.layout = m_layout;
	bg.entryCount = 5;
	bg.entries = be;
	return m_ctx.device.CreateBindGroup(&bg);
}

std::shared_ptr<Texture> DawnRmlRenderer::makeTexture(int w, int h, const std::uint8_t* rgba) const
{
	auto t = std::make_shared<Texture>();
	wgpu::TextureDescriptor td{};
	td.size = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
	td.format = wgpu::TextureFormat::RGBA8Unorm;
	td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	t->texture = m_ctx.device.CreateTexture(&td);
	if (rgba != nullptr)
	{
		wgpu::TexelCopyTextureInfo dst{};
		dst.texture = t->texture;
		wgpu::TexelCopyBufferLayout layout{};
		layout.bytesPerRow = static_cast<std::uint32_t>(w) * 4;
		layout.rowsPerImage = static_cast<std::uint32_t>(h);
		const wgpu::Extent3D extent{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
		m_ctx.queue.WriteTexture(&dst, rgba, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, &layout, &extent);
	}
	t->view = t->texture.CreateView();
	t->width = w;
	t->height = h;
	t->bindGroup = makeGroup(t->view, m_white ? m_white->view : t->view);
	return t;
}

Target DawnRmlRenderer::makeTarget(int w, int h) const
{
	Target t;
	wgpu::TextureDescriptor td{};
	td.size = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
	td.format = m_device.target().format;
	td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
	           wgpu::TextureUsage::CopyDst;
	t.texture = m_ctx.device.CreateTexture(&td);
	t.view = t.texture.CreateView();
	return t;
}

// 大きさが変わったときだけ、作業用の画像、層、ステンシルを作り直す。0 以外の層は使うときに作る
void DawnRmlRenderer::ensureTargets(int width, int height)
{
	if (width == m_targetWidth && height == m_targetHeight && m_stencil) { return; }
	m_targetWidth = width;
	m_targetHeight = height;
	m_layers.assign(1, Target{});
	for (Target& t : m_post) { t = makeTarget(width, height); }
	wgpu::TextureDescriptor td{};
	td.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
	td.format = kStencilFormat;
	td.usage = wgpu::TextureUsage::RenderAttachment;
	m_stencil = m_ctx.device.CreateTexture(&td);
	m_stencilView = m_stencil.CreateView();
}

bool DawnRmlRenderer::ensureLayer(int index)
{
	while (static_cast<int>(m_layers.size()) <= index) { m_layers.push_back(makeTarget(m_targetWidth, m_targetHeight)); }
	return true;
}

bool DawnRmlRenderer::beginFrame(int width, int height, Rml::Vector2i logical)
{
	if (m_open || width <= 0 || height <= 0 || logical.x <= 0 || logical.y <= 0 || m_ctx.lost) { return false; }
	m_open = true;
	m_width = width;
	m_height = height;
	ensureTargets(width, height);
	m_layers[0].texture = m_device.target().texture;
	m_layers[0].view = m_device.target().view;
	m_layerCount = 1;
	m_layerOverflow = 0;
	m_postOrder = {0, 1, 2, 3};
	m_scale = {static_cast<float>(width) / static_cast<float>(logical.x), static_cast<float>(height) / static_cast<float>(logical.y)};
	m_projection = Rml::Matrix4f::ProjectOrtho(0.0f, static_cast<float>(logical.x), static_cast<float>(logical.y), 0.0f, -10000.0f, 10000.0f);
	m_transform = m_projection;
	m_scissor = fullTarget();
	m_curScissor = m_scissor;
	m_viewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
	m_stencilRef = 0;
	m_clipMaskEnabled = false;
	m_drawCount = 0;
	m_uniformBytes.clear();
	m_encoder = m_ctx.device.CreateCommandEncoder();
	m_passOpen = false;
	m_boundPipeline = {};
	return true;
}

void DawnRmlRenderer::endFrame()
{
	if (!m_open) { return; }
	m_open = false;
	endPass();
	if (m_drawCount > 0)
	{
		m_ctx.queue.WriteBuffer(m_uniform, 0, m_uniformBytes.data(), m_uniformBytes.size());
		wgpu::CommandBuffer commands = m_encoder.Finish();
		m_ctx.queue.Submit(1, &commands);
	}
	m_encoder = {};
}

// ── 描画の下回り ──

void DawnRmlRenderer::endPass()
{
	if (!m_passOpen) { return; }
	m_pass.End();
	m_pass = {};
	m_passOpen = false;
	m_boundPipeline = {};
}

// view へ描く。clearStencil が 0 以上なら、ステンシルを消して新しい描画を始める
void DawnRmlRenderer::beginPass(const wgpu::TextureView& view, bool stencil, int clearStencil)
{
	if (m_passOpen && clearStencil < 0 && m_passView.Get() == view.Get() && m_passStencil == stencil) { return; }
	endPass();
	wgpu::RenderPassColorAttachment color{};
	color.view = view;
	color.loadOp = wgpu::LoadOp::Load;
	color.storeOp = wgpu::StoreOp::Store;
	wgpu::RenderPassDepthStencilAttachment ds{};
	ds.view = m_stencilView;
	ds.stencilLoadOp = clearStencil >= 0 ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
	ds.stencilStoreOp = wgpu::StoreOp::Store;
	ds.stencilClearValue = static_cast<std::uint32_t>(std::max(clearStencil, 0));
	wgpu::RenderPassDescriptor rp{};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &color;
	if (stencil) { rp.depthStencilAttachment = &ds; }
	m_pass = m_encoder.BeginRenderPass(&rp);
	m_passOpen = true;
	m_passView = view;
	m_passStencil = stencil;
	if (stencil) { m_pass.SetStencilReference(m_stencilRef); }
}

void DawnRmlRenderer::applyState()
{
	const Rect r = m_curScissor.Valid() ? m_curScissor : fullTarget();
	const int x0 = std::clamp(r.Left(), 0, m_width);
	const int y0 = std::clamp(r.Top(), 0, m_height);
	const int x1 = std::clamp(r.Right(), x0, m_width);
	const int y1 = std::clamp(r.Bottom(), y0, m_height);
	m_pass.SetViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3], 0.0f, 1.0f);
	m_pass.SetScissorRect(static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0), static_cast<std::uint32_t>(x1 - x0),
	                      static_cast<std::uint32_t>(y1 - y0));
}

std::uint32_t DawnRmlRenderer::pushConstants(const DrawConstants& c)
{
	const auto offset = static_cast<std::uint32_t>(m_uniformBytes.size());
	m_uniformBytes.resize(m_uniformBytes.size() + kUniformStride, 0);
	std::memcpy(m_uniformBytes.data() + offset, &c, sizeof(c));
	++m_drawCount;
	return offset;
}

DrawConstants DawnRmlRenderer::baseConstants(Rml::Vector2f translation) const
{
	DrawConstants c{};
	std::memcpy(c.transform, m_transform.data(), sizeof(c.transform));
	c.a[0] = translation.x;
	c.a[1] = translation.y;
	c.d[2] = c.d[3] = 1.0f;
	c.f[0] = 1.0f;
	return c;
}

void DawnRmlRenderer::setPipeline(Pso pso)
{
	const wgpu::RenderPipeline& p = m_pipelines[static_cast<std::size_t>(pso)];
	if (p.Get() != m_boundPipeline.Get())
	{
		m_pass.SetPipeline(p);
		m_boundPipeline = p;
	}
}

void DawnRmlRenderer::drawGeometry(const Geometry& g, Pso pso, const DrawConstants& c, const wgpu::BindGroup& group)
{
	if (m_drawCount >= kMaxDraws)
	{
		debug::verboseOnce("rmlui.dawn.drawlimit", "UI の描画がこのフレームの上限を超えたので、残りは描きません。");
		return;
	}
	setPipeline(pso);
	applyState();
	const std::uint32_t offset = pushConstants(c);
	m_pass.SetBindGroup(0, group, 1, &offset);
	m_pass.SetVertexBuffer(0, g.vertexBuffer);
	m_pass.SetIndexBuffer(g.indexBuffer, wgpu::IndexFormat::Uint32);
	m_pass.DrawIndexed(g.indexCount);
}

void DawnRmlRenderer::drawFullscreen(Pso pso, const Target& dst, const wgpu::TextureView& src, const DrawConstants& c,
                                     const wgpu::TextureView* mask)
{
	if (m_drawCount >= kMaxDraws) { return; }
	const bool layer = pso <= Pso::LayerClear;
	beginPass(dst.view, layer);
	setPipeline(pso);
	applyState();
	const std::uint32_t offset = pushConstants(c);
	const wgpu::BindGroup group = makeGroup(src, mask != nullptr ? *mask : m_white->view);
	m_pass.SetBindGroup(0, group, 1, &offset);
	m_pass.Draw(3);
}

// ── 幾何と画像 ──

Rml::CompiledGeometryHandle DawnRmlRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	if (vertices.empty() || indices.empty()) { return {}; }
	auto g = std::make_shared<Geometry>();
	g->indexCount = static_cast<std::uint32_t>(indices.size());
	g->vertexBuffer = makeBuffer(vertices.data(), vertices.size() * sizeof(Rml::Vertex), wgpu::BufferUsage::Vertex);
	g->indexBuffer = makeBuffer(indices.data(), indices.size() * sizeof(int), wgpu::BufferUsage::Index);
	return static_cast<Rml::CompiledGeometryHandle>(m_geometries.add(std::move(g)));
}

void DawnRmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	m_geometries.remove(static_cast<std::uintptr_t>(geometry));
}

void DawnRmlRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	if (!m_open) { return; }
	const auto g = m_geometries.get(static_cast<std::uintptr_t>(geometry));
	if (!g) { return; }
	std::shared_ptr<Texture> tex;
	if (texture != 0)
	{
		tex = m_textures.get(static_cast<std::uintptr_t>(texture));
		if (!tex || (tex->externalSlot >= 0 && !refreshExternal(*tex))) { return; }
	}
	bindTopLayer();
	const Pso pso = tex ? clipped(Pso::Texture, Pso::TextureClip) : clipped(Pso::Color, Pso::ColorClip);
	drawGeometry(*g, pso, baseConstants(translation), tex ? tex->bindGroup : m_white->bindGroup);
}

Rml::TextureHandle DawnRmlRenderer::addExternalTexture(int slot, Rml::Vector2i& dimensions)
{
	const UiExternalImageDawn img = m_externalFn != nullptr ? m_externalFn(m_externalCtx, slot) : UiExternalImageDawn{};
	auto t = std::make_shared<Texture>();
	t->externalSlot = slot;
	t->width = std::max(img.width, 1);
	t->height = std::max(img.height, 1);
	dimensions = {t->width, t->height};
	return static_cast<Rml::TextureHandle>(m_textures.add(std::move(t)));
}

// 副ビューは作り直すと画像が変わるため、描くたびに取得し直す。画像が変わったときだけ束を作り直す。無ければ描かない
bool DawnRmlRenderer::refreshExternal(Texture& texture) const
{
	const UiExternalImageDawn img = m_externalFn != nullptr ? m_externalFn(m_externalCtx, texture.externalSlot) : UiExternalImageDawn{};
	if (img.textureView == nullptr) { return false; }
	if (img.textureView == texture.externalHandle) { return true; }
	texture.view = wgpu::TextureView(static_cast<WGPUTextureView>(img.textureView));
	texture.bindGroup = makeGroup(texture.view, m_white ? m_white->view : texture.view);
	texture.externalHandle = img.textureView;
	return true;
}

Rml::TextureHandle DawnRmlRenderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
	if (source.rfind(kView3DImageScheme, 0) == 0)
	{
		return addExternalTexture(std::atoi(source.c_str() + kView3DImageScheme.size()), dimensions);
	}
	std::vector<Rml::byte> file;
	if (!readFile(source, file)) { return {}; }
	int w = 0;
	int h = 0;
	int n = 0;
	stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &n, 4);
	if (pixels == nullptr)
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "image could not be decoded (PNG / JPEG only): %s", source.c_str());
		return {};
	}
	std::vector<Rml::byte> rgba(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
	stbi_image_free(pixels);
	premultiply(rgba);
	dimensions = {w, h};
	return GenerateTexture(rgba, dimensions);
}

Rml::TextureHandle DawnRmlRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
	if (dimensions.x <= 0 || dimensions.y <= 0 ||
	    source.size() < static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4)
	{
		return {};
	}
	auto t = makeTexture(dimensions.x, dimensions.y, source.data());
	return t ? static_cast<Rml::TextureHandle>(m_textures.add(std::move(t))) : Rml::TextureHandle{};
}

void DawnRmlRenderer::ReleaseTexture(Rml::TextureHandle texture)
{
	m_textures.remove(static_cast<std::uintptr_t>(texture));
}

// ── 切り抜き・変換・クリップマスク ──

DawnRmlRenderer::Rect DawnRmlRenderer::toPhysical(const Rect& logical) const
{
	if (!logical.Valid()) { return fullTarget(); }
	const int x0 = std::clamp(static_cast<int>(std::floor(static_cast<float>(logical.Left()) * m_scale.x)), 0, m_width);
	const int y0 = std::clamp(static_cast<int>(std::floor(static_cast<float>(logical.Top()) * m_scale.y)), 0, m_height);
	const int x1 = std::clamp(static_cast<int>(std::ceil(static_cast<float>(logical.Right()) * m_scale.x)), x0, m_width);
	const int y1 = std::clamp(static_cast<int>(std::ceil(static_cast<float>(logical.Bottom()) * m_scale.y)), y0, m_height);
	return Rect::FromCorners({x0, y0}, {x1, y1});
}

void DawnRmlRenderer::EnableScissorRegion(bool enable)
{
	// 有効にした直後は SetScissorRegion が来るため、ここでは無効にするときだけ扱う
	if (!enable && m_open)
	{
		m_scissor = fullTarget();
		setScissor(m_scissor);
	}
}

void DawnRmlRenderer::SetScissorRegion(Rml::Rectanglei region)
{
	if (!m_open) { return; }
	m_scissor = toPhysical(region);
	setScissor(m_scissor);
}

void DawnRmlRenderer::SetTransform(const Rml::Matrix4f* transform)
{
	m_transform = transform != nullptr ? m_projection * (*transform) : m_projection;
}

// 切り抜きは 1 枚のステンシルを全層で共有する。Set は消して 1 を書き、SetInverse は 1 で埋めて 0 を書く。Intersect は図形の内側だけ 1 を足し、重ねた数と一致する所だけ残す
void DawnRmlRenderer::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                                       Rml::Vector2f translation)
{
	const auto g = m_geometries.get(static_cast<std::uintptr_t>(geometry));
	if (!m_open || !g) { return; }
	std::uint8_t writeRef = 1;
	std::uint8_t testRef = 1;
	Pso pso = Pso::MaskReplace;
	const wgpu::TextureView& top = m_layers[static_cast<std::size_t>(m_layerCount - 1)].view;
	switch (operation)
	{
	case Rml::ClipMaskOperation::Set:
		beginPass(top, true, 0);
		break;
	case Rml::ClipMaskOperation::SetInverse:
		beginPass(top, true, 1);
		writeRef = 0;
		break;
	case Rml::ClipMaskOperation::Intersect:
		bindTopLayer();
		pso = Pso::MaskIncrement;
		writeRef = m_stencilRef;
		testRef = static_cast<std::uint8_t>(m_stencilRef + 1);
		break;
	}
	m_pass.SetStencilReference(writeRef);
	drawGeometry(*g, pso, baseConstants(translation), m_white->bindGroup);
	m_stencilRef = testRef;
	m_pass.SetStencilReference(m_stencilRef);
}

} // namespace mitiru::ui_rml::dawn_ui

namespace mitiru::ui_rml
{

std::unique_ptr<RmlRenderInterfaceDawn> RmlRenderInterfaceDawn::create(gfx::DawnDevice& device, std::string& error)
{
	auto renderer = std::make_unique<dawn_ui::DawnRmlRenderer>(device);
	if (!renderer->initialize(error)) { return nullptr; }
	return renderer;
}

} // namespace mitiru::ui_rml
