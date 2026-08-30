#pragma once

/// @file AutoScreenshot.hpp
/// @brief 自動スクリーンショット検証。フレーム描画後にバックバッファをキャプチャして分析
/// @details Engine.capture()を使ってDX11/DX12両対応。
///          ピクセル分析、BMP保存、描画異常検出を提供。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace mitiru::validate
{

/// @brief スクリーンショット分析結果
struct ScreenshotAnalysis
{
	int width = 0;
	int height = 0;
	int totalPixels = 0;
	int nonBlackPixels = 0;
	float nonBlackPercent = 0;
	int uniqueColors = 0;

	/// @brief エリア別サンプル
	struct Sample { int x = 0, y = 0, r = 0, g = 0, b = 0; };
	Sample topCenter{};
	Sample center{};
	Sample bottomLeft{};
	Sample bottomRight{};

	bool valid = false;
	std::string issues;

	/// @brief ピクセルデータを分析する
	/// @param pixels RGBA8形式のピクセルデータ
	/// @param w 画像幅
	/// @param h 画像高さ
	void analyze(const std::vector<uint8_t>& pixels, int w, int h)
	{
		if (w <= 0 || h <= 0) return;
		const auto expectedSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
		if (pixels.size() < expectedSize) return;

		width = w;
		height = h;
		totalPixels = w * h;
		valid = true;

		nonBlackPixels = 0;
		std::unordered_set<uint32_t> colorSet;

		for (int i = 0; i < totalPixels; ++i)
		{
			const auto idx = static_cast<size_t>(i) * 4;
			const uint8_t r = pixels[idx];
			const uint8_t g = pixels[idx + 1];
			const uint8_t b = pixels[idx + 2];

			if (r > 10 || g > 10 || b > 10)
			{
				++nonBlackPixels;
			}

			const uint32_t c = (static_cast<uint32_t>(r) << 16) |
				(static_cast<uint32_t>(g) << 8) |
				static_cast<uint32_t>(b);
			colorSet.insert(c);
		}
		uniqueColors = static_cast<int>(colorSet.size());
		nonBlackPercent = 100.0f * static_cast<float>(nonBlackPixels) /
			static_cast<float>(totalPixels);

		auto samp = [&](int x, int y) -> Sample {
			const auto si = static_cast<size_t>((y * w + x)) * 4;
			return {x, y, pixels[si], pixels[si + 1], pixels[si + 2]};
		};
		topCenter = samp(w / 2, h / 4);
		center = samp(w / 2, h / 2);
		bottomLeft = samp(w / 4, h * 3 / 4);
		bottomRight = samp(w * 3 / 4, h * 3 / 4);

		issues.clear();
		if (nonBlackPercent < 5.0f) issues += "WARN:mostly_black ";
		if (uniqueColors <= 1) issues += "WARN:single_color ";
		if (uniqueColors < 5) issues += "WARN:flat_image ";
	}

	/// @brief 分析結果を文字列として取得する
	/// @return フォーマット済みサマリ文字列
	[[nodiscard]] std::string summary() const
	{
		char buf[256];
		std::snprintf(buf, sizeof(buf),
			"Screenshot %dx%d: NonBlack=%d/%d (%.1f%%), UniqueColors=%d%s",
			width, height, nonBlackPixels, totalPixels, nonBlackPercent,
			uniqueColors, issues.empty() ? "" : (", Issues: " + issues).c_str());
		return std::string(buf);
	}

	/// @brief 描画が正常かどうかを判定する
	/// @return 重大な問題がなければtrue
	[[nodiscard]] bool isHealthy() const noexcept
	{
		return valid && nonBlackPercent >= 5.0f && uniqueColors >= 5;
	}

	/// @brief RGBA8ピクセルデータをBMPファイルに保存する
	/// @param path 出力ファイルパス
	/// @param pixels RGBA8形式のピクセルデータ
	/// @param w 画像幅
	/// @param h 画像高さ
	/// @return 保存に成功した場合true
	static bool saveBMP(const char* path,
		const std::vector<uint8_t>& pixels, int w, int h)
	{
		if (!path || w <= 0 || h <= 0) return false;
		const auto expectedSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
		if (pixels.size() < expectedSize) return false;

		const int rowBytes = w * 3;
		const int padBytes = (4 - (rowBytes % 4)) % 4;
		const int dataSize = (rowBytes + padBytes) * h;
		const int fileSize = 54 + dataSize;

		std::ofstream f(path, std::ios::binary);
		if (!f.is_open()) return false;

		uint8_t hdr[54] = {};
		hdr[0] = 'B'; hdr[1] = 'M';
		std::memcpy(&hdr[2], &fileSize, 4);
		int offset = 54;
		std::memcpy(&hdr[10], &offset, 4);
		int infoSize = 40;
		std::memcpy(&hdr[14], &infoSize, 4);
		std::memcpy(&hdr[18], &w, 4);
		int negH = -h;
		std::memcpy(&hdr[22], &negH, 4);
		short planes = 1;
		std::memcpy(&hdr[26], &planes, 2);
		short bpp = 24;
		std::memcpy(&hdr[28], &bpp, 2);
		std::memcpy(&hdr[34], &dataSize, 4);

		f.write(reinterpret_cast<const char*>(hdr), 54);

		std::vector<uint8_t> row(static_cast<size_t>(rowBytes + padBytes), 0);
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				const auto src = static_cast<size_t>((y * w + x)) * 4;
				row[static_cast<size_t>(x * 3)] = pixels[src + 2];
				row[static_cast<size_t>(x * 3 + 1)] = pixels[src + 1];
				row[static_cast<size_t>(x * 3 + 2)] = pixels[src];
			}
			f.write(reinterpret_cast<const char*>(row.data()),
				rowBytes + padBytes);
		}
		return f.good();
	}
};

/// @brief 自動スクリーンショットキャプチャ。Engineと連携してフレームをキャプチャ・分析・保存
/// @details 使用例:
/// @code
/// mitiru::Engine engine;
/// AutoScreenshot screenshot;
/// auto pixels = engine.capture();
/// auto result = screenshot.captureAndAnalyze(pixels, engine.screen()->width(), engine.screen()->height());
/// if (!result.isHealthy()) { /* 描画異常 */ }
/// @endcode
class AutoScreenshot
{
public:
	/// @brief キャプチャ結果
	struct CaptureResult
	{
		ScreenshotAnalysis analysis;
		std::vector<uint8_t> pixels;
		int frameNumber = 0;
		bool saved = false;
		std::string savedPath;
	};

	/// @brief 自動保存先ディレクトリを設定する
	/// @param dir 出力ディレクトリパス
	void setOutputDir(const std::string& dir) { m_outputDir = dir; }

	/// @brief 自動保存を有効/無効にする
	void setAutoSave(bool enabled) { m_autoSave = enabled; }

	/// @brief ピクセルデータをキャプチャして分析する
	/// @param pixels RGBA8形式のピクセルデータ
	/// @param w 画像幅
	/// @param h 画像高さ
	/// @return キャプチャ結果
	CaptureResult captureAndAnalyze(
		const std::vector<uint8_t>& pixels, int w, int h)
	{
		CaptureResult result;
		result.pixels = pixels;
		result.frameNumber = m_frameCounter++;
		result.analysis.analyze(pixels, w, h);

		if (m_autoSave && result.analysis.valid)
		{
			const std::string path = m_outputDir + "/frame_" +
				std::to_string(result.frameNumber) + ".bmp";
			result.saved = ScreenshotAnalysis::saveBMP(
				path.c_str(), pixels, w, h);
			if (result.saved)
			{
				result.savedPath = path;
			}
		}

		m_history.push_back(result.analysis);
		return result;
	}

	/// @brief Nフレーム連続でキャプチャ・分析し、全て正常かチェックする
	/// @param captureFunc キャプチャ関数（各フレームで呼び出される）
	/// @param w 画像幅
	/// @param h 画像高さ
	/// @param frameCount チェックするフレーム数
	/// @return 全フレーム正常ならtrue
	bool validateFrames(
		std::function<std::vector<uint8_t>()> captureFunc,
		int w, int h, int frameCount = 3)
	{
		for (int i = 0; i < frameCount; ++i)
		{
			auto pixels = captureFunc();
			auto result = captureAndAnalyze(pixels, w, h);
			if (!result.analysis.isHealthy())
			{
				return false;
			}
		}
		return true;
	}

	/// @brief 直近の分析履歴を取得する
	[[nodiscard]] const std::vector<ScreenshotAnalysis>& history() const noexcept
	{
		return m_history;
	}

	/// @brief 履歴をクリアする
	void clearHistory() { m_history.clear(); }

private:
	std::string m_outputDir = "screenshots";
	bool m_autoSave = false;
	int m_frameCounter = 0;
	std::vector<ScreenshotAnalysis> m_history;
};

} // namespace mitiru::validate
