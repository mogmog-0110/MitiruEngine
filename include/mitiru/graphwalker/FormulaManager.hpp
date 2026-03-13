#pragma once

/// @file FormulaManager.hpp
/// @brief 数式管理・プラットフォーム化システム
///
/// 電卓で入力された数式を管理し、曲線データとプラットフォーム
/// セグメントを保持する。数式のライフタイム管理も行う。
///
/// @code
/// mitiru::gw::FormulaManager fm;
/// fm.submitFormula("x^2");
/// fm.update(1.0f);
/// auto& formulas = fm.getActiveFormulas();
/// @endcode

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Rect.hpp"

namespace mitiru::gw
{

/// @brief アクティブな数式の情報
struct ActiveFormula
{
	std::string expression;              ///< 数式文字列
	std::vector<sgc::Vec2f> curvePoints; ///< 描画用曲線点列
	std::vector<sgc::Rectf> platformSegments; ///< 衝突判定用プラットフォームセグメント
	float lifetime{30.0f};               ///< 残りライフタイム（秒）
};

/// @brief 数式評価インターフェース（前方宣言用）
///
/// 実際の数式パーサーへのブリッジ。submitFormula内で
/// 曲線の点列とプラットフォームセグメントを生成する際に使用する。
class FormulaSystem
{
public:
	/// @brief 仮想デストラクタ
	virtual ~FormulaSystem() = default;

	/// @brief 数式を検証する
	/// @param expr 数式文字列
	/// @return 有効ならtrue
	[[nodiscard]] virtual bool validate(const std::string& expr) const = 0;

	/// @brief 数式を評価して曲線点列を生成する
	/// @param expr 数式文字列
	/// @return 曲線を構成する点列
	[[nodiscard]] virtual std::vector<sgc::Vec2f> evaluate(
		const std::string& expr) const = 0;

	/// @brief 曲線点列からプラットフォームセグメントを生成する
	/// @param points 曲線点列
	/// @return プラットフォーム矩形の配列
	[[nodiscard]] virtual std::vector<sgc::Rectf> createPlatforms(
		const std::vector<sgc::Vec2f>& points) const = 0;
};

/// @brief 数式マネージャー
///
/// 数式のライフサイクルを管理する。FormulaSystemを注入して
/// 数式の検証・評価・プラットフォーム化を行う。
class FormulaManager
{
public:
	/// @brief デフォルトコンストラクタ
	FormulaManager() = default;

	/// @brief 数式評価システムを設定する
	/// @param system FormulaSystemへのポインタ（所有権なし）
	void setFormulaSystem(FormulaSystem* system) noexcept
	{
		m_system = system;
	}

	/// @brief 数式を投入する
	/// @param expr 数式文字列
	/// @return 成功時true、失敗時false（エラーはgetLastError()で取得）
	bool submitFormula(const std::string& expr)
	{
		m_lastError.clear();

		if (expr.empty())
		{
			m_lastError = "Empty expression";
			return false;
		}

		// アクティブ数上限チェック
		if (m_formulas.size() >= static_cast<std::size_t>(m_maxActive))
		{
			m_lastError = "Maximum active formulas reached";
			return false;
		}

		// FormulaSystem経由での検証・生成
		if (m_system)
		{
			if (!m_system->validate(expr))
			{
				m_lastError = "Invalid formula: " + expr;
				return false;
			}

			ActiveFormula formula;
			formula.expression = expr;
			formula.curvePoints = m_system->evaluate(expr);
			formula.platformSegments = m_system->createPlatforms(formula.curvePoints);
			formula.lifetime = m_lifetime;
			m_formulas.push_back(std::move(formula));
		}
		else
		{
			// FormulaSystemなしの場合は式のみ保存
			ActiveFormula formula;
			formula.expression = expr;
			formula.lifetime = m_lifetime;
			m_formulas.push_back(std::move(formula));
		}

		return true;
	}

	/// @brief アクティブな数式一覧を取得する
	/// @return アクティブな数式配列への参照
	[[nodiscard]] const std::vector<ActiveFormula>& getActiveFormulas() const noexcept
	{
		return m_formulas;
	}

	/// @brief 数式の更新（ライフタイム減少・期限切れ除去）
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& f : m_formulas)
		{
			f.lifetime -= dt;
		}

		// 期限切れを除去
		m_formulas.erase(
			std::remove_if(m_formulas.begin(), m_formulas.end(),
				[](const ActiveFormula& f) { return f.lifetime <= 0.0f; }),
			m_formulas.end()
		);
	}

	/// @brief 全数式をクリアする
	void clearFormulas()
	{
		m_formulas.clear();
	}

	/// @brief 最後のエラーメッセージを取得する
	/// @return エラー文字列（エラーなしなら空文字列）
	[[nodiscard]] const std::string& getLastError() const noexcept
	{
		return m_lastError;
	}

	/// @brief 数式のライフタイムを設定する
	/// @param seconds ライフタイム（秒）
	void setFormulaLifetime(float seconds) noexcept
	{
		m_lifetime = seconds;
	}

	/// @brief 同時アクティブ数上限を取得する
	/// @return 最大アクティブ数
	[[nodiscard]] int getMaxActiveFormulas() const noexcept
	{
		return m_maxActive;
	}

	/// @brief 同時アクティブ数上限を設定する
	/// @param count 最大数
	void setMaxActiveFormulas(int count) noexcept
	{
		m_maxActive = count;
	}

private:
	FormulaSystem* m_system{nullptr};       ///< 数式評価システム（非所有）
	std::vector<ActiveFormula> m_formulas;   ///< アクティブな数式
	std::string m_lastError;                ///< 最後のエラーメッセージ
	float m_lifetime{30.0f};                ///< デフォルトライフタイム（秒）
	int m_maxActive{3};                     ///< 同時アクティブ数上限
};

} // namespace mitiru::gw
