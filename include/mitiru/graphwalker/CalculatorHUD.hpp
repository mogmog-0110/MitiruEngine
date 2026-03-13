#pragma once

/// @file CalculatorHUD.hpp
/// @brief 電卓HUDのデータモデル（描画ロジックなし）
///
/// 電卓UIの状態管理を担う。ボタングリッドレイアウト・
/// 数式入力・履歴管理をフレームワーク非依存で提供する。
///
/// @code
/// mitiru::gw::CalculatorHUD hud;
/// hud.open();
/// hud.pressButton("3");
/// hud.pressButton("+");
/// hud.pressButton("x");
/// auto expr = hud.submit(); // "3+x"
/// @endcode

#include <string>
#include <vector>
#include <set>

#include "sgc/math/Vec2.hpp"

namespace mitiru::gw
{

/// @brief アンロック済みボタンの集合
using UnlockedButtons = std::set<std::string>;

/// @brief 電卓ボタン1つの情報
struct CalcButton
{
	std::string label;      ///< 表示ラベル
	std::string value;      ///< 入力される値
	int row{};              ///< グリッド行（0始まり）
	int col{};              ///< グリッド列（0始まり）
	bool isUnlocked{true};  ///< アンロック済みか
	bool isOperator{false}; ///< 演算子か
};

/// @brief 電卓UIの状態
struct CalculatorState
{
	std::string currentExpression;  ///< 現在の数式
	std::vector<std::string> history; ///< 入力履歴
	bool isOpen{false};             ///< 表示中か
	sgc::Vec2f windowPos{50.0f, 50.0f};   ///< ウィンドウ位置
	sgc::Vec2f windowSize{280.0f, 360.0f}; ///< ウィンドウサイズ
	bool isDragging{false};         ///< ドラッグ中か
};

/// @brief 電卓HUD（データモデル）
///
/// ゲーム内電卓の入力状態を管理する。
/// 描画はレンダラー側が担当し、ここでは純粋なデータ操作のみ行う。
class CalculatorHUD
{
public:
	/// @brief デフォルトコンストラクタ
	CalculatorHUD() = default;

	// ── 開閉制御 ──────────────────────────────────────────

	/// @brief 電卓を開く
	void open() noexcept
	{
		m_state.isOpen = true;
	}

	/// @brief 電卓を閉じる
	void close() noexcept
	{
		m_state.isOpen = false;
	}

	/// @brief 開閉を切り替える
	void toggle() noexcept
	{
		m_state.isOpen = !m_state.isOpen;
	}

	/// @brief 表示中か
	/// @return 表示中ならtrue
	[[nodiscard]] bool isOpen() const noexcept
	{
		return m_state.isOpen;
	}

	// ── 入力操作 ──────────────────────────────────────────

	/// @brief ボタン押下（値を数式に追加）
	/// @param value 追加する文字列
	void pressButton(const std::string& value)
	{
		m_state.currentExpression += value;
	}

	/// @brief 末尾の1文字を削除する
	void backspace()
	{
		if (!m_state.currentExpression.empty())
		{
			m_state.currentExpression.pop_back();
		}
	}

	/// @brief 数式をクリアする
	void clear()
	{
		m_state.currentExpression.clear();
	}

	/// @brief 数式を確定して履歴に追加する
	/// @return 確定した数式
	[[nodiscard]] std::string submit()
	{
		const auto expr = m_state.currentExpression;
		if (!expr.empty())
		{
			m_state.history.push_back(expr);
		}
		m_state.currentExpression.clear();
		return expr;
	}

	/// @brief 現在の数式を取得する
	/// @return 現在の数式文字列への参照
	[[nodiscard]] const std::string& getExpression() const noexcept
	{
		return m_state.currentExpression;
	}

	/// @brief 履歴を取得する
	/// @return 履歴の配列への参照
	[[nodiscard]] const std::vector<std::string>& getHistory() const noexcept
	{
		return m_state.history;
	}

	// ── レイアウト ──────────────────────────────────────────

	/// @brief ボタングリッドを生成する
	/// @param unlocked アンロック済みボタンIDの集合
	/// @return CalcButtonの配列（5列レイアウト）
	[[nodiscard]] std::vector<CalcButton> getButtons(const UnlockedButtons& unlocked) const
	{
		/// デフォルトボタン定義（ラベル, 値, 行, 列, 演算子フラグ）
		struct ButtonDef
		{
			const char* label;
			const char* value;
			int row;
			int col;
			bool isOperator;
		};

		static constexpr ButtonDef DEFS[] =
		{
			// 行0: 数字
			{"7", "7", 0, 0, false},
			{"8", "8", 0, 1, false},
			{"9", "9", 0, 2, false},
			{"/", "/", 0, 3, true},
			{"C", "C", 0, 4, true},
			// 行1: 数字
			{"4", "4", 1, 0, false},
			{"5", "5", 1, 1, false},
			{"6", "6", 1, 2, false},
			{"*", "*", 1, 3, true},
			{"(", "(", 1, 4, true},
			// 行2: 数字
			{"1", "1", 2, 0, false},
			{"2", "2", 2, 1, false},
			{"3", "3", 2, 2, false},
			{"-", "-", 2, 3, true},
			{")", ")", 2, 4, true},
			// 行3: ゼロ・小数点・変数
			{"0", "0", 3, 0, false},
			{".", ".", 3, 1, false},
			{"x", "x", 3, 2, false},
			{"+", "+", 3, 3, true},
			{"=", "=", 3, 4, true},
			// 行4: 関数
			{"sin", "sin(", 4, 0, true},
			{"cos", "cos(", 4, 1, true},
			{"^", "^", 4, 2, true},
			{"pi", "pi", 4, 3, false},
			{"e", "e", 4, 4, false},
		};

		std::vector<CalcButton> buttons;
		buttons.reserve(std::size(DEFS));

		for (const auto& def : DEFS)
		{
			const bool isUnlocked = unlocked.empty()
				|| unlocked.count(def.value) > 0;
			buttons.push_back(CalcButton{
				def.label,
				def.value,
				def.row,
				def.col,
				isUnlocked,
				def.isOperator
			});
		}
		return buttons;
	}

	// ── ウィンドウ位置 ──────────────────────────────────────

	/// @brief ウィンドウ位置を設定する
	/// @param pos 新しい位置
	void setPosition(sgc::Vec2f pos) noexcept
	{
		m_state.windowPos = pos;
	}

	/// @brief ウィンドウ位置を取得する
	/// @return 現在のウィンドウ位置
	[[nodiscard]] sgc::Vec2f getPosition() const noexcept
	{
		return m_state.windowPos;
	}

	/// @brief 内部状態を取得する（読み取り専用）
	/// @return CalculatorStateへの参照
	[[nodiscard]] const CalculatorState& getState() const noexcept
	{
		return m_state;
	}

private:
	CalculatorState m_state; ///< 電卓の全状態
};

} // namespace mitiru::gw
