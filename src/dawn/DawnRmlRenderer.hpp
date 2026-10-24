#pragma once

// Dawn 上で RmlUi の RenderInterface を動かす宣言。DX12 の RmlRenderInterfaceDx12 に相当する。
// 最終色を最下層に置き、その上に UI を重ねる。
// opacity、filter、backdrop-filter、mask-image の各層は、層ごとの絵と作業用の絵 (Primary、Secondary、Tertiary、BlendMask) を使う。
// 角丸の overflow: hidden は、全層で共有する 1 枚のステンシルで切り抜く。
// コマンドは呼ばれた順に積み、endFrame で提出する。
// 実装は DawnRmlRenderer.cpp (リソース、幾何、描画) と DawnRmlEffects.cpp (層、filter、グラデーション) に分ける。

#include <mitiru/ui_rml/RmlRenderInterfaceDawn.hpp>

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core/Vertex.h>

#include <mitiru/gfx/dawn/DawnDevice.hpp>
#include <mitiru/ui_rml/RmlFilterMath.hpp>

#include "DawnContext.hpp"

namespace mitiru::ui_rml::dawn_ui
{

constexpr std::uint64_t kUniformStride = 768;
constexpr std::uint32_t kMaxDraws = 8192;
constexpr int kMaxLayers = 24;

/// ui.slang の Draw と同じ並びの 576 バイト。
struct DrawConstants
{
	float transform[16];
	float colorMatrix[16];
	float stopColors[16][4];
	float stopPositions[16];
	float color[4];
	float weights[4];
	float a[4];       // 平行移動 xy、グラデーションの p zw
	float b[4];       // グラデーションの v xy、ぼかしの 1 画素 zw
	float c[4];       // 読む範囲の左上 xy、右下 zw
	float d[4];       // uv の足し xy、倍率 zw
	float f[4];       // 不透明度 x
	std::int32_t ints[4];   // グラデーションの種類 x、色止めの数 y
};
static_assert(sizeof(DrawConstants) == 576, "ui.slang の Draw と大きさが合っていない");

/// Layer は層の絵へステンシル付きで描き、Post は作業用の絵へステンシルなしで描く。
enum class Pso : std::uint8_t
{
	Color, ColorClip, Texture, TextureClip, Gradient, GradientClip, MaskReplace, MaskIncrement,
	LayerOver, LayerOverClip, LayerReplace, LayerReplaceClip, LayerClear,
	PostCopy, PostCopyOver, PostSample, PostColorMatrix, PostBlendMask, PostBlur, PostDropShadow, PostClear,
	Count
};

struct Geometry
{
	wgpu::Buffer vertexBuffer;
	wgpu::Buffer indexBuffer;
	std::uint32_t indexCount = 0;
};

struct Texture
{
	wgpu::Texture texture;
	wgpu::TextureView view;
	wgpu::BindGroup bindGroup;   ///< 幾何を描くときの束 (マスクの欄は白)
	int width = 0;
	int height = 0;
	int externalSlot = -1;                ///< "view3d:N" の N。外 (3D レンダラ) が持つ画像を、描くたびに引き直す
	void* externalHandle = nullptr;       ///< 今 view と bindGroup が指している WGPUTextureView
};

struct Target
{
	wgpu::Texture texture;
	wgpu::TextureView view;
};

class DawnRmlRenderer final : public RmlRenderInterfaceDawn
{
public:
	explicit DawnRmlRenderer(gfx::DawnDevice& device) : m_device(device), m_ctx(*device.context()) {}

	bool initialize(std::string& error);
	bool beginFrame(int width, int height, Rml::Vector2i logical) override;
	void endFrame() override;
	void setExternalImageSource(UiExternalImageDawnFn fn, void* ctx) override
	{
		m_externalFn = fn;
		m_externalCtx = ctx;
	}

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;
	void EnableClipMask(bool enable) override { m_clipMaskEnabled = enable; }
	void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
	void SetTransform(const Rml::Matrix4f* transform) override;
	Rml::LayerHandle PushLayer() override;
	void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blendMode,
	                     Rml::Span<const Rml::CompiledFilterHandle> filters) override;
	void PopLayer() override;
	Rml::TextureHandle SaveLayerAsTexture() override;
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	                  Rml::TextureHandle texture) override;
	void ReleaseShader(Rml::CompiledShaderHandle shader) override;

private:
	using Rect = Rml::Rectanglei;
	enum Post : std::size_t { Primary = 0, Secondary = 1, Tertiary = 2, BlendMask = 3, PostCount = 4 };

	// リソースの実装は DawnRmlRenderer.cpp。
	[[nodiscard]] bool createPipelines(const wgpu::ShaderModule& module);
	[[nodiscard]] wgpu::Buffer makeBuffer(const void* data, std::size_t bytes, wgpu::BufferUsage usage) const;
	[[nodiscard]] std::shared_ptr<Texture> makeTexture(int w, int h, const std::uint8_t* rgba) const;
	[[nodiscard]] Rml::TextureHandle addExternalTexture(int slot, Rml::Vector2i& dimensions);
	[[nodiscard]] bool refreshExternal(Texture& texture) const;
	[[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& tex, const wgpu::TextureView& mask) const;
	[[nodiscard]] Target makeTarget(int w, int h) const;
	void ensureTargets(int width, int height);
	[[nodiscard]] bool ensureLayer(int index);

	void beginPass(const wgpu::TextureView& view, bool stencil, int clearStencil = -1);
	void endPass();
	void bindTopLayer() { beginPass(m_layers[static_cast<std::size_t>(m_layerCount - 1)].view, true); }
	void setScissor(const Rect& r) { m_curScissor = r; }
	void setViewport(float x, float y, float w, float h) { m_viewport = {x, y, w, h}; }
	void applyState();
	[[nodiscard]] std::uint32_t pushConstants(const DrawConstants& c);
	[[nodiscard]] DrawConstants baseConstants(Rml::Vector2f translation) const;
	void setPipeline(Pso pso);
	void drawGeometry(const Geometry& g, Pso pso, const DrawConstants& c, const wgpu::BindGroup& group);
	void drawFullscreen(Pso pso, const Target& dst, const wgpu::TextureView& src, const DrawConstants& c,
	                    const wgpu::TextureView* mask = nullptr);
	[[nodiscard]] Rect toPhysical(const Rect& logical) const;
	[[nodiscard]] Rect fullTarget() const noexcept { return Rect::FromPositionSize({0, 0}, {m_width, m_height}); }
	[[nodiscard]] Pso clipped(Pso plain, Pso withClip) const noexcept { return m_clipMaskEnabled ? withClip : plain; }
	[[nodiscard]] Target& post(Post p) { return m_post[m_postOrder[p]]; }

	// 層と filter の実装は DawnRmlEffects.cpp。
	void blitLayerToPrimary(int layer);
	void clearRegion(Pso pso, const Rect& region);
	void renderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters);
	void renderOpacity(float value);
	void renderColorMatrix(const RmlFilter& f);
	void renderMaskImage();
	void renderDropShadow(const RmlFilter& f);
	void renderBlur(float sigma, Target& srcDst, Target& temp, const Rect& window);
	void blurDownscale(int passLevel, Target& srcDst, Target& temp, Rect& scissor);
	void blurPass(Target& from, Target& to, Rml::Vector2f texelOffset, const Rect& scissor, float sigma);
	void blurUpscale(int passLevel, Target& temp, Target& dst, const Rect& reduced, const Rect& window);
	[[nodiscard]] float pixelScale() const noexcept { return 0.5f * (m_scale.x + m_scale.y); }
	void swapPrimarySecondary() noexcept { std::swap(m_postOrder[Primary], m_postOrder[Secondary]); }

	template <class T>
	class HandleTable
	{
	public:
		std::uintptr_t add(std::shared_ptr<T> item)
		{
			m_items[++m_next] = std::move(item);
			return m_next;
		}
		[[nodiscard]] std::shared_ptr<T> get(std::uintptr_t h) const
		{
			const auto it = m_items.find(h);
			return it != m_items.end() ? it->second : nullptr;
		}
		void remove(std::uintptr_t h) { m_items.erase(h); }

	private:
		std::unordered_map<std::uintptr_t, std::shared_ptr<T>> m_items;
		std::uintptr_t m_next = 0;
	};

	gfx::DawnDevice& m_device;
	gfx::DawnContext& m_ctx;
	wgpu::Buffer m_uniform;
	wgpu::Sampler m_wrap;
	wgpu::Sampler m_clamp;
	wgpu::BindGroupLayout m_layout;
	std::array<gfx::AsyncRenderPipeline, static_cast<std::size_t>(Pso::Count)> m_pipelines;
	UiExternalImageDawnFn m_externalFn = nullptr;
	void* m_externalCtx = nullptr;
	std::shared_ptr<Texture> m_white;
	HandleTable<Geometry> m_geometries;
	HandleTable<Texture> m_textures;
	HandleTable<RmlFilter> m_filters;
	HandleTable<RmlGradient> m_gradients;

	std::vector<Target> m_layers;     ///< 0 は最終色そのもの
	int m_layerCount = 1;
	int m_layerOverflow = 0;   ///< 層の上限を超えた PushLayer の数。対応する PopLayer では層を外さない
	std::array<Target, PostCount> m_post;
	std::array<std::size_t, PostCount> m_postOrder = {0, 1, 2, 3};
	wgpu::Texture m_stencil;
	wgpu::TextureView m_stencilView;
	int m_targetWidth = 0;
	int m_targetHeight = 0;

	wgpu::CommandEncoder m_encoder;
	wgpu::RenderPassEncoder m_pass;
	wgpu::TextureView m_passView;
	bool m_passOpen = false;
	bool m_passStencil = false;
	wgpu::RenderPipeline m_boundPipeline;
	std::vector<std::uint8_t> m_uniformBytes;
	Rml::Matrix4f m_projection = Rml::Matrix4f::Identity();
	Rml::Matrix4f m_transform = Rml::Matrix4f::Identity();
	Rml::Vector2f m_scale = {1.0f, 1.0f};
	Rect m_scissor = Rect::MakeInvalid();      ///< RmlUi が頼んだ切り抜き (実画素)
	Rect m_curScissor = Rect::MakeInvalid();   ///< いま使う切り抜き。ぼかしの途中は m_scissor と違う
	std::array<float, 4> m_viewport = {0.0f, 0.0f, 0.0f, 0.0f};
	std::uint32_t m_drawCount = 0;
	std::uint8_t m_stencilRef = 0;
	bool m_clipMaskEnabled = false;
	bool m_open = false;
	int m_width = 0;
	int m_height = 0;
};

} // namespace mitiru::ui_rml::dawn_ui
