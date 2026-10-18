#pragma once

/// @file StartupSplash.hpp
/// @brief 最初の present まで窓を黒く塗り、隅に 1 枚絵を出す (ADR 0080)
/// @details 最初のフレームの準備の間はゲームのコードが動かない。swap chain が最初に present するまで窓の中身は GDI の絵
///          なので、WM_PAINT で描けば GPU の準備を待たずに出せる。置き場の計算は Win32 を使わないのでテストできる。

#include <algorithm>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace mitiru::platform
{

/// @brief 1 枚絵を描く矩形。単位はクライアント座標の画素
struct SplashRect
{
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

/// @brief 絵を右下に置く。拡大せず、窓の幅と高さの 4 割以内に縮める。余白は窓の高さの 1/24
[[nodiscard]] inline SplashRect placeSplash(int clientW, int clientH, int imageW, int imageH) noexcept
{
	if (clientW <= 0 || clientH <= 0 || imageW <= 0 || imageH <= 0) { return {}; }
	constexpr float kMaxShare = 0.4f;
	const float scale = std::min({1.0f, kMaxShare * static_cast<float>(clientW) / static_cast<float>(imageW),
	                              kMaxShare * static_cast<float>(clientH) / static_cast<float>(imageH)});
	const int w = std::max(1, static_cast<int>(static_cast<float>(imageW) * scale));
	const int h = std::max(1, static_cast<int>(static_cast<float>(imageH) * scale));
	const int margin = clientH / 24;
	return {clientW - margin - w, clientH - margin - h, w, h};
}

class StartupSplash
{
public:
	/// @param rgba 上の行から並んだ 8 bit RGBA。空なら黒だけで塗る
	void setImage(const std::uint8_t* rgba, int w, int h)
	{
		m_bgra.clear();
		m_w = 0;
		m_h = 0;
		if (rgba == nullptr || w <= 0 || h <= 0) { return; }
		m_w = w;
		m_h = h;
		m_bgra.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
		for (std::size_t i = 0; i < m_bgra.size(); i += 4)
		{
			// 黒の上に描くため、各色成分にアルファ値を掛ける
			const unsigned a = rgba[i + 3];
			m_bgra[i + 0] = static_cast<std::uint8_t>(rgba[i + 2] * a / 255u);
			m_bgra[i + 1] = static_cast<std::uint8_t>(rgba[i + 1] * a / 255u);
			m_bgra[i + 2] = static_cast<std::uint8_t>(rgba[i + 0] * a / 255u);
			m_bgra[i + 3] = 255;
		}
	}

	[[nodiscard]] bool hasImage() const noexcept { return !m_bgra.empty(); }

#ifdef _WIN32
	void paint(HDC dc, const RECT& client) const
	{
		FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		if (m_bgra.empty()) { return; }
		const SplashRect r = placeSplash(client.right - client.left, client.bottom - client.top, m_w, m_h);
		BITMAPINFO bi{};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = m_w;
		bi.bmiHeader.biHeight = -m_h;   // 上の行から
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		SetStretchBltMode(dc, HALFTONE);
		StretchDIBits(dc, client.left + r.x, client.top + r.y, r.w, r.h, 0, 0, m_w, m_h, m_bgra.data(), &bi,
		              DIB_RGB_COLORS, SRCCOPY);
	}
#endif

private:
	std::vector<std::uint8_t> m_bgra;
	int m_w = 0;
	int m_h = 0;
};

}  // namespace mitiru::platform
