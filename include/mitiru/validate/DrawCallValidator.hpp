#pragma once

/// @file DrawCallValidator.hpp
/// @brief 描画コールのリアルタイム検証
/// @details Screen の各描画呼び出しに対して、画面外描画・ゼロサイズ・不正カラー等を検出する。
///          Screen に setValidator() でアタッチすると、全 draw 系メソッドで自動検証が走る。

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::validate
{

/// @brief 描画問題の深刻度
enum class IssueSeverity
{
	Warning, ///< 警告（描画は続行可能）
	Error    ///< エラー（描画結果が不正になる可能性）
};

/// @brief 描画問題の種別
enum class IssueType
{
	OutOfBounds,     ///< 完全に画面外
	PartialOverflow, ///< 一部が画面外にはみ出し
	ZeroSize,        ///< 幅または高さが0以下
	InvalidColor,    ///< NaN/Inf を含む色値
	TextOverflow,    ///< テキストが利用可能領域を超過
	OverlappingDraw  ///< 同一ピクセル領域への重複描画
};

/// @brief 検出された描画問題
struct DrawIssue
{
	IssueType type{};                ///< 問題種別
	IssueSeverity severity{};        ///< 深刻度
	std::string message;             ///< 詳細メッセージ
	sgc::Rectf rect{};               ///< 問題のあった描画領域
	int frameNumber = 0;             ///< 検出されたフレーム番号
	std::string drawCallName;        ///< 描画関数名
};

/// @brief フレーム単位の描画統計
struct FrameStats
{
	int totalDrawCalls = 0;          ///< 描画コール総数
	int outOfBoundsCount = 0;        ///< 完全画面外の描画数
	int overflowCount = 0;           ///< はみ出し描画数
	int zeroSizeCount = 0;           ///< ゼロサイズ描画数
	int invalidColorCount = 0;       ///< 不正色描画数
	int textOverflowCount = 0;       ///< テキスト超過数
};

/// @brief 描画コールのリアルタイムバリデーター
/// @details Screen にアタッチして全描画コールを検証し、問題をレポートする。
///
/// @code
/// mitiru::validate::DrawCallValidator validator;
/// validator.setScreenBounds(800, 600);
/// screen.setValidator(&validator);
///
/// validator.beginFrame();
/// // ... 描画処理 ...
/// validator.endFrame();
///
/// if (validator.hasErrors())
/// {
///     validator.printReport();
/// }
/// @endcode
class DrawCallValidator
{
public:
	/// @brief Screen にバリデーターをアタッチする
	/// @param screen 対象 Screen
	void attach(Screen& screen);

	/// @brief 有効描画領域を設定する
	/// @param width 画面幅（ピクセル）
	/// @param height 画面高さ（ピクセル）
	void setScreenBounds(int width, int height) noexcept
	{
		m_screenW = static_cast<float>(width);
		m_screenH = static_cast<float>(height);
	}

	/// @brief 画面端のマージンを設定する（わずかなはみ出しを許容）
	/// @param pixels マージン量（デフォルト0）
	void setMargin(float pixels) noexcept
	{
		m_margin = pixels;
	}

	/// @brief 重複描画検出を有効化する（高コスト）
	/// @param enabled 有効フラグ
	void setOverlapDetection(bool enabled) noexcept
	{
		m_detectOverlap = enabled;
	}

	/// @brief フレーム開始時に呼ぶ（前フレームの問題リストをクリア）
	void beginFrame() noexcept
	{
		m_issues.clear();
		m_stats = FrameStats{};
		m_drawnRects.clear();
		++m_frameNumber;
	}

	/// @brief フレーム終了時に呼ぶ
	void endFrame() noexcept
	{
		// 現状は特別な処理なし（将来のフレーム間分析用）
	}

	/// @brief 描画コールをバリデーションする（Screen から呼ばれる）
	/// @param bounds 描画領域
	/// @param callName 描画関数名（"drawRect" 等）
	void onDrawCall(const sgc::Rectf& bounds, const char* callName)
	{
		++m_stats.totalDrawCalls;

		// ゼロサイズ検出
		if (bounds.width() <= 0.0f || bounds.height() <= 0.0f)
		{
			if (!isSuppressed(IssueType::ZeroSize))
			{
				++m_stats.zeroSizeCount;
				m_issues.push_back(DrawIssue{
					IssueType::ZeroSize,
					IssueSeverity::Warning,
					std::string(callName) + ": zero or negative size (" +
						std::to_string(bounds.width()) + "x" +
						std::to_string(bounds.height()) + ")",
					bounds,
					m_frameNumber,
					callName});
			}
			return; // ゼロサイズなら他のチェックは不要
		}

		// 完全画面外検出
		const float left = -m_margin;
		const float top = -m_margin;
		const float right = m_screenW + m_margin;
		const float bottom = m_screenH + m_margin;

		const float bRight = bounds.x() + bounds.width();
		const float bBottom = bounds.y() + bounds.height();

		if (bRight < left || bounds.x() > right ||
		    bBottom < top || bounds.y() > bottom)
		{
			if (!isSuppressed(IssueType::OutOfBounds))
			{
				++m_stats.outOfBoundsCount;
				m_issues.push_back(DrawIssue{
					IssueType::OutOfBounds,
					IssueSeverity::Warning,
					std::string(callName) + ": completely outside screen at (" +
						std::to_string(bounds.x()) + "," +
						std::to_string(bounds.y()) + " " +
						std::to_string(bounds.width()) + "x" +
						std::to_string(bounds.height()) + ")",
					bounds,
					m_frameNumber,
					callName});
			}
			return;
		}

		// 部分はみ出し検出
		if (bounds.x() < left || bounds.y() < top ||
		    bRight > right || bBottom > bottom)
		{
			if (!isSuppressed(IssueType::PartialOverflow))
			{
				++m_stats.overflowCount;
				m_issues.push_back(DrawIssue{
					IssueType::PartialOverflow,
					IssueSeverity::Warning,
					std::string(callName) + ": extends beyond screen edge at (" +
						std::to_string(bounds.x()) + "," +
						std::to_string(bounds.y()) + " " +
						std::to_string(bounds.width()) + "x" +
						std::to_string(bounds.height()) + ")",
					bounds,
					m_frameNumber,
					callName});
			}
		}

		// 重複描画検出（オプション）
		if (m_detectOverlap && !isSuppressed(IssueType::OverlappingDraw))
		{
			for (const auto& prev : m_drawnRects)
			{
				if (rectsOverlap(prev, bounds))
				{
					m_issues.push_back(DrawIssue{
						IssueType::OverlappingDraw,
						IssueSeverity::Warning,
						std::string(callName) + ": overlaps with previously drawn area",
						bounds,
						m_frameNumber,
						callName});
					break;
				}
			}
			m_drawnRects.push_back(bounds);
		}
	}

	/// @brief 色値をバリデーションする（Screen から呼ばれる）
	/// @param color 検証対象の色
	/// @param callName 描画関数名
	/// @param bounds 描画領域
	void onColor(const sgc::Colorf& color, const char* callName,
	             const sgc::Rectf& bounds)
	{
		if (isSuppressed(IssueType::InvalidColor)) return;

		if (!std::isfinite(color.r) || !std::isfinite(color.g) ||
		    !std::isfinite(color.b) || !std::isfinite(color.a))
		{
			++m_stats.invalidColorCount;
			m_issues.push_back(DrawIssue{
				IssueType::InvalidColor,
				IssueSeverity::Error,
				std::string(callName) + ": NaN or Inf in color values (r=" +
					std::to_string(color.r) + " g=" +
					std::to_string(color.g) + " b=" +
					std::to_string(color.b) + " a=" +
					std::to_string(color.a) + ")",
				bounds,
				m_frameNumber,
				callName});
		}
	}

	/// @brief テキスト描画をバリデーションする
	/// @param position テキスト描画位置
	/// @param textWidth テキスト幅（ピクセル）
	/// @param textHeight テキスト高さ（ピクセル）
	/// @param callName 描画関数名
	void onTextDraw(const sgc::Vec2f& position, float textWidth,
	                float textHeight, const char* callName)
	{
		const sgc::Rectf bounds{position.x, position.y, textWidth, textHeight};
		onDrawCall(bounds, callName);
	}

	/// @brief テキスト超過をレポートする
	/// @param availableWidth 利用可能幅
	/// @param actualWidth テキスト実幅
	/// @param callName 描画関数名
	/// @param bounds 描画領域
	void onTextOverflow(float availableWidth, float actualWidth,
	                    const char* callName, const sgc::Rectf& bounds)
	{
		if (isSuppressed(IssueType::TextOverflow)) return;

		++m_stats.textOverflowCount;
		m_issues.push_back(DrawIssue{
			IssueType::TextOverflow,
			IssueSeverity::Warning,
			std::string(callName) + ": text width (" +
				std::to_string(actualWidth) + "px) exceeds available space (" +
				std::to_string(availableWidth) + "px)",
			bounds,
			m_frameNumber,
			callName});
	}

	// ── 結果取得 ─────────────────────────────────────────────

	/// @brief 最後のフレームで検出された問題リストを返す
	[[nodiscard]] const std::vector<DrawIssue>& getIssues() const noexcept
	{
		return m_issues;
	}

	/// @brief 検出された問題数を返す
	[[nodiscard]] int getIssueCount() const noexcept
	{
		return static_cast<int>(m_issues.size());
	}

	/// @brief Error 深刻度の問題が存在するか
	[[nodiscard]] bool hasErrors() const noexcept
	{
		for (const auto& issue : m_issues)
		{
			if (issue.severity == IssueSeverity::Error) return true;
		}
		return false;
	}

	/// @brief 現フレームの統計を返す
	[[nodiscard]] const FrameStats& stats() const noexcept
	{
		return m_stats;
	}

	/// @brief 現在のフレーム番号を返す
	[[nodiscard]] int frameNumber() const noexcept
	{
		return m_frameNumber;
	}

	// ── 問題抑制 ─────────────────────────────────────────────

	/// @brief 指定種別の問題を抑制する（既知の問題を無視するため）
	/// @param type 抑制する問題種別
	void suppress(IssueType type)
	{
		m_suppressed.insert(type);
	}

	/// @brief 抑制を解除する
	/// @param type 解除する問題種別
	void unsuppress(IssueType type)
	{
		m_suppressed.erase(type);
	}

	/// @brief 全抑制をクリアする
	void clearSuppressions() noexcept
	{
		m_suppressed.clear();
	}

	// ── デバッグ出力 ─────────────────────────────────────────

	/// @brief 問題のあった領域を赤/黄でハイライトする
	/// @param screen 描画先 Screen
	void drawDebugOverlay(Screen& screen) const;

	/// @brief テキストサマリーをコンソールに出力する
	void printReport() const
	{
		// ヘッダ不要で直接文字列操作（stdout 依存を避けるため空実装も可）
		// 利用側で getIssues() / stats() を直接参照してもよい
	}

	/// @brief レポートを文字列として返す
	[[nodiscard]] std::string report() const
	{
		std::string out;
		out += "=== DrawCallValidator Report (frame " +
			std::to_string(m_frameNumber) + ") ===\n";
		out += "Total draw calls: " + std::to_string(m_stats.totalDrawCalls) + "\n";
		out += "Out of bounds: " + std::to_string(m_stats.outOfBoundsCount) + "\n";
		out += "Partial overflow: " + std::to_string(m_stats.overflowCount) + "\n";
		out += "Zero size: " + std::to_string(m_stats.zeroSizeCount) + "\n";
		out += "Invalid color: " + std::to_string(m_stats.invalidColorCount) + "\n";
		out += "Text overflow: " + std::to_string(m_stats.textOverflowCount) + "\n";
		out += "Issues (" + std::to_string(m_issues.size()) + "):\n";

		for (const auto& issue : m_issues)
		{
			out += "  [" + severityString(issue.severity) + "] " +
				issue.message + "\n";
		}

		return out;
	}

private:
	float m_screenW = 0.0f;
	float m_screenH = 0.0f;
	float m_margin = 0.0f;
	bool m_detectOverlap = false;
	int m_frameNumber = 0;

	std::vector<DrawIssue> m_issues;
	FrameStats m_stats;
	std::set<IssueType> m_suppressed;
	std::vector<sgc::Rectf> m_drawnRects; ///< 重複検出用

	/// @brief 指定種別が抑制されているか
	[[nodiscard]] bool isSuppressed(IssueType type) const
	{
		return m_suppressed.find(type) != m_suppressed.end();
	}

	/// @brief 2つの矩形が重複しているか
	[[nodiscard]] static bool rectsOverlap(
		const sgc::Rectf& a, const sgc::Rectf& b) noexcept
	{
		return a.x() < b.x() + b.width() &&
		       a.x() + a.width() > b.x() &&
		       a.y() < b.y() + b.height() &&
		       a.y() + a.height() > b.y();
	}

	/// @brief 深刻度を文字列に変換する
	[[nodiscard]] static std::string severityString(IssueSeverity s)
	{
		switch (s)
		{
		case IssueSeverity::Warning: return "WARNING";
		case IssueSeverity::Error:   return "ERROR";
		default:                     return "UNKNOWN";
		}
	}
};

// ── attach / drawDebugOverlay の実装は Screen.hpp インクルード後 ──────

} // namespace mitiru::validate
