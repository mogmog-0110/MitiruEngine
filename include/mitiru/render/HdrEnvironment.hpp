#pragma once

/// @file HdrEnvironment.hpp
/// @brief 正距円筒の HDR 画像 (Radiance .hdr) を読み、HdrCubemap にする (Screen::environmentHdr3D)
/// @details 画像の行 0 は天頂、u = 0.5 は -Z を向く。u が増えると、上から見て時計回り (+X 側) に回る。
///          yawDeg はこの向きを Y 軸まわりに回し、正の値では u = 0.5 の向きが -Z から +X 側へ回る。
///          読めるのは RGBE の新形式の RLE・旧形式の RLE・無圧縮で、向きは "-Y h +X w" (上から下) と
///          "+Y h +X w" (下から上)。XYZE とそれ以外の向きは読まない。EXPOSURE= の行は stb_image などと同じく無視する。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Vec3.hpp>

#include <mitiru/render/HdrCubemap.hpp>

namespace mitiru::render
{

/// @brief 線形 RGB の正距円筒画像。行 0 が天頂
struct EquirectImage
{
	int width = 0;
	int height = 0;
	std::vector<float> rgb;   ///< width * height * 3

	[[nodiscard]] bool valid() const noexcept
	{
		return width > 0 && height > 0 && rgb.size() == static_cast<std::size_t>(width) * height * 3u;
	}
};

namespace hdr_detail
{

/// 新形式の RLE ではない行。旧形式の RLE は、(1, 1, 1, n) の画素が「直前の画素を n 回くり返す」(続けば 8 bit ずつ桁が上がる)
[[nodiscard]] inline bool readFlatScanline(std::span<const std::uint8_t> in, std::size_t& pos, int width, std::uint8_t* out)
{
	int shift = 0;
	for (int x = 0; x < width;)
	{
		if (pos + 4 > in.size()) { return false; }
		const std::uint8_t* px = in.data() + pos;
		pos += 4;
		if (px[0] == 1 && px[1] == 1 && px[2] == 1)
		{
			if (x == 0 || shift > 24) { return false; }
			const long long repeat = static_cast<long long>(px[3]) << shift;
			if (repeat > width - x) { return false; }
			for (long long i = 0; i < repeat; ++i, ++x) { std::copy_n(out + (x - 1) * 4, 4, out + x * 4); }
			shift += 8;
			continue;
		}
		shift = 0;
		std::copy_n(px, 4, out + x * 4);
		++x;
	}
	return true;
}

/// RGBE の 1 行を out (width * 4 byte) へ読む。新形式の RLE は 4 成分を別々に格納する
[[nodiscard]] inline bool readScanline(std::span<const std::uint8_t> in, std::size_t& pos, int width, std::uint8_t* out)
{
	const bool rle = width >= 8 && width < 0x8000 && pos + 4 <= in.size() && in[pos] == 2 && in[pos + 1] == 2 &&
	                 ((in[pos + 2] << 8) | in[pos + 3]) == width;
	if (!rle) { return readFlatScanline(in, pos, width, out); }
	pos += 4;
	for (int ch = 0; ch < 4; ++ch)
	{
		for (int x = 0; x < width;)
		{
			if (pos >= in.size()) { return false; }
			int n = in[pos++];
			const bool run = n > 128;
			if (run) { n -= 128; }
			if (n == 0 || x + n > width || pos + (run ? 1u : static_cast<std::size_t>(n)) > in.size()) { return false; }
			for (int i = 0; i < n; ++i, ++x) { out[x * 4 + ch] = run ? in[pos] : in[pos + static_cast<std::size_t>(i)]; }
			pos += run ? 1u : static_cast<std::size_t>(n);
		}
	}
	return true;
}

/// 1 行を読んで pos を進める (行末の CR は落とす)。行の終わりが無ければ false
[[nodiscard]] inline bool nextLine(std::string_view text, std::size_t& pos, std::string_view& line)
{
	const std::size_t end = text.find('\n', pos);
	if (end == std::string_view::npos) { return false; }
	line = text.substr(pos, end - pos);
	if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
	pos = end + 1;
	return true;
}

/// 1 辺の上限。これを超える画像は、壊れたヘッダで巨大な確保をしないために断る
inline constexpr int kMaxSide = 16384;
inline constexpr std::size_t kMaxPixels = 64u * 1024u * 1024u;

/// ヘッダを読み、画素の開始位置と大きさを返す。bottomUp は "+Y" (最初の行が画像の下) のとき真
[[nodiscard]] inline bool readHeader(std::span<const std::uint8_t> in, std::size_t& pos, int& width, int& height,
                                     bool& bottomUp)
{
	const std::string_view text(reinterpret_cast<const char*>(in.data()), in.size());
	std::string_view line;
	pos = 0;
	if (!nextLine(text, pos, line) || !line.starts_with("#?")) { return false; }
	for (;;)
	{
		if (!nextLine(text, pos, line)) { return false; }
		if (line.empty()) { break; }
		if (line.starts_with("FORMAT=") && line != "FORMAT=32-bit_rle_rgbe") { return false; }
	}
	if (!nextLine(text, pos, line)) { return false; }
	bottomUp = line.starts_with("+Y ");
	if (!bottomUp && !line.starts_with("-Y ")) { return false; }
	const std::size_t xAt = line.find(" +X ");
	if (xAt == std::string_view::npos) { return false; }
	height = std::atoi(std::string(line.substr(3, xAt - 3)).c_str());
	width = std::atoi(std::string(line.substr(xAt + 4)).c_str());
	return width > 0 && height > 0 && width <= kMaxSide && height <= kMaxSide &&
	       static_cast<std::size_t>(width) * static_cast<std::size_t>(height) <= kMaxPixels;
}

}  // namespace hdr_detail

/// @brief .hdr の bytes を線形 RGB に変換する。形式が違う、または壊れている場合は nullopt。結果の行 0 は常に画像の上
[[nodiscard]] inline std::optional<EquirectImage> decodeRadianceHdr(std::span<const std::uint8_t> bytes)
{
	std::size_t pos = 0;
	EquirectImage img;
	bool bottomUp = false;
	if (!hdr_detail::readHeader(bytes, pos, img.width, img.height, bottomUp)) { return std::nullopt; }
	// 1 行は最小でも (新形式の RLE の 4 byte の頭) + 4 成分 x 1 つの run (2 byte) = 12 byte か、幅が狭ければ生の 4 x 幅
	const std::size_t minRow = std::min<std::size_t>(12u, static_cast<std::size_t>(img.width) * 4u);
	if (bytes.size() - pos < minRow * static_cast<std::size_t>(img.height)) { return std::nullopt; }
	img.rgb.resize(static_cast<std::size_t>(img.width) * img.height * 3u);
	std::vector<std::uint8_t> line(static_cast<std::size_t>(img.width) * 4u);
	for (int y = 0; y < img.height; ++y)
	{
		if (!hdr_detail::readScanline(bytes, pos, img.width, line.data())) { return std::nullopt; }
		const int row = bottomUp ? img.height - 1 - y : y;
		float* dst = img.rgb.data() + static_cast<std::size_t>(row) * img.width * 3u;
		for (int x = 0; x < img.width; ++x)
		{
			const std::uint8_t e = line[static_cast<std::size_t>(x) * 4u + 3u];
			const float f = e == 0 ? 0.0f : std::ldexp(1.0f, static_cast<int>(e) - 136);
			for (int c = 0; c < 3; ++c) { dst[x * 3 + c] = line[static_cast<std::size_t>(x) * 4u + c] * f; }
		}
	}
	return img;
}

/// @brief dir の向きに対応する画像の u, v。dir は正規化されていなくてもよく、範囲は 0 から 1、v = 0 が天頂
inline void equirectUv(const sgc::Vec3f& dir, float yawDeg, float& u, float& v) noexcept
{
	constexpr float kPi = 3.14159265358979f;
	const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	const float y = len > 0.0f ? std::clamp(dir.y / len, -1.0f, 1.0f) : 1.0f;
	const float az = std::atan2(dir.x, -dir.z) - yawDeg * (kPi / 180.0f);
	u = az / (2.0f * kPi) + 0.5f;
	u -= std::floor(u);
	v = 0.5f - std::asin(y) / kPi;
}

/// @brief u は横方向に回り込み、v は端で止めて双線形補間する
[[nodiscard]] inline sgc::Vec3f sampleEquirect(const EquirectImage& img, float u, float v) noexcept
{
	const float fx = u * static_cast<float>(img.width) - 0.5f;
	const float fy = std::clamp(v * static_cast<float>(img.height) - 0.5f, 0.0f, static_cast<float>(img.height - 1));
	const int x0 = static_cast<int>(std::floor(fx));
	const int y0 = static_cast<int>(fy);
	const float tx = fx - static_cast<float>(x0);
	const float ty = fy - static_cast<float>(y0);
	const auto at = [&](int x, int y) {
		x = ((x % img.width) + img.width) % img.width;
		y = std::min(y, img.height - 1);
		const float* p = img.rgb.data() + (static_cast<std::size_t>(y) * img.width + x) * 3u;
		return sgc::Vec3f{p[0], p[1], p[2]};
	};
	const sgc::Vec3f top = at(x0, y0) * (1.0f - tx) + at(x0 + 1, y0) * tx;
	const sgc::Vec3f bottom = at(x0, y0 + 1) * (1.0f - tx) + at(x0 + 1, y0 + 1) * tx;
	return top * (1.0f - ty) + bottom * ty;
}

/// @brief 正距円筒画像を faceSize の HdrCubemap にする。明るさは画像のまま (intensity は描くときに掛ける)
[[nodiscard]] inline HdrCubemap hdrCubemapFromEquirect(const EquirectImage& img, int faceSize, float yawDeg)
{
	if (!img.valid() || faceSize <= 0) { return {}; }
	return HdrCubemap::fromDirections(faceSize, [&](const sgc::Vec3f& dir) {
		float u = 0.0f, v = 0.0f;
		equirectUv(dir, yawDeg, u, v);
		return sampleEquirect(img, u, v);
	});
}

/// @brief 画像の幅に見合う空の面の一辺。正距円筒は幅が 360 度なので、1 面 (90 度) は幅の 1/4 の画素で足りる。256..1024 に収める
[[nodiscard]] inline int skyFaceSizeFor(int equirectWidth) noexcept
{
	int size = 256;
	while (size < 1024 && size * 4 < equirectWidth) { size *= 2; }
	return size;
}

}  // namespace mitiru::render
