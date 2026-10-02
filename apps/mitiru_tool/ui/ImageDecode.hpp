#pragma once
// /api/ai/frame が返す base64 の PNG を RGBA8 に戻す (scene_view がゲームの画面を貼るため)。

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace mitiru::tool
{

struct DecodedImage
{
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;
};

[[nodiscard]] std::optional<DecodedImage> decodePngBase64(std::string_view text);

} // namespace mitiru::tool
