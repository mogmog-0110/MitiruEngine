#pragma once

/// @file DawnApi.hpp
/// @brief Dawn が内部で使う OS の描画 API の選択を宣言する。Engine.hpp から読めるように、WebGPU のヘッダを含まない

#include <string>
#include <string_view>

namespace mitiru::gfx
{

enum class DawnApi
{
	D3D11,   ///< D3D12 を持たない GPU も拾える。段階 0 の次の手 (ADR 0087)
	D3D12,
	Vulkan,
};

[[nodiscard]] constexpr std::string_view dawnApiName(DawnApi api) noexcept
{
	switch (api)
	{
	case DawnApi::D3D11:  return "d3d11";
	case DawnApi::D3D12:  return "d3d12";
	case DawnApi::Vulkan: return "vulkan";
	}
	return "d3d11";
}

/// @brief "d3d11"、"d3d12"、"vulkan" を DawnApi に変換する。それ以外は false を返す
[[nodiscard]] inline bool parseDawnApi(std::string_view name, DawnApi& out) noexcept
{
	if (name == "d3d11") { out = DawnApi::D3D11; return true; }
	if (name == "d3d12") { out = DawnApi::D3D12; return true; }
	if (name == "vulkan") { out = DawnApi::Vulkan; return true; }
	return false;
}

} // namespace mitiru::gfx
