#pragma once

/// @file ScreenCapture.hpp
/// @brief スクリーンショットキャプチャユーティリティ
/// @details IDevice::readPixels を利用してフレームバッファの内容を取得する。

#include <cstdint>
#include <vector>

#include <mitiru/gfx/IDevice.hpp>

namespace mitiru::render
{

/// @brief スクリーンショットデータ
/// @details キャプチャされたフレームバッファのピクセルデータを保持する。
struct ScreenshotData
{
	std::vector<std::uint8_t> pixels;  ///< RGBA8形式のピクセルデータ
	int width = 0;                     ///< 画像幅（ピクセル）
	int height = 0;                    ///< 画像高さ（ピクセル）
	std::uint64_t frameNumber = 0;     ///< キャプチャ時のフレーム番号

	/// @brief ピクセルデータが有効かどうかを判定する
	/// @return 幅・高さが正でデータが存在すればtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return (width > 0) && (height > 0) && !pixels.empty();
	}

	/// @brief 指定座標のピクセル色を取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return RGBA8の4要素配列（範囲外は全て0）
	/// @note 範囲外アクセスの場合は黒透明を返す
	[[nodiscard]] std::uint8_t pixelR(int x, int y) const noexcept
	{
		if (x < 0 || x >= width || y < 0 || y >= height)
		{
			return 0;
		}
		const auto idx = static_cast<std::size_t>((y * width + x) * 4);
		return pixels[idx];
	}

	/// @brief 指定座標の緑成分を取得する
	[[nodiscard]] std::uint8_t pixelG(int x, int y) const noexcept
	{
		if (x < 0 || x >= width || y < 0 || y >= height)
		{
			return 0;
		}
		const auto idx = static_cast<std::size_t>((y * width + x) * 4 + 1);
		return pixels[idx];
	}

	/// @brief 指定座標の青成分を取得する
	[[nodiscard]] std::uint8_t pixelB(int x, int y) const noexcept
	{
		if (x < 0 || x >= width || y < 0 || y >= height)
		{
			return 0;
		}
		const auto idx = static_cast<std::size_t>((y * width + x) * 4 + 2);
		return pixels[idx];
	}

	/// @brief 指定座標のアルファ成分を取得する
	[[nodiscard]] std::uint8_t pixelA(int x, int y) const noexcept
	{
		if (x < 0 || x >= width || y < 0 || y >= height)
		{
			return 0;
		}
		const auto idx = static_cast<std::size_t>((y * width + x) * 4 + 3);
		return pixels[idx];
	}
};

/// @brief フレームバッファをキャプチャする
/// @param device GPUデバイス
/// @param width キャプチャ幅
/// @param height キャプチャ高さ
/// @param frameNumber 現在のフレーム番号（オプション）
/// @return キャプチャされたスクリーンショットデータ
///
/// @code
/// auto screenshot = mitiru::render::capture(device, 1280, 720);
/// if (screenshot.isValid())
/// {
///     auto r = screenshot.pixelR(640, 360);
/// }
/// @endcode
[[nodiscard]] inline ScreenshotData capture(
	const gfx::IDevice& device,
	int width,
	int height,
	std::uint64_t frameNumber = 0)
{
	ScreenshotData data;
	data.pixels = device.readPixels(width, height);
	data.width = width;
	data.height = height;
	data.frameNumber = frameNumber;
	return data;
}

} // namespace mitiru::render
