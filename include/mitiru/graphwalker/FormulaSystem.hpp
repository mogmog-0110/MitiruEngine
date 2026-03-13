#pragma once

/// @file FormulaSystem.hpp
/// @brief 数式パース・曲線生成・プラットフォーム化システム
///
/// FormulaSystemはMathParserを使って数式文字列 y=f(x) を評価し、
/// 曲線上の点列やプラットフォーム矩形を生成する。
/// アンロック可能な演算子管理も提供する。
///
/// @code
/// mitiru::gw::GraphArea area{60.0f, {400.0f, 300.0f}};
/// mitiru::gw::FormulaSystem system;
/// system.setGraphArea(area);
/// auto result = system.evaluate("x^2", -5.0f, 5.0f);
/// if (result.valid) { /* result.curvePoints を描画 */ }
/// @endcode

#include <string>
#include <vector>
#include <set>
#include <cmath>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/math/MathParser.hpp"
#include "mitiru/graphwalker/GraphArea.hpp"

namespace mitiru::gw
{

/// @brief 数式評価結果
struct FormulaResult
{
	bool valid = false;                         ///< 評価が成功したか
	std::string error;                          ///< エラーメッセージ（失敗時）
	std::vector<sgc::Vec2f> curvePoints;        ///< 曲線上の点列（ワールド座標）
};

/// @brief アンロック済みの数学演算子・関数ボタンを管理する
struct UnlockedButtons
{
	std::set<std::string> buttons;              ///< アンロック済みボタン識別子

	/// @brief 指定のボタンがアンロック済みか確認する
	/// @param id ボタン識別子
	/// @return アンロック済みならtrue
	[[nodiscard]] bool isUnlocked(const std::string& id) const
	{
		return buttons.count(id) > 0;
	}

	/// @brief ボタンをアンロックする
	/// @param id ボタン識別子
	void unlock(const std::string& id)
	{
		buttons.insert(id);
	}
};

/// @brief 数式パース・評価・曲線生成システム
///
/// MathParserをラップし、y=f(x)の評価、曲線点列の生成、
/// プラットフォーム矩形の生成を行う。
class FormulaSystem
{
public:
	/// @brief デフォルトコンストラクタ
	FormulaSystem() = default;

	/// @brief 数式を評価して曲線点列を生成する
	/// @param expr 数式文字列（xを変数として使用）
	/// @param xMin サンプリング範囲の最小X（論理座標）
	/// @param xMax サンプリング範囲の最大X（論理座標）
	/// @param sampleCount サンプル数
	/// @return 評価結果（ワールド座標の点列を含む）
	[[nodiscard]] FormulaResult evaluate(
		const std::string& expr,
		float xMin,
		float xMax,
		int sampleCount = 200) const
	{
		FormulaResult result;

		if (sampleCount < 2)
		{
			result.valid = false;
			result.error = "Sample count must be at least 2";
			return result;
		}

		// パーサーインスタンスを作成して式を検証
		sgc::math::MathParser parser;
		parser.setVariable("x", 0.0f);

		auto testResult = parser.evaluate(expr);
		if (!testResult.has_value())
		{
			result.valid = false;
			result.error = parser.getLastError();
			return result;
		}

		// サンプリングして曲線点列を生成
		result.curvePoints.reserve(static_cast<std::size_t>(sampleCount));
		const float step = (xMax - xMin) / static_cast<float>(sampleCount - 1);

		for (int i = 0; i < sampleCount; ++i)
		{
			const float x = xMin + step * static_cast<float>(i);
			parser.setVariable("x", x);

			auto yOpt = parser.evaluate(expr);
			if (yOpt.has_value())
			{
				const float y = *yOpt;
				// NaN/Infチェック — 描画不能な点はスキップ
				if (std::isfinite(y))
				{
					const auto worldPt = m_graphArea.logicToWorld({x, y});
					result.curvePoints.push_back(worldPt);
				}
			}
		}

		result.valid = true;
		return result;
	}

	/// @brief 数式の構文が正しいか検証する
	/// @param expr 数式文字列
	/// @return 構文が正しい場合true
	[[nodiscard]] bool isValid(const std::string& expr) const
	{
		sgc::math::MathParser parser;
		parser.setVariable("x", 0.0f);
		return parser.isValid(expr);
	}

	/// @brief アンロック状態に基づいて利用可能な演算子リストを返す
	/// @param unlocked アンロック済みボタン情報
	/// @return 利用可能な演算子・関数名の一覧
	///
	/// デフォルトで使用可能: 数字, +, -, x, (, )
	/// アンロック対象: *, /, sin, cos, tan, abs, sqrt, log, ^, pi, e
	[[nodiscard]] static std::vector<std::string> getAvailableOperators(
		const UnlockedButtons& unlocked)
	{
		// デフォルトで利用可能な演算子
		std::vector<std::string> ops = {
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
			"+", "-", "x", "(", ")"
		};

		// アンロック可能な演算子一覧
		static const std::vector<std::string> UNLOCKABLE = {
			"*", "/", "sin", "cos", "tan", "abs", "sqrt", "log", "^", "pi", "e"
		};

		for (const auto& op : UNLOCKABLE)
		{
			if (unlocked.isUnlocked(op))
			{
				ops.push_back(op);
			}
		}

		return ops;
	}

	/// @brief 曲線点列からプラットフォーム矩形列を生成する
	/// @param formulaResult 数式評価結果
	/// @param thickness プラットフォームの厚さ（ピクセル）
	/// @return プラットフォーム矩形の配列（ワールド座標）
	///
	/// 隣接する2点間を結ぶ矩形セグメントを生成する。
	/// 各セグメントは曲線に沿った水平方向の矩形（AABB近似）となる。
	[[nodiscard]] static std::vector<sgc::Rectf> generatePlatformSegments(
		const FormulaResult& formulaResult,
		float thickness = 10.0f)
	{
		std::vector<sgc::Rectf> segments;

		const auto& pts = formulaResult.curvePoints;
		if (pts.size() < 2)
		{
			return segments;
		}

		segments.reserve(pts.size() - 1);

		for (std::size_t i = 0; i + 1 < pts.size(); ++i)
		{
			const auto& p0 = pts[i];
			const auto& p1 = pts[i + 1];

			// 2点のバウンディングボックスを計算
			const float minX = (p0.x < p1.x) ? p0.x : p1.x;
			const float maxX = (p0.x > p1.x) ? p0.x : p1.x;
			const float minY = (p0.y < p1.y) ? p0.y : p1.y;
			const float maxY = (p0.y > p1.y) ? p0.y : p1.y;

			const float halfThick = thickness * 0.5f;
			const float w = maxX - minX;
			// 幅が0の場合でも最低限のサイズを確保
			const float segW = (w < 1.0f) ? 1.0f : w;

			segments.emplace_back(
				sgc::Vec2f{minX, minY - halfThick},
				sgc::Vec2f{segW, (maxY - minY) + thickness}
			);
		}

		return segments;
	}

	/// @brief GraphAreaを設定する
	/// @param area 座標変換用のGraphArea
	void setGraphArea(const GraphArea& area) noexcept
	{
		m_graphArea = area;
	}

	/// @brief 現在のGraphAreaを取得する
	/// @return GraphAreaへの参照
	[[nodiscard]] const GraphArea& getGraphArea() const noexcept
	{
		return m_graphArea;
	}

private:
	GraphArea m_graphArea;   ///< 座標変換用グラフ領域
};

} // namespace mitiru::gw
