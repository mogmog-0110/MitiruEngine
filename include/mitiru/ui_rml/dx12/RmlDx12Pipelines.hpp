#pragma once
// RmlUi 描画層のルートシグネチャと PSO 一式。PSO は起動時に全部作る (描画の途中で作ると
// その 1 フレームだけ長くなる)。層 (MSAA + stencil) に描く版と、後処理用の 1x 描画先に描く版がある。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>

namespace mitiru::ui_rml::dx12
{

inline constexpr DXGI_FORMAT kRmlColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
inline constexpr DXGI_FORMAT kRmlStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

enum class RmlPso : std::uint8_t
{
	// 層 (MSAA、stencil あり) に描く。*Clip は stencil の値が参照値と等しい所だけに描く。
	Color, ColorClip, Texture, TextureClip, Gradient, GradientClip,
	MaskReplace, MaskIncrement,
	LayerOver, LayerOverClip, LayerReplace, LayerReplaceClip,
	// 後処理用の 1x 描画先に描く。
	PostCopy, PostCopyOver, PostSample, PostColorMatrix, PostBlendMask, PostBlur, PostDropShadow,
	Count
};

enum class RmlRootParam : UINT { Constants = 0, Texture = 1, Mask = 2 };

class RmlDx12Pipelines
{
public:
	/// @param layerSamples 層の MSAA サンプル数 (1 なら MSAA なし)
	/// @return 失敗したら false。理由は error() に入る
	bool create(ID3D12Device* device, UINT layerSamples);

	[[nodiscard]] ID3D12RootSignature* rootSignature() const noexcept { return m_root.Get(); }
	[[nodiscard]] ID3D12PipelineState* get(RmlPso p) const noexcept { return m_pso[static_cast<std::size_t>(p)].Get(); }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
	struct Blobs;
	bool createRootSignature(ID3D12Device* device);
	bool compileShaders(Blobs& b);
	bool createAll(ID3D12Device* device, const Blobs& b, UINT layerSamples);

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
	std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<std::size_t>(RmlPso::Count)> m_pso;
	std::string m_error;
};

} // namespace mitiru::ui_rml::dx12

#endif // _WIN32
