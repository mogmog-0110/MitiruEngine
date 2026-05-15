#pragma once

/// @file VisualRegressionTest.hpp
/// @brief ビジュアルリグレッションテストフレームワーク
/// @details ゴールデンイメージと現在のスクリーンショットを比較し、
///          視覚的な回帰を検出する。ScreenshotDifferを内部で使用する。

#include <map>
#include <string>
#include <vector>

#include <mitiru/validate/ScreenshotDiffer.hpp>
#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::validate
{

/// @brief 単一テストのリグレッション結果
struct RegressionResult
{
	std::string testName;          ///< テスト名
	bool passed = true;            ///< テスト合格フラグ
	float diffPercentage = 0.0f;   ///< 差異ピクセルの割合（%）
	std::string message;           ///< 結果メッセージ

	/// @brief 結果をJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";
		json += "\"testName\":\"" + observe::jsonEscape(testName) + "\"";
		json += ",\"passed\":" + std::string(passed ? "true" : "false");
		json += ",\"diffPercentage\":" + std::to_string(diffPercentage);
		json += ",\"message\":\"" + observe::jsonEscape(message) + "\"";
		json += "}";
		return json;
	}
};

/// @brief ビジュアルリグレッションテスト管理クラス
/// @details ゴールデンイメージを登録し、テスト対象のスクリーンショットと比較する。
///
/// @code
/// mitiru::validate::VisualRegressionTest vrt;
/// vrt.setThreshold(1.0f);
/// vrt.setGoldenImage("main_menu", goldenScreenshot);
/// auto result = vrt.test("main_menu", currentScreenshot);
/// if (!result.passed) { /* regression detected */ }
/// @endcode
class VisualRegressionTest
{
	ScreenshotDiffer m_differ;                              ///< 内部比較器
	std::map<std::string, render::ScreenshotData> m_goldenImages; ///< 登録済みゴールデンイメージ
	float m_threshold = 0.5f;                               ///< 許容差異割合（%）
	int m_tolerance = 2;                                    ///< チャンネルごとの許容差分値

public:
	/// @brief 許容差異割合を設定する
	/// @param percent 許容する差異ピクセルの最大割合（%）
	void setThreshold(float percent)
	{
		m_threshold = percent;
	}

	/// @brief チャンネルごとの許容差分値を設定する
	/// @param tolerance 許容差分値（0〜255）
	void setTolerance(int tolerance)
	{
		m_tolerance = tolerance;
	}

	/// @brief ゴールデンイメージを登録する
	/// @param name テスト名（識別キー）
	/// @param image ゴールデンイメージデータ
	void setGoldenImage(const std::string& name, render::ScreenshotData image)
	{
		m_goldenImages.insert_or_assign(name, std::move(image));
	}

	/// @brief 指定テスト名のスクリーンショットをゴールデンイメージと比較する
	/// @param name テスト名（登録済みゴールデンイメージのキー）
	/// @param current テスト対象のスクリーンショット
	/// @return リグレッション結果
	/// @note ゴールデンイメージが未登録の場合は不合格として返す
	[[nodiscard]] RegressionResult test(
		const std::string& name,
		const render::ScreenshotData& current) const
	{
		RegressionResult result;
		result.testName = name;

		const auto it = m_goldenImages.find(name);
		if (it == m_goldenImages.end())
		{
			result.passed = false;
			result.message = "No golden image registered for '" + name + "'";
			return result;
		}

		const auto diff = m_differ.compare(it->second, current, m_tolerance);
		result.diffPercentage = diff.diffPercentage;

		if (diff.diffPercentage <= m_threshold)
		{
			result.passed = true;
			result.message = "Visual match within threshold (" +
				std::to_string(diff.diffPercentage) + "% <= " +
				std::to_string(m_threshold) + "%)";
		}
		else
		{
			result.passed = false;
			result.message = "Visual regression detected: " +
				std::to_string(diff.differentPixels) + " pixels differ (" +
				std::to_string(diff.diffPercentage) + "% > " +
				std::to_string(m_threshold) + "%)";
		}

		return result;
	}

	/// @brief 全登録ゴールデンイメージに対してテストを実行する
	/// @param currentImages テスト名をキーとする現在のスクリーンショット群
	/// @return 全テストのリグレッション結果リスト
	[[nodiscard]] std::vector<RegressionResult> testAll(
		const std::map<std::string, render::ScreenshotData>& currentImages) const
	{
		std::vector<RegressionResult> results;

		for (const auto& [name, golden] : m_goldenImages)
		{
			const auto it = currentImages.find(name);
			if (it == currentImages.end())
			{
				RegressionResult result;
				result.testName = name;
				result.passed = false;
				result.message = "No current screenshot provided for '" + name + "'";
				results.push_back(std::move(result));
				continue;
			}

			results.push_back(test(name, it->second));
		}

		return results;
	}

	/// @brief ゴールデンイメージが登録されているか確認する
	/// @param name テスト名
	/// @return 登録済みであればtrue
	[[nodiscard]] bool hasGoldenImage(const std::string& name) const
	{
		return m_goldenImages.find(name) != m_goldenImages.end();
	}

	/// @brief 登録済みゴールデンイメージのテスト名一覧を取得する
	/// @return テスト名のベクタ
	[[nodiscard]] std::vector<std::string> goldenImageNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_goldenImages.size());
		for (const auto& [name, img] : m_goldenImages)
		{
			names.push_back(name);
		}
		return names;
	}

	/// @brief 結果リストをJSON配列文字列に変換する
	/// @param results リグレッション結果リスト
	/// @return JSON配列文字列
	[[nodiscard]] std::string toJson(
		const std::vector<RegressionResult>& results) const
	{
		std::string json = "[";
		for (std::size_t i = 0; i < results.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += results[i].toJson();
		}
		json += "]";
		return json;
	}
};

} // namespace mitiru::validate
