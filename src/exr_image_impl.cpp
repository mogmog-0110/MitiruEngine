/// @file exr_image_impl.cpp
/// @brief HdrEnvironment.hpp の decodeExrImage の実装。tinyexr はこの TU にだけ入れる

#include <mitiru/render/HdrEnvironment.hpp>

#include <cstring>
#include <memory>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4267 4456 4457 4100 4505 4701 4702 4996)
#endif
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_STB_ZLIB 0
#define TINYEXR_USE_THREAD 0
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace mitiru::render
{
namespace
{

struct HeaderGuard
{
	EXRHeader h{};
	HeaderGuard() { InitEXRHeader(&h); }
	~HeaderGuard() { FreeEXRHeader(&h); }
};

struct ImageGuard
{
	EXRImage i{};
	ImageGuard() { InitEXRImage(&i); }
	~ImageGuard() { FreeEXRImage(&i); }
};

/// tinyexr の文 (無ければ fallback) を error に写して解放する
void takeError(const char* msg, const char* fallback, std::string& error)
{
	error = msg != nullptr ? msg : fallback;
	if (msg != nullptr) { FreeEXRErrorMessage(msg); }
	while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) { error.pop_back(); }
}

int channelIndex(const EXRHeader& h, const char* name)
{
	for (int i = 0; i < h.num_channels; ++i)
	{
		if (std::strcmp(h.channels[i].name, name) == 0) { return i; }
	}
	return -1;
}

/// 1 段目のタイルを width x height の平面へ戻す。tinyexr は tile.offset_x / offset_y に画素ではなくタイルの番号を、
/// 各タイルの画素に「ヘッダーのタイル寸法」を行の幅にした配列を返す (端の小さいタイルも同じ幅で、使うのは tile.width 列まで)
void gatherTiles(const EXRImage& img, int ch, const EXRHeader& h, int width, int height, int stride, float* dst)
{
	for (int t = 0; t < img.num_tiles; ++t)
	{
		const EXRTile& tile = img.tiles[t];
		if (tile.level_x != 0 || tile.level_y != 0 || tile.images == nullptr) { continue; }
		const auto* src = reinterpret_cast<const float*>(tile.images[ch]);
		const long long originX = static_cast<long long>(tile.offset_x) * h.tile_size_x;
		const long long originY = static_cast<long long>(tile.offset_y) * h.tile_size_y;
		for (int y = 0; y < tile.height; ++y)
		{
			const long long dy = originY + y;
			if (dy < 0 || dy >= height) { continue; }
			for (int x = 0; x < tile.width; ++x)
			{
				const long long dx = originX + x;
				if (dx < 0 || dx >= width) { continue; }
				dst[(static_cast<std::size_t>(dy) * width + static_cast<std::size_t>(dx)) * stride] =
				    src[static_cast<std::size_t>(y) * h.tile_size_x + x];
			}
		}
	}
}

} // namespace

std::optional<EquirectImage> decodeExrImage(std::span<const std::uint8_t> bytes, std::string& error)
{
	EXRVersion version{};
	if (ParseEXRVersionFromMemory(&version, bytes.data(), bytes.size()) != TINYEXR_SUCCESS)
	{
		error = "OpenEXR ではない";
		return std::nullopt;
	}
	if (version.multipart)
	{
		error = "複数パートの EXR は読まない";
		return std::nullopt;
	}
	if (version.non_image)
	{
		error = "deep の EXR は読まない";
		return std::nullopt;
	}

	HeaderGuard header;
	const char* msg = nullptr;
	if (ParseEXRHeaderFromMemory(&header.h, &version, bytes.data(), bytes.size(), &msg) != TINYEXR_SUCCESS)
	{
		takeError(msg, "EXR のヘッダーを読めない", error);
		return std::nullopt;
	}
	const EXRHeader& h = header.h;
	const long long width = static_cast<long long>(h.data_window.max_x) - h.data_window.min_x + 1;
	const long long height = static_cast<long long>(h.data_window.max_y) - h.data_window.min_y + 1;
	if (width <= 0 || height <= 0 || width > hdr_detail::kMaxSide || height > hdr_detail::kMaxSide ||
	    static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height) > hdr_detail::kMaxPixels)
	{
		error = "EXR の大きさが範囲外 (1 辺 16384、6700 万画素まで)";
		return std::nullopt;
	}
	if (h.tiled && (h.tile_size_x <= 0 || h.tile_size_y <= 0 || h.tile_size_x > 16384 || h.tile_size_y > 16384))
	{
		error = "EXR のタイルの寸法が範囲外";
		return std::nullopt;
	}
	int idx[3] = {channelIndex(h, "R"), channelIndex(h, "G"), channelIndex(h, "B")};
	if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0)
	{
		const int y = channelIndex(h, "Y");
		if (y < 0)
		{
			error = "EXR に R, G, B (か Y) のチャンネルが無い";
			return std::nullopt;
		}
		idx[0] = idx[1] = idx[2] = y;
	}
	for (int i = 0; i < h.num_channels; ++i)
	{
		if (h.pixel_types[i] == TINYEXR_PIXELTYPE_UINT && (i == idx[0] || i == idx[1] || i == idx[2]))
		{
			error = "EXR の整数 (UINT) のチャンネルは読まない";
			return std::nullopt;
		}
		if (h.channels[i].x_sampling != 1 || h.channels[i].y_sampling != 1)
		{
			error = "EXR の間引いた (sampling が 1 でない) チャンネルは読まない";
			return std::nullopt;
		}
		// half は float へ上げて受ける。使わない整数のチャンネル (物体 ID など) は、tinyexr が型の変換を許さないのでそのまま
		if (h.pixel_types[i] != TINYEXR_PIXELTYPE_UINT) { header.h.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; }
	}

	ImageGuard image;
	if (LoadEXRImageFromMemory(&image.i, &h, bytes.data(), bytes.size(), &msg) != TINYEXR_SUCCESS)
	{
		takeError(msg, "EXR の画素を復号できない", error);
		return std::nullopt;
	}
	const EXRImage& img = image.i;
	if (img.width != width || img.height != height)
	{
		error = "EXR の画素の大きさがヘッダーと合わない";
		return std::nullopt;
	}

	EquirectImage out;
	out.width = static_cast<int>(width);
	out.height = static_cast<int>(height);
	out.rgb.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u, 0.0f);
	if (img.images != nullptr)
	{
		for (int c = 0; c < 3; ++c)
		{
			const auto* src = reinterpret_cast<const float*>(img.images[idx[c]]);
			if (src == nullptr) { error = "EXR のチャンネルが空"; return std::nullopt; }
			for (std::size_t p = 0; p < static_cast<std::size_t>(width) * height; ++p) { out.rgb[p * 3 + c] = src[p]; }
		}
		return out;
	}
	if (img.tiles == nullptr)
	{
		error = "EXR の画素が無い";
		return std::nullopt;
	}
	for (int c = 0; c < 3; ++c) { gatherTiles(img, idx[c], h, out.width, out.height, 3, out.rgb.data() + c); }
	return out;
}

} // namespace mitiru::render
