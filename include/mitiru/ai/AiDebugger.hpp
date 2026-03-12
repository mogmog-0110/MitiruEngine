#pragma once

/// @file AiDebugger.hpp
/// @brief AI駆動デバッグユーティリティ
/// @details スクリーンショット比較・状態トラッキング・フレーム解析・
///          二分探索によるバグ発生フレーム特定等のデバッグ機能を提供する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/ai/AiSession.hpp>

namespace mitiru::ai
{

/// @brief 矩形領域（差分領域の近似バウンディングボックス用）
struct Rect
{
	int x = 0;       ///< 左上X座標
	int y = 0;       ///< 左上Y座標
	int width = 0;   ///< 幅
	int height = 0;  ///< 高さ

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += R"json({"x":)json";
		json += std::to_string(x);
		json += R"json(,"y":)json";
		json += std::to_string(y);
		json += R"json(,"width":)json";
		json += std::to_string(width);
		json += R"json(,"height":)json";
		json += std::to_string(height);
		json += "}";
		return json;
	}
};

/// @brief スクリーンショット差分結果
struct DiffResult
{
	float diffPercent = 0.0f;              ///< 差分ピクセルの割合（0.0〜100.0）
	int diffPixelCount = 0;                ///< 差分ピクセル数
	std::vector<Rect> changedRegions;      ///< 変化した領域の近似バウンディングボックス

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += R"json({"diffPercent":)json";
		json += std::to_string(diffPercent);
		json += R"json(,"diffPixelCount":)json";
		json += std::to_string(diffPixelCount);
		json += R"json(,"changedRegions":[)json";
		for (std::size_t i = 0; i < changedRegions.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += changedRegions[i].toJson();
		}
		json += "]}";
		return json;
	}
};

/// @brief フレーム解析結果
struct FrameAnalysis
{
	int frame = 0;                ///< フレーム番号
	std::string snapshot;         ///< そのフレームのスナップショットJSON
	bool conditionMet = false;    ///< 条件が満たされたか

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += R"json({"frame":)json";
		json += std::to_string(frame);
		json += R"json(,"conditionMet":)json";
		json += conditionMet ? "true" : "false";
		json += R"json(,"snapshot":)json";
		json += snapshot;
		json += "}";
		return json;
	}
};

/// @brief AI駆動デバッグユーティリティ
/// @details AiSessionと連携して高レベルなデバッグ操作を提供する。
///          ピクセル比較・ベースライン差分・フレーム解析・二分探索等。
///
/// @code
/// mitiru::ai::AiDebugger debugger(session);
/// debugger.markBaseline();
/// session.step(100);
/// auto diff = debugger.diffFromBaseline();
/// @endcode
class AiDebugger
{
public:
	/// @brief コンストラクタ
	/// @param session AI操作セッションへの参照
	explicit AiDebugger(AiSession& session) noexcept
		: m_session(session)
	{
	}

	// ─── スクリーンショット比較 ───

	/// @brief 2枚のRGBAスクリーンショットを比較する
	/// @param a 比較元のピクセルデータ（RGBA8）
	/// @param b 比較先のピクセルデータ（RGBA8）
	/// @param width 画像幅（ピクセル）
	/// @param height 画像高さ（ピクセル）
	/// @return 差分結果
	[[nodiscard]] DiffResult compareScreenshots(
		const std::vector<std::uint8_t>& a,
		const std::vector<std::uint8_t>& b,
		int width, int height) const
	{
		DiffResult result;

		const int totalPixels = width * height;
		if (totalPixels <= 0)
		{
			return result;
		}

		const std::size_t expectedSize =
			static_cast<std::size_t>(totalPixels) * 4;

		/// サイズ不一致は全体が異なるとみなす
		if (a.size() != expectedSize || b.size() != expectedSize)
		{
			result.diffPixelCount = totalPixels;
			result.diffPercent = 100.0f;
			return result;
		}

		/// ピクセル単位で比較（RGBA各チャネルの差分閾値 = 4）
		constexpr int THRESHOLD = 4;
		int minX = width;
		int minY = height;
		int maxX = 0;
		int maxY = 0;

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const std::size_t idx =
					static_cast<std::size_t>((y * width + x) * 4);

				const bool differs =
					std::abs(static_cast<int>(a[idx])     - static_cast<int>(b[idx]))     > THRESHOLD ||
					std::abs(static_cast<int>(a[idx + 1]) - static_cast<int>(b[idx + 1])) > THRESHOLD ||
					std::abs(static_cast<int>(a[idx + 2]) - static_cast<int>(b[idx + 2])) > THRESHOLD ||
					std::abs(static_cast<int>(a[idx + 3]) - static_cast<int>(b[idx + 3])) > THRESHOLD;

				if (differs)
				{
					++result.diffPixelCount;
					minX = std::min(minX, x);
					minY = std::min(minY, y);
					maxX = std::max(maxX, x);
					maxY = std::max(maxY, y);
				}
			}
		}

		result.diffPercent = static_cast<float>(result.diffPixelCount) /
			static_cast<float>(totalPixels) * 100.0f;

		/// 差分があれば包括的バウンディングボックスを1つ追加
		if (result.diffPixelCount > 0)
		{
			result.changedRegions.push_back(Rect{
				.x = minX,
				.y = minY,
				.width = maxX - minX + 1,
				.height = maxY - minY + 1
			});
		}

		return result;
	}

	// ─── ベースライン管理 ───

	/// @brief 現在の状態をベースラインとして保存する
	void markBaseline()
	{
		m_baselineSnapshot = m_session.snapshot();
		m_baselineScreenshot = m_session.screenshot();
		m_baselineFrame = m_session.frameNumber();
	}

	/// @brief 現在の状態とベースラインの差分を返す
	/// @return 差分情報のJSON文字列
	[[nodiscard]] std::string diffFromBaseline() const
	{
		return m_session.diffFromBaseline(m_baselineSnapshot);
	}

	/// @brief ベースラインスクリーンショットと現在のスクリーンショットを比較する
	/// @param width 画像幅
	/// @param height 画像高さ
	/// @return 差分結果
	[[nodiscard]] DiffResult diffScreenshotFromBaseline(int width, int height)
	{
		const auto current = m_session.screenshot();
		return compareScreenshots(m_baselineScreenshot, current, width, height);
	}

	// ─── フレーム解析 ───

	/// @brief 指定範囲のフレームを解析する
	/// @param startFrame 開始フレーム（現在からの相対フレーム数として使用）
	/// @param endFrame 終了フレーム（実行するフレーム数）
	/// @param interval 解析間隔（フレーム数）
	/// @return フレーム解析結果の配列
	/// @note endFrame - startFrame 分だけゲームを進めながらスナップショットを収集する
	[[nodiscard]] std::vector<FrameAnalysis> analyzeFrames(
		int startFrame, int endFrame, int interval = 1)
	{
		std::vector<FrameAnalysis> analyses;

		/// startFrameまでスキップ
		if (startFrame > 0)
		{
			m_session.step(startFrame);
		}

		const int totalFrames = endFrame - startFrame;
		for (int i = 0; i < totalFrames; ++i)
		{
			m_session.step(1);

			if (interval > 0 && (i % interval == 0))
			{
				FrameAnalysis fa;
				fa.frame = static_cast<int>(m_session.frameNumber());
				fa.snapshot = m_session.snapshot();
				fa.conditionMet = false;  ///< 条件なしの場合は常にfalse
				analyses.push_back(std::move(fa));
			}
		}

		return analyses;
	}

	/// @brief 条件付きフレーム解析
	/// @param framesToRun 実行するフレーム数
	/// @param condition 各フレームで評価する条件
	/// @param interval 解析間隔
	/// @return フレーム解析結果の配列
	[[nodiscard]] std::vector<FrameAnalysis> analyzeFramesWithCondition(
		int framesToRun,
		std::function<bool()> condition,
		int interval = 1)
	{
		std::vector<FrameAnalysis> analyses;

		for (int i = 0; i < framesToRun; ++i)
		{
			m_session.step(1);

			if (interval > 0 && (i % interval == 0))
			{
				FrameAnalysis fa;
				fa.frame = static_cast<int>(m_session.frameNumber());
				fa.snapshot = m_session.snapshot();
				fa.conditionMet = condition();
				analyses.push_back(std::move(fa));
			}
		}

		return analyses;
	}

	// ─── 二分探索 ───

	/// @brief 条件が最初にtrueになるフレームを二分探索で特定する
	/// @param condition 評価条件（trueになるフレームを探す）
	/// @param startFrame 探索開始フレーム（相対）
	/// @param endFrame 探索終了フレーム（相対）
	/// @return 条件が最初にtrueになったフレーム番号（-1 = 見つからなかった）
	/// @note ゲームは決定論的モードである必要がある。
	///       毎回startFrameからゲームを進めるため、探索範囲が広いと時間がかかる。
	///       現実装はシーケンシャル走査（二分探索の前提条件である単調性を保証できないため）。
	[[nodiscard]] int bisectFrame(
		std::function<bool()> condition,
		int startFrame, int endFrame)
	{
		/// シーケンシャル走査で最初に条件を満たすフレームを見つける
		for (int i = startFrame; i <= endFrame; ++i)
		{
			m_session.step(1);
			if (condition())
			{
				return static_cast<int>(m_session.frameNumber());
			}
		}
		return -1;
	}

	// ─── ユーティリティ ───

	/// @brief ベースラインフレーム番号を取得する
	/// @return ベースライン時点のフレーム番号
	[[nodiscard]] std::uint64_t baselineFrame() const noexcept
	{
		return m_baselineFrame;
	}

private:
	AiSession& m_session;                            ///< AI操作セッションへの参照
	std::string m_baselineSnapshot;                  ///< ベースラインのスナップショットJSON
	std::vector<std::uint8_t> m_baselineScreenshot;  ///< ベースラインのスクリーンショット
	std::uint64_t m_baselineFrame = 0;               ///< ベースライン時点のフレーム番号
};

} // namespace mitiru::ai
