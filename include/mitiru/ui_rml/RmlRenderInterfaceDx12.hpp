#pragma once
// RmlUi の RenderInterface をエンジンの DX12 デバイスの上に実装したもの。
// ゲームを描き終えた描画先 (バックバッファ / headless の offscreen RT) の上に、同じフレームの中で
// UI を重ねる。描画先の中身を最下層に写してから UI を描くので、backdrop-filter がゲーム画面まで
// ぼかせ、最後は描画先へ画素をそのまま書き戻す (UI の無い所のゲームの画素は 1 も動かない)。
//
// 文脈 (Rml::Context) の座標は論理解像度、層は描画先の実画素で持つ。投影は論理座標を NDC へ写すので
// そのまま実画素に広がり、切り抜き (scissor) と画素数を単位とする filter の値だけを実画素へ直す。

#ifdef _WIN32

#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#include <mitiru/render/dx12/Dx12UploadRing.hpp>
#include <mitiru/ui_rml/RmlFilterMath.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Descriptors.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Pipelines.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Shaders.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Targets.hpp>

#include <RmlUi/Core/RenderInterface.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui_rml
{

/// RmlUi の handle (整数) から中身を引く表。handle は添字 + 1 (0 は RmlUi が「無し」に使う)。
template <class T>
class RmlHandleTable
{
public:
	std::uintptr_t add(std::unique_ptr<T> item)
	{
		if (!m_free.empty())
		{
			const std::size_t i = m_free.back();
			m_free.pop_back();
			m_items[i] = std::move(item);
			return i + 1;
		}
		m_items.push_back(std::move(item));
		return m_items.size();
	}
	[[nodiscard]] T* get(std::uintptr_t handle) const noexcept
	{
		return (handle == 0 || handle > m_items.size()) ? nullptr : m_items[handle - 1].get();
	}
	std::unique_ptr<T> take(std::uintptr_t handle)
	{
		if (get(handle) == nullptr) { return nullptr; }
		m_free.push_back(handle - 1);
		return std::move(m_items[handle - 1]);
	}
	[[nodiscard]] std::size_t liveCount() const noexcept { return m_items.size() - m_free.size(); }

private:
	std::vector<std::unique_ptr<T>> m_items;
	std::vector<std::size_t> m_free;
};

class RmlRenderInterfaceDx12 final : public Rml::RenderInterface
{
public:
	RmlRenderInterfaceDx12();
	~RmlRenderInterfaceDx12() override;
	RmlRenderInterfaceDx12(const RmlRenderInterfaceDx12&) = delete;
	RmlRenderInterfaceDx12& operator=(const RmlRenderInterfaceDx12&) = delete;

	/// @return 失敗したら false (理由は error())。以後の呼び出しは何もしない
	bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

	/// @param target  ゲームを描き終えた描画先。RENDER_TARGET 状態で渡し、endFrame の後も同じ状態に戻る
	/// @param width   描画先のうち画面に見える幅 (バッファは窓より大きいことがある)
	/// @param logical 文脈 (Rml::Context) の大きさ
	bool beginFrame(ID3D12Resource* target, int width, int height, Rml::Vector2i logical);
	void endFrame();

	[[nodiscard]] bool ready() const noexcept { return m_ready; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] UINT layerSamples() const noexcept { return m_samples; }
	[[nodiscard]] std::size_t liveTextureCount() const noexcept { return m_textures.liveCount(); }
	[[nodiscard]] std::size_t liveGeometryCount() const noexcept { return m_geometries.liveCount(); }

	// RmlUi から呼ばれる口
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;
	void EnableClipMask(bool enable) override;
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
	struct Geometry;
	struct Texture;
	using Rect = Rml::Rectanglei;
	using Target = dx12::RmlTarget;

	// フレームと資源 (RmlRenderInterfaceDx12.cpp)
	bool createFrameObjects();
	bool openCommandList();
	void initBaseLayer();
	void waitFence(std::uint64_t value);
	[[nodiscard]] std::uint64_t releaseFence() const noexcept;
	void recordPendingUploads();
	void recordUpload(Texture& t);
	Rml::TextureHandle addTexture(gfx::GpuResource resource, int w, int h, D3D12_RESOURCE_STATES state);

	// 描画の下回り
	void bindTarget(Target& t, bool withStencil);
	void bindTopLayer();
	void setPso(dx12::RmlPso pso);
	void setScissor(const Rect& r);
	void setViewport(float x, float y, float w, float h);
	void bindTexture(dx12::RmlRootParam param, std::uint32_t srv);
	void pushConstants(const dx12::RmlDrawConstants& c);
	[[nodiscard]] dx12::RmlDrawConstants baseConstants(Rml::Vector2f translation) const;
	bool drawGeometry(Geometry& g);
	void drawFullscreen(dx12::RmlPso pso, Target& dst, Target& src, const dx12::RmlDrawConstants& c);
	[[nodiscard]] Rect toPhysical(const Rect& logical) const;
	[[nodiscard]] Rect fullTarget() const noexcept;
	[[nodiscard]] dx12::RmlPso clipped(dx12::RmlPso plain, dx12::RmlPso withClip) const noexcept;

	// 層と filter (RmlRenderInterfaceDx12_Effects.cpp)
	void blitLayerToPrimary(int layer);
	void renderFilters(Rml::Span<const Rml::CompiledFilterHandle> filters);
	void renderOpacity(float value);
	void renderColorMatrix(const RmlFilter& f);
	void renderMaskImage();
	void renderDropShadow(const RmlFilter& f);
	void renderBlur(float sigma, Target& srcDst, Target& temp, const Rect& window);
	void blurDownscale(int passLevel, Target& srcDst, Target& temp, Rect& scissor);
	void blurPass(Target& from, Target& to, Rml::Vector2f texelOffset, const Rect& scissor, float sigma);
	void blurUpscale(int passLevel, Target& temp, Target& dst, const Rect& reduced, const Rect& window);
	[[nodiscard]] float pixelScale() const noexcept;

	ID3D12Device* m_device = nullptr;
	ID3D12CommandQueue* m_queue = nullptr;
	dx12::RmlDx12Pipelines m_pipelines;
	dx12::RmlDescriptorPool m_srvPool;
	dx12::RmlTargetStack m_targets;
	render::dx12::Dx12UploadRing m_ring;

	static constexpr std::size_t kSlots = 3;
	std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kSlots> m_allocators;
	std::array<std::uint64_t, kSlots> m_slotFence = {};
	std::array<std::vector<gfx::GpuResource>, kSlots> m_slotStaging;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_list;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent = nullptr;
	std::uint64_t m_fenceValue = 0;
	std::uint64_t m_frameSerial = 0;
	std::size_t m_slot = 0;

	RmlHandleTable<Geometry> m_geometries;
	RmlHandleTable<Texture> m_textures;
	RmlHandleTable<RmlFilter> m_filters;
	RmlHandleTable<RmlGradient> m_gradients;
	std::vector<Texture*> m_pendingUploads;

	// フレーム中の状態
	ID3D12Resource* m_frameTarget = nullptr;
	Rml::Vector2f m_scale = { 1.0f, 1.0f };
	Rml::Matrix4f m_projection = Rml::Matrix4f::Identity();
	Rml::Matrix4f m_transform = Rml::Matrix4f::Identity();
	Rect m_scissor = Rect::MakeInvalid();
	ID3D12PipelineState* m_boundPso = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE m_boundRtv = {};
	bool m_clipMaskEnabled = false;
	std::uint8_t m_stencilRef = 0;
	bool m_frameOpen = false;
	bool m_ready = false;
	UINT m_samples = 1;
	std::string m_error;
};

} // namespace mitiru::ui_rml

#endif // _WIN32
