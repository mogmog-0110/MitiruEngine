#pragma once

/// @file Dx12PassMarkers.hpp
/// @brief `Renderer3D main` の各パスの先頭に置く DRED のしるし
/// @details 3D は 1 本のコマンドリストに全パスを積むので、breadcrumbs の op 番号だけではどのパスで止まったか
///          分からない。しるしは条件付きのパスでも必ず置き、n 番目のしるし = 表の n 番目にしておく。
///          ドライバが SetMarker の文字列を残さなくても、報告が番号から名前を引ける (#79)。

#ifdef _WIN32

#include <d3d12.h>

#include <mitiru/gfx/dx12/Dx12DredReport.hpp>

namespace mitiru::render::dx12
{

enum class Pass3D : int
{
	Begin,
	ShadowCascade0,
	ShadowCascade1,
	ShadowCascade2,
	Opaque,
	Clod,
	Sky,
	Transparent,
	Trails,
	Velocity,
	Ssao,
	MsaaResolve,
	AerialFog,
	Bloom,
	Tonemap,
	Outline,
	OcclusionReadback,
	DepthOfField,
	AntiAliasing,
	MotionBlur,
	Upscale,
	StyleLive2DNeural,
	Count
};

inline constexpr const wchar_t* kPass3DNames[static_cast<int>(Pass3D::Count)] = {
	L"3D begin",
	L"3D shadow cascade 0",
	L"3D shadow cascade 1",
	L"3D shadow cascade 2",
	L"3D opaque",
	L"3D clod",
	L"3D sky / volumetric fog",
	L"3D OIT (transparent)",
	L"3D trails",
	L"3D velocity",
	L"3D SSAO",
	L"3D MSAA resolve",
	L"3D aerial perspective + fog composite",
	L"3D bloom",
	L"3D tonemap",
	L"3D outline",
	L"3D occlusion readback",
	L"3D depth of field",
	L"3D AA (FXAA / TAA)",
	L"3D motion blur",
	L"3D upscale (TAAU)",
	L"3D style/Live2D/neural/relight",
};

inline constexpr const wchar_t* kRenderer3DListName = L"Renderer3D main";

inline void markPass(ID3D12GraphicsCommandList* cl, Pass3D pass) noexcept
{
	gfx::dred::markPass(cl, kPass3DNames[static_cast<int>(pass)]);
}

inline void registerPass3DOrder() noexcept
{
	gfx::dred::registerPassOrder(kRenderer3DListName, kPass3DNames, static_cast<int>(Pass3D::Count));
}

} // namespace mitiru::render::dx12

#endif // _WIN32
