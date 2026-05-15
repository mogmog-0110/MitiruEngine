#pragma once

/// @file AutoScreenshotTest.hpp
/// @brief 自動スクリーンショットテストフレームワーク
/// @details 登録されたデモ/サンプルを順次実行し、DrawCallValidator による検証と
///          基本的なビジュアルチェック（真っ黒、単色、空白領域等）を自動で行う。

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/core/Screen.hpp>
#include <mitiru/validate/DrawCallValidator.hpp>
#include <mitiru/validate/ScreenshotDiffer.hpp>
#include <mitiru/render/ScreenCapture.hpp>

namespace mitiru::validate
{

/// @brief デモのインターフェース（テスト対象が実装する）
struct ITestableDemo
{
	virtual ~ITestableDemo() = default;

	/// @brief 1フレーム分の更新処理
	/// @param deltaTime フレーム時間（秒）
	virtual void update(float deltaTime) = 0;

	/// @brief 1フレーム分の描画処理
	/// @param screen 描画先
	virtual void draw(Screen& screen) = 0;
};

/// @brief デモ生成ファクトリ型
using DemoFactory = std::function<std::unique_ptr<ITestableDemo>()>;

/// @brief 個別デモのテスト結果
struct DemoTestResult
{
	std::string name;                          ///< デモ名
	bool passed = true;                        ///< 合格フラグ
	std::vector<DrawIssue> issues;             ///< 検出された描画問題
	std::vector<std::string> validationErrors; ///< ビジュアルチェックのエラー
	std::string screenshotPath;                ///< 保存先パス（空なら未保存）
	int drawCallCount = 0;                     ///< フレーム内描画コール数
	int framesTested = 0;                      ///< テストしたフレーム数

	/// @brief 結果をJSON文字列に変換する
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";
		json += "\"name\":\"" + name + "\"";
		json += ",\"passed\":" + std::string(passed ? "true" : "false");
		json += ",\"drawCallCount\":" + std::to_string(drawCallCount);
		json += ",\"framesTested\":" + std::to_string(framesTested);
		json += ",\"issueCount\":" + std::to_string(issues.size());
		json += ",\"validationErrors\":[";
		for (std::size_t i = 0; i < validationErrors.size(); ++i)
		{
			if (i > 0) json += ",";
			json += "\"" + validationErrors[i] + "\"";
		}
		json += "]}";
		return json;
	}
};

/// @brief テスト全体のレポート
struct TestReport
{
	std::vector<DemoTestResult> results;   ///< 各デモの結果
	int totalPassed = 0;                   ///< 合格数
	int totalFailed = 0;                   ///< 不合格数
	std::string outputDirectory;           ///< スクリーンショット保存先

	/// @brief 結果をJSON文字列に変換する
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";
		json += "\"totalPassed\":" + std::to_string(totalPassed);
		json += ",\"totalFailed\":" + std::to_string(totalFailed);
		json += ",\"outputDirectory\":\"" + outputDirectory + "\"";
		json += ",\"results\":[";
		for (std::size_t i = 0; i < results.size(); ++i)
		{
			if (i > 0) json += ",";
			json += results[i].toJson();
		}
		json += "]}";
		return json;
	}
};

/// @brief 自動スクリーンショットテスト
/// @details デモを登録し、一括テストを実行する。
///
/// @code
/// mitiru::validate::AutoScreenshotTest tester;
/// tester.registerDemo("MainMenu", []{ return std::make_unique<MainMenuDemo>(); });
/// tester.registerDemo("GamePlay", []{ return std::make_unique<GamePlayDemo>(); });
/// auto report = tester.runAll(800, 600);
/// @endcode
class AutoScreenshotTest
{
public:
	/// @brief デモを登録する
	/// @param name デモの識別名
	/// @param factory デモ生成ファクトリ
	void registerDemo(const std::string& name, DemoFactory factory)
	{
		m_demos.push_back({name, std::move(factory)});
	}

	/// @brief ゴールデンイメージを登録する
	/// @param name デモ名
	/// @param golden ゴールデンスクリーンショット
	void setGoldenImage(const std::string& name, render::ScreenshotData golden)
	{
		m_goldenImages.insert_or_assign(name, std::move(golden));
	}

	/// @brief ゴールデンイメージ比較の閾値を設定する
	/// @param percent 許容差異割合（%）
	void setDiffThreshold(float percent) noexcept
	{
		m_diffThreshold = percent;
	}

	/// @brief テスト時のフレーム数を設定する
	/// @param frames フレーム数（デフォルト60）
	void setFramesPerDemo(int frames) noexcept
	{
		m_framesPerDemo = frames;
	}

	/// @brief 出力ディレクトリを設定する
	/// @param dir ディレクトリパス
	void setOutputDirectory(const std::string& dir)
	{
		m_outputDir = dir;
	}

	/// @brief 全登録デモのテストを実行する
	/// @param screenW 画面幅
	/// @param screenH 画面高さ
	/// @param framesPerDemo デモごとのフレーム数（0ならデフォルト値使用）
	/// @return テストレポート
	[[nodiscard]] TestReport runAll(int screenW, int screenH,
	                                int framesPerDemo = 0) const
	{
		const int frames = (framesPerDemo > 0) ? framesPerDemo : m_framesPerDemo;

		TestReport report;
		report.outputDirectory = m_outputDir;

		for (const auto& [name, factory] : m_demos)
		{
			report.results.push_back(
				runSingleDemo(name, factory, screenW, screenH, frames));
		}

		for (const auto& result : report.results)
		{
			if (result.passed) ++report.totalPassed;
			else ++report.totalFailed;
		}

		return report;
	}

	/// @brief 登録済みデモ数を返す
	[[nodiscard]] std::size_t demoCount() const noexcept
	{
		return m_demos.size();
	}

private:
	struct DemoEntry
	{
		std::string name;
		DemoFactory factory;
	};

	std::vector<DemoEntry> m_demos;
	std::map<std::string, render::ScreenshotData> m_goldenImages;
	float m_diffThreshold = 0.5f;
	int m_framesPerDemo = 60;
	std::string m_outputDir = "test_screenshots";

	/// @brief 単一デモをテストする
	[[nodiscard]] DemoTestResult runSingleDemo(
		const std::string& name,
		const DemoFactory& factory,
		int screenW, int screenH,
		int frames) const
	{
		DemoTestResult result;
		result.name = name;
		result.framesTested = frames;

		// デモインスタンスを生成
		auto demo = factory();
		if (!demo)
		{
			result.passed = false;
			result.validationErrors.push_back("Failed to create demo instance");
			return result;
		}

		// Screen とバリデーターを準備
		Screen screen(screenW, screenH);
		screen.enableSoftwareFramebuffer();

		DrawCallValidator validator;
		validator.setScreenBounds(screenW, screenH);
		screen.setValidator(&validator);

		// フレームを実行
		const float dt = 1.0f / 60.0f;
		for (int f = 0; f < frames; ++f)
		{
			validator.beginFrame();
			screen.resetDrawCallCount();

			screen.clear();
			demo->update(dt);
			demo->draw(screen);
			screen.present();

			validator.endFrame();
		}

		result.drawCallCount = screen.drawCallCount();

		// 最終フレームの描画問題を収集
		result.issues = validator.getIssues();

		// ── ビジュアルチェック ──────────────────────────────

		const auto& pixels = screen.pixels();
		const int totalPixels = screenW * screenH;

		if (totalPixels > 0 && !pixels.empty())
		{
			// チェック 1: 真っ黒でないか（描画が行われたか）
			{
				bool allBlack = true;
				for (int i = 0; i < totalPixels && allBlack; ++i)
				{
					const auto idx = static_cast<std::size_t>(i * 4);
					if (pixels[idx] != 0 || pixels[idx + 1] != 0 || pixels[idx + 2] != 0)
					{
						allBlack = false;
					}
				}
				if (allBlack)
				{
					result.validationErrors.push_back(
						"Screen is completely black - rendering may have failed");
				}
			}

			// チェック 2: 全ピクセルが同一色でないか
			{
				const auto r0 = pixels[0];
				const auto g0 = pixels[1];
				const auto b0 = pixels[2];
				bool allSame = true;
				for (int i = 1; i < totalPixels && allSame; ++i)
				{
					const auto idx = static_cast<std::size_t>(i * 4);
					if (pixels[idx] != r0 || pixels[idx + 1] != g0 || pixels[idx + 2] != b0)
					{
						allSame = false;
					}
				}
				if (allSame && totalPixels > 1)
				{
					result.validationErrors.push_back(
						"Screen is entirely one color - only background visible");
				}
			}

			// チェック 3: 中央に空白領域がないか
			{
				const int cx = screenW / 2;
				const int cy = screenH / 2;
				const int checkRadius = std::min(screenW, screenH) / 6;
				bool centerEmpty = true;

				// 背景色を左上角から推定
				const auto bgR = pixels[0];
				const auto bgG = pixels[1];
				const auto bgB = pixels[2];

				for (int dy = -checkRadius; dy <= checkRadius && centerEmpty; ++dy)
				{
					for (int dx = -checkRadius; dx <= checkRadius && centerEmpty; ++dx)
					{
						const int px = cx + dx;
						const int py = cy + dy;
						if (px < 0 || px >= screenW || py < 0 || py >= screenH) continue;
						const auto idx = static_cast<std::size_t>((py * screenW + px) * 4);
						if (pixels[idx] != bgR || pixels[idx + 1] != bgG || pixels[idx + 2] != bgB)
						{
							centerEmpty = false;
						}
					}
				}
				if (centerEmpty && totalPixels > 100)
				{
					result.validationErrors.push_back(
						"Center of screen is empty - possible layout issue");
				}
			}
		}

		// ── ゴールデンイメージ比較 ──────────────────────────

		const auto goldenIt = m_goldenImages.find(name);
		if (goldenIt != m_goldenImages.end() && !pixels.empty())
		{
			render::ScreenshotData currentShot;
			currentShot.width = screenW;
			currentShot.height = screenH;
			currentShot.pixels = pixels;

			ScreenshotDiffer differ;
			const auto diff = differ.compare(goldenIt->second, currentShot, 2);
			if (diff.diffPercentage > m_diffThreshold)
			{
				result.validationErrors.push_back(
					"Golden image mismatch: " +
					std::to_string(diff.diffPercentage) + "% differs (threshold: " +
					std::to_string(m_diffThreshold) + "%)");
			}
		}

		// ── 合否判定 ────────────────────────────────────────

		// Error 深刻度の問題があれば不合格
		for (const auto& issue : result.issues)
		{
			if (issue.severity == IssueSeverity::Error)
			{
				result.passed = false;
				break;
			}
		}

		// ビジュアルチェックのエラーがあれば不合格
		if (!result.validationErrors.empty())
		{
			result.passed = false;
		}

		// バリデーターを解除
		screen.setValidator(nullptr);

		return result;
	}
};

} // namespace mitiru::validate
