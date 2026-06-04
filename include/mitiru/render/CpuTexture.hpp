#pragma once

/// @file CpuTexture.hpp
/// @brief CPU 側の RGBA8 テクスチャ（ソフトウェア deferred のサンプル用、#17）
/// @details glTF/VRM の base-color などを CPU でサンプルして `pixel.albedo` に書くための最小型。
///          GPU リソースではなく、`std::vector<uint8_t>` の RGBA8 を持つだけ。UV は repeat。

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief CPU 側 RGBA8 テクスチャ
struct CpuTexture
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;   ///< width*height*4（RGBA, sRGB バイト）

	[[nodiscard]] bool valid() const noexcept
	{
		return width > 0 && height > 0 &&
		       rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
	}

	/// @brief nearest サンプル（UV は repeat、戻りは 0..1 の Colorf）。
	[[nodiscard]] sgc::Colorf sampleNearest(float u, float v) const noexcept
	{
		if (!valid()) { return sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f}; }
		// repeat: 小数部を取る（負も 0..1 に折り返す）。
		float fu = u - std::floor(u);
		float fv = v - std::floor(v);
		int x = static_cast<int>(fu * static_cast<float>(width));
		int y = static_cast<int>(fv * static_cast<float>(height));
		if (x >= width)  { x = width - 1; }
		if (y >= height) { y = height - 1; }
		if (x < 0) { x = 0; }
		if (y < 0) { y = 0; }
		const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
		constexpr float inv = 1.0f / 255.0f;
		return sgc::Colorf{rgba[i] * inv, rgba[i + 1] * inv, rgba[i + 2] * inv, rgba[i + 3] * inv};
	}
};

} // namespace mitiru::render
