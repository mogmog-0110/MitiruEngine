#pragma once

/// @file UILayoutValidator.hpp
/// @brief UIレイアウト検証ユーティリティ
/// @details UI要素の配置を検証し、画面外はみ出し・重複・最小サイズ違反を検出する。

#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::validate
{

/// @brief 検証対象のUI要素情報
struct UIElement
{
	std::string id;        ///< 要素の識別子
	std::string role;      ///< 要素の役割（"health_bar", "score_label" 等）
	sgc::Rectf bounds;     ///< スクリーン空間でのバウンディング矩形
};

/// @brief レイアウト検証で検出された問題
struct LayoutIssue
{
	/// @brief 問題の種別
	enum Type
	{
		OutOfBounds, ///< 要素が画面外に完全にはみ出している
		Overlap,     ///< 2つの要素が重複している
		TooSmall,    ///< 要素が最小サイズ未満
		Clipped      ///< 要素が画面境界で部分的にクリップされている
	};

	Type type;                  ///< 問題の種別
	std::string elementId;      ///< 対象要素のID
	std::string otherElementId; ///< 重複相手のID（Overlapの場合のみ）
	std::string description;    ///< 問題の説明文

	/// @brief 問題情報をJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		auto typeStr = [](Type t) -> const char* {
			switch (t)
			{
			case OutOfBounds: return "OutOfBounds";
			case Overlap:     return "Overlap";
			case TooSmall:    return "TooSmall";
			case Clipped:     return "Clipped";
			}
			return "Unknown";
		};

		std::string json = "{";
		json += "\"type\":\"" + std::string(typeStr(type)) + "\"";
		json += ",\"elementId\":\"" + observe::jsonEscape(elementId) + "\"";
		if (!otherElementId.empty())
		{
			json += ",\"otherElementId\":\"" + observe::jsonEscape(otherElementId) + "\"";
		}
		json += ",\"description\":\"" + observe::jsonEscape(description) + "\"";
		json += "}";
		return json;
	}
};

/// @brief UIレイアウト検証器
/// @details UI要素群を検証し、画面外・重複・最小サイズ違反・クリッピングを検出する。
///
/// @code
/// mitiru::validate::UILayoutValidator validator(1280.0f, 720.0f);
/// std::vector<mitiru::validate::UIElement> elements = { ... };
/// auto issues = validator.validate(elements);
/// @endcode
class UILayoutValidator
{
	float m_screenW;                ///< 画面幅
	float m_screenH;                ///< 画面高さ
	float m_minElementSize = 8.0f;  ///< 最小許容サイズ（幅・高さ）

public:
	/// @brief 画面サイズを指定して構築する
	/// @param screenW 画面幅（ピクセル）
	/// @param screenH 画面高さ（ピクセル）
	UILayoutValidator(float screenW, float screenH)
		: m_screenW(screenW)
		, m_screenH(screenH)
	{
	}

	/// @brief 最小許容サイズを設定する
	/// @param size 最小許容幅・高さ（ピクセル）
	void setMinElementSize(float size)
	{
		m_minElementSize = size;
	}

	/// @brief 全要素を検証する
	/// @param elements 検証対象のUI要素群
	/// @return 検出された問題のリスト
	/// @details 各要素の画面範囲チェック・最小サイズチェック、および全ペアの重複チェックを行う。
	[[nodiscard]] std::vector<LayoutIssue> validate(
		const std::vector<UIElement>& elements) const
	{
		std::vector<LayoutIssue> issues;

		for (const auto& elem : elements)
		{
			auto boundsIssues = checkBounds(elem);
			issues.insert(issues.end(), boundsIssues.begin(), boundsIssues.end());

			if (elem.bounds.width() < m_minElementSize ||
				elem.bounds.height() < m_minElementSize)
			{
				LayoutIssue issue;
				issue.type = LayoutIssue::TooSmall;
				issue.elementId = elem.id;
				issue.description =
					"Element '" + elem.id + "' is too small (" +
					std::to_string(elem.bounds.width()) + "x" +
					std::to_string(elem.bounds.height()) + "), minimum is " +
					std::to_string(m_minElementSize);
				issues.push_back(issue);
			}
		}

		auto overlapIssues = checkOverlaps(elements);
		issues.insert(issues.end(), overlapIssues.begin(), overlapIssues.end());

		return issues;
	}

	/// @brief 単一要素の画面範囲チェックを行う
	/// @param elem 検証対象のUI要素
	/// @return 検出された問題のリスト（OutOfBoundsまたはClipped）
	[[nodiscard]] std::vector<LayoutIssue> checkBounds(const UIElement& elem) const
	{
		std::vector<LayoutIssue> issues;

		const sgc::Rectf screen{0.0f, 0.0f, m_screenW, m_screenH};
		const auto& b = elem.bounds;

		const bool completelyOutside =
			b.right() <= 0.0f || b.left() >= m_screenW ||
			b.bottom() <= 0.0f || b.top() >= m_screenH;

		if (completelyOutside)
		{
			LayoutIssue issue;
			issue.type = LayoutIssue::OutOfBounds;
			issue.elementId = elem.id;
			issue.description =
				"Element '" + elem.id + "' is completely outside the screen";
			issues.push_back(issue);
			return issues;
		}

		const bool partiallyOutside =
			b.left() < 0.0f || b.top() < 0.0f ||
			b.right() > m_screenW || b.bottom() > m_screenH;

		if (partiallyOutside)
		{
			LayoutIssue issue;
			issue.type = LayoutIssue::Clipped;
			issue.elementId = elem.id;
			issue.description =
				"Element '" + elem.id + "' is partially outside the screen";
			issues.push_back(issue);
		}

		return issues;
	}

	/// @brief 全ペアの重複チェックを行う
	/// @param elements 検証対象のUI要素群
	/// @return 重複が検出されたペアの問題リスト
	[[nodiscard]] std::vector<LayoutIssue> checkOverlaps(
		const std::vector<UIElement>& elements) const
	{
		std::vector<LayoutIssue> issues;

		for (std::size_t i = 0; i < elements.size(); ++i)
		{
			for (std::size_t j = i + 1; j < elements.size(); ++j)
			{
				if (elements[i].bounds.intersects(elements[j].bounds))
				{
					LayoutIssue issue;
					issue.type = LayoutIssue::Overlap;
					issue.elementId = elements[i].id;
					issue.otherElementId = elements[j].id;
					issue.description =
						"Element '" + elements[i].id +
						"' overlaps with '" + elements[j].id + "'";
					issues.push_back(issue);
				}
			}
		}

		return issues;
	}

	/// @brief 問題リストをJSON配列文字列に変換する
	/// @param issues 問題リスト
	/// @return JSON配列文字列
	[[nodiscard]] std::string toJson(const std::vector<LayoutIssue>& issues) const
	{
		std::string json = "[";
		for (std::size_t i = 0; i < issues.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += issues[i].toJson();
		}
		json += "]";
		return json;
	}
};

} // namespace mitiru::validate
