#include "ImageDecode.hpp"

#include <mitiru/util/Base64.hpp>

#include <stb_image.h>

#include <stdexcept>
#include <string>

namespace mitiru::tool
{

std::optional<DecodedImage> decodePngBase64(std::string_view text)
{
	std::vector<std::uint8_t> file;
	try { file = util::Base64::decode(std::string(text)); }
	catch (const std::invalid_argument&) { return std::nullopt; }
	if (file.empty()) { return std::nullopt; }
	int w = 0, h = 0, channels = 0;
	stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &channels, 4);
	if (pixels == nullptr) { return std::nullopt; }
	DecodedImage img{ w, h, std::vector<std::uint8_t>(pixels, pixels + static_cast<std::size_t>(w) * h * 4) };
	stbi_image_free(pixels);
	return img;
}

} // namespace mitiru::tool
