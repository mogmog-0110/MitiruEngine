#pragma once

/// @file Bloom.hpp
/// @brief ソフトウェアブルームポストプロセス
/// @details 輝度閾値を超えるピクセルを抽出・ブラーして合成する。

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mitiru::effects
{

/// @brief ブルームエフェクト（ソフトウェア処理）
class Bloom
{
public:
	float threshold = 0.8f;   ///< 輝度閾値 [0,1]
	float intensity = 1.0f;   ///< ブルーム強度
	int blurPasses = 3;       ///< ブラー反復回数

	/// @brief RGBA8 ピクセルデータにブルームを適用する
	/// @param pixels RGBA8ピクセルデータ（in-place変更）
	/// @param width 画像幅
	/// @param height 画像高さ
	void apply(std::vector<uint8_t>& pixels, int width, int height) const
	{
		if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width * height * 4))
		{
			return;
		}

		const int size = width * height;

		/// 輝度閾値で明るいピクセルを抽出する
		std::vector<float> bright(size * 3, 0.0f);
		for (int i = 0; i < size; ++i)
		{
			const float r = static_cast<float>(pixels[i * 4]) / 255.0f;
			const float g = static_cast<float>(pixels[i * 4 + 1]) / 255.0f;
			const float b = static_cast<float>(pixels[i * 4 + 2]) / 255.0f;
			const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
			if (luminance > threshold)
			{
				const float factor = luminance - threshold;
				bright[i * 3] = r * factor;
				bright[i * 3 + 1] = g * factor;
				bright[i * 3 + 2] = b * factor;
			}
		}

		/// 簡易ボックスブラーを適用する
		std::vector<float> temp(size * 3);
		for (int pass = 0; pass < blurPasses; ++pass)
		{
			/// 水平ブラー
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					float sr = 0, sg = 0, sb = 0;
					int count = 0;
					for (int dx = -2; dx <= 2; ++dx)
					{
						const int nx = x + dx;
						if (nx >= 0 && nx < width)
						{
							const int idx = (y * width + nx) * 3;
							sr += bright[idx];
							sg += bright[idx + 1];
							sb += bright[idx + 2];
							++count;
						}
					}
					const int idx = (y * width + x) * 3;
					temp[idx] = sr / static_cast<float>(count);
					temp[idx + 1] = sg / static_cast<float>(count);
					temp[idx + 2] = sb / static_cast<float>(count);
				}
			}

			/// 垂直ブラー
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					float sr = 0, sg = 0, sb = 0;
					int count = 0;
					for (int dy = -2; dy <= 2; ++dy)
					{
						const int ny = y + dy;
						if (ny >= 0 && ny < height)
						{
							const int idx = (ny * width + x) * 3;
							sr += temp[idx];
							sg += temp[idx + 1];
							sb += temp[idx + 2];
							++count;
						}
					}
					const int idx = (y * width + x) * 3;
					bright[idx] = sr / static_cast<float>(count);
					bright[idx + 1] = sg / static_cast<float>(count);
					bright[idx + 2] = sb / static_cast<float>(count);
				}
			}
		}

		/// 元画像に加算合成する
		for (int i = 0; i < size; ++i)
		{
			const float r = static_cast<float>(pixels[i * 4]) / 255.0f + bright[i * 3] * intensity;
			const float g = static_cast<float>(pixels[i * 4 + 1]) / 255.0f + bright[i * 3 + 1] * intensity;
			const float b = static_cast<float>(pixels[i * 4 + 2]) / 255.0f + bright[i * 3 + 2] * intensity;
			pixels[i * 4] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
			pixels[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
			pixels[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
		}
	}
};

} // namespace mitiru::effects
