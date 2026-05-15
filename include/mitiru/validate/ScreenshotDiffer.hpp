#pragma once

/// @file ScreenshotDiffer.hpp
/// @brief スクリーンショット比較ユーティリティ
/// @details 2枚のスクリーンショットをピクセル単位で比較し、差分結果・差分画像を生成する。
///          ビジュアルリグレッションテストの基盤として使用される。

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#include <sgc/math/Rect.hpp>
#include <mitiru/render/ScreenCapture.hpp>
#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::validate
{

/// @brief ピクセル比較の差分結果
/// @details 比較対象の2枚の画像間の差異を数値・領域情報として保持する。
struct DiffResult
{
	bool match = true;              ///< 全ピクセルが許容範囲内であればtrue
	int totalPixels = 0;            ///< 比較対象の総ピクセル数
	int differentPixels = 0;        ///< 許容範囲を超えたピクセル数
	float diffPercentage = 0.0f;    ///< 差異ピクセルの割合（0.0〜100.0）
	int maxChannelDiff = 0;         ///< 全チャンネル中の最大差分値（0〜255）
	sgc::Rectf boundingBox{};       ///< 差異ピクセルを囲む最小矩形

	/// @brief 差分結果をJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";
		json += "\"match\":" + std::string(match ? "true" : "false");
		json += ",\"totalPixels\":" + std::to_string(totalPixels);
		json += ",\"differentPixels\":" + std::to_string(differentPixels);
		json += ",\"diffPercentage\":" + std::to_string(diffPercentage);
		json += ",\"maxChannelDiff\":" + std::to_string(maxChannelDiff);
		json += ",\"boundingBox\":{";
		json += "\"x\":" + std::to_string(boundingBox.x());
		json += ",\"y\":" + std::to_string(boundingBox.y());
		json += ",\"w\":" + std::to_string(boundingBox.width());
		json += ",\"h\":" + std::to_string(boundingBox.height());
		json += "}}";
		return json;
	}
};

/// @brief スクリーンショットのピクセル単位比較器
/// @details 2枚のスクリーンショットを比較し、差分情報の算出や差分画像の生成を行う。
///
/// @code
/// mitiru::validate::ScreenshotDiffer differ;
/// auto result = differ.compare(golden, current, 2);
/// if (!result.match)
/// {
///     auto diffImg = differ.generateDiffImage(golden, current, 2);
/// }
/// @endcode
class ScreenshotDiffer
{
public:
	/// @brief 2枚のスクリーンショットをピクセル単位で比較する
	/// @param expected 期待画像（ゴールデンイメージ）
	/// @param actual 実際の画像（テスト対象）
	/// @param tolerance チャンネルごとの許容差分値（0=完全一致、10=±10まで許容）
	/// @return 差分結果
	/// @note 画像サイズが異なる場合は不一致として全ピクセルを差分扱いにする
	[[nodiscard]] DiffResult compare(
		const render::ScreenshotData& expected,
		const render::ScreenshotData& actual,
		int tolerance = 0) const
	{
		DiffResult result;

		if (!expected.isValid() || !actual.isValid())
		{
			result.match = false;
			return result;
		}

		if (expected.width != actual.width || expected.height != actual.height)
		{
			result.match = false;
			result.totalPixels = expected.width * expected.height;
			result.differentPixels = result.totalPixels;
			result.diffPercentage = 100.0f;
			result.boundingBox = sgc::Rectf{
				0.0f, 0.0f,
				static_cast<float>(expected.width),
				static_cast<float>(expected.height)};
			return result;
		}

		const int w = expected.width;
		const int h = expected.height;
		result.totalPixels = w * h;

		int minX = w;
		int minY = h;
		int maxX = -1;
		int maxY = -1;

		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				const int dr = std::abs(
					static_cast<int>(expected.pixelR(x, y)) -
					static_cast<int>(actual.pixelR(x, y)));
				const int dg = std::abs(
					static_cast<int>(expected.pixelG(x, y)) -
					static_cast<int>(actual.pixelG(x, y)));
				const int db = std::abs(
					static_cast<int>(expected.pixelB(x, y)) -
					static_cast<int>(actual.pixelB(x, y)));
				const int da = std::abs(
					static_cast<int>(expected.pixelA(x, y)) -
					static_cast<int>(actual.pixelA(x, y)));

				const int channelMax = std::max({dr, dg, db, da});
				result.maxChannelDiff = std::max(result.maxChannelDiff, channelMax);

				if (channelMax > tolerance)
				{
					++result.differentPixels;
					minX = std::min(minX, x);
					minY = std::min(minY, y);
					maxX = std::max(maxX, x);
					maxY = std::max(maxY, y);
				}
			}
		}

		if (result.differentPixels > 0)
		{
			result.match = false;
			result.diffPercentage =
				(static_cast<float>(result.differentPixels) /
				 static_cast<float>(result.totalPixels)) * 100.0f;
			result.boundingBox = sgc::Rectf{
				static_cast<float>(minX),
				static_cast<float>(minY),
				static_cast<float>(maxX - minX + 1),
				static_cast<float>(maxY - minY + 1)};
		}

		return result;
	}

	/// @brief 閾値ベースの一致判定を行う
	/// @param expected 期待画像
	/// @param actual 実際の画像
	/// @param maxDiffPercent 許容する差異ピクセルの最大割合（%）
	/// @param tolerance チャンネルごとの許容差分値
	/// @return diffPercentage <= maxDiffPercent であればtrue
	[[nodiscard]] bool matches(
		const render::ScreenshotData& expected,
		const render::ScreenshotData& actual,
		float maxDiffPercent = 0.1f,
		int tolerance = 2) const
	{
		const auto result = compare(expected, actual, tolerance);
		return result.diffPercentage <= maxDiffPercent;
	}

	/// @brief 差分を可視化した画像を生成する
	/// @param expected 期待画像
	/// @param actual 実際の画像
	/// @param tolerance チャンネルごとの許容差分値
	/// @return 差分箇所を赤で強調した画像（actual をベースにコピー）
	/// @note サイズが異なる場合は空のScreenshotDataを返す
	[[nodiscard]] render::ScreenshotData generateDiffImage(
		const render::ScreenshotData& expected,
		const render::ScreenshotData& actual,
		int tolerance = 0) const
	{
		if (!expected.isValid() || !actual.isValid())
		{
			return {};
		}

		if (expected.width != actual.width || expected.height != actual.height)
		{
			return {};
		}

		render::ScreenshotData diffImage;
		diffImage.width = actual.width;
		diffImage.height = actual.height;
		diffImage.frameNumber = actual.frameNumber;
		diffImage.pixels = actual.pixels;

		const int w = actual.width;
		const int h = actual.height;

		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				const int dr = std::abs(
					static_cast<int>(expected.pixelR(x, y)) -
					static_cast<int>(actual.pixelR(x, y)));
				const int dg = std::abs(
					static_cast<int>(expected.pixelG(x, y)) -
					static_cast<int>(actual.pixelG(x, y)));
				const int db = std::abs(
					static_cast<int>(expected.pixelB(x, y)) -
					static_cast<int>(actual.pixelB(x, y)));
				const int da = std::abs(
					static_cast<int>(expected.pixelA(x, y)) -
					static_cast<int>(actual.pixelA(x, y)));

				const int channelMax = std::max({dr, dg, db, da});

				if (channelMax > tolerance)
				{
					const auto idx = static_cast<std::size_t>((y * w + x) * 4);
					diffImage.pixels[idx + 0] = 255; // R
					diffImage.pixels[idx + 1] = 0;   // G
					diffImage.pixels[idx + 2] = 0;   // B
					diffImage.pixels[idx + 3] = 255; // A
				}
			}
		}

		return diffImage;
	}
};

} // namespace mitiru::validate
