#pragma once
// RmlUi 描画層の描画先: 層 (MSAA + 共有 stencil) の積み重ねと、filter 用の 1x 描画先 4 枚。
// 大きさはいつも描画先 (バックバッファ) と同じ。filter の入出力も同じ大きさなので、写すときに
// 補間を通さず画素をそのまま読める。

#ifdef _WIN32

#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Descriptors.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace mitiru::ui_rml::dx12
{

struct RmlTarget
{
	gfx::GpuResource resource;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
	std::uint32_t srv = RmlDescriptorPool::kInvalid;   ///< 1x のものだけが持つ
	int width = 0;
	int height = 0;
	UINT samples = 1;
};

/// 状態を記録しながら遷移を積む (同じ状態なら何もしない)。
void transition(ID3D12GraphicsCommandList* cl, RmlTarget& t, D3D12_RESOURCE_STATES to);
void transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);

class RmlTargetStack
{
public:
	enum Post : std::size_t { Primary = 0, Secondary = 1, Tertiary = 2, BlendMask = 3, PostCount = 4 };
	static constexpr std::uint32_t kMaxLayers = 24;

	bool create(ID3D12Device* device, RmlDescriptorPool* srvPool, UINT samples);

	/// 大きさが変わっていたら全部作り直す。古い SRV 枠は fenceValue を越えるまで使い回さない。
	bool ensureSize(int width, int height, std::uint64_t fenceValue);

	/// 層を 1 つ積み、その番号 (RmlUi の LayerHandle) を返す。描画先が作れなければ -1
	int push();
	void pop() noexcept { if (m_count > 0) { --m_count; } }
	void reset() noexcept { m_count = 0; }

	[[nodiscard]] int topIndex() const noexcept { return static_cast<int>(m_count) - 1; }
	[[nodiscard]] RmlTarget& layer(int index) { return m_layers[static_cast<std::size_t>(index)]; }
	[[nodiscard]] RmlTarget& post(Post p) { return m_post[m_postOrder[p]]; }
	void swapPrimarySecondary() noexcept { std::swap(m_postOrder[Primary], m_postOrder[Secondary]); }

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE dsv() const noexcept { return m_dsvHeap.at(0); }
	[[nodiscard]] ID3D12Resource* stencil() const noexcept { return m_stencil.Get(); }
	[[nodiscard]] UINT samples() const noexcept { return m_samples; }
	[[nodiscard]] int width() const noexcept { return m_width; }
	[[nodiscard]] int height() const noexcept { return m_height; }

private:
	bool createLayer(RmlTarget& t, std::uint32_t rtvIndex);
	bool createPost(RmlTarget& t, std::uint32_t rtvIndex);
	bool createStencil();
	void releaseAll(std::uint64_t fenceValue);

	ID3D12Device* m_device = nullptr;
	RmlDescriptorPool* m_srvPool = nullptr;
	RmlCpuDescriptorHeap m_rtvHeap;
	RmlCpuDescriptorHeap m_dsvHeap;
	std::vector<RmlTarget> m_layers;
	std::array<RmlTarget, PostCount> m_post;
	std::array<std::size_t, PostCount> m_postOrder = { 0, 1, 2, 3 };
	gfx::GpuResource m_stencil;
	std::uint32_t m_count = 0;
	UINT m_samples = 1;
	int m_width = 0;
	int m_height = 0;
};

} // namespace mitiru::ui_rml::dx12

#endif // _WIN32
