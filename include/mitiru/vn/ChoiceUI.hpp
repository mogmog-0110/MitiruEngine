#pragma once

/// @file ChoiceUI.hpp
/// @brief VN 用の対話型 choice/選択システム
/// @details N 個の choice を縦（または横/グリッド）のボタンリストとして表示し、
///          キーボード・マウス・コントローラーでのナビゲーションに対応する。
///          時間制限付き choice、条件による有効/無効、順次表示アニメーションをサポート。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>

namespace mitiru::vn
{

// ── 状態マシン ────────────────────────────────────────────

/// @brief choice UI の表示/操作状態
enum class ChoiceState : std::uint8_t
{
	Hidden,        ///< 非表示
	Appearing,     ///< 表示アニメーション中
	Active,        ///< 入力受付中
	Selected,      ///< choice が決定された短いフィードバック状態
	Disappearing   ///< 退場アニメーション中
};

// ── レイアウト ───────────────────────────────────────────────────

/// @brief choice の配置方向
enum class ChoiceLayout : std::uint8_t
{
	Vertical,     ///< 上から下へのリスト
	Horizontal,   ///< 左から右への行
	Grid          ///< 複数列のグリッド
};

/// @brief choice リストの水平方向の揃え
enum class ChoiceAlignment : std::uint8_t
{
	Left,
	Center,
	Right
};

/// @brief 個々の choice の表示アニメーション
enum class ChoiceAnimation : std::uint8_t
{
	None,       ///< 即座に表示
	FadeIn,     ///< 項目ごとの alpha フェード
	SlideIn     ///< 横からスライドイン
};

// ── スタイル ──────────────────────────────────────────────────

/// @brief choice ボタンの視覚スタイル
struct ChoiceButtonStyle
{
	sgc::Colorf normalColor{0.15f, 0.15f, 0.15f, 0.85f};
	sgc::Colorf hoverColor{0.25f, 0.25f, 0.35f, 0.9f};
	sgc::Colorf selectedColor{0.0f, 0.5f, 1.0f, 0.9f};
	sgc::Colorf disabledColor{0.3f, 0.3f, 0.3f, 0.5f};
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf disabledTextColor{0.6f, 0.6f, 0.6f, 0.6f};
	sgc::Colorf borderColor{0.5f, 0.5f, 0.5f, 0.8f};
	float borderWidth = 1.0f;
	float fontSize    = 20.0f;
	float paddingH    = 16.0f;   ///< ボタン内側の水平方向の余白
	float paddingV    = 10.0f;   ///< ボタン内側の垂直方向の余白
};

// ── choice エントリ ─────────────────────────────────────────────

/// @brief 1 つの choice 項目
struct ChoiceEntry
{
	std::string text;                       ///< 表示テキスト
	bool enabled = true;                    ///< 選択可能か（false = グレーアウト）
	std::function<bool()> condition;        ///< 任意の動的条件

	/// @brief この choice が現在選択可能かを評価する
	[[nodiscard]] bool isEnabled() const
	{
		if (condition)
		{
			return condition();
		}
		return enabled;
	}
};

// ── コールバック ────────────────────────────────────────────────

/// @brief choice が選択されたときに呼ばれる
/// @param index 選択された choice の 0 始まりインデックス
using ChoiceSelectedCallback = std::function<void(std::size_t index)>;

/// @brief 未選択のままタイマーが切れたときに呼ばれる
/// @param defaultIndex デフォルトの choice インデックス
using ChoiceTimeoutCallback = std::function<void(std::size_t defaultIndex)>;

/// @brief choice ラベル用のテキスト描画コールバック
using ChoiceTextRenderer = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& text,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

// ── 設定 ────────────────────────────────────────────────

/// @brief choice UI の全設定
struct ChoiceUIConfig
{
	// レイアウト
	sgc::Rectf containerBounds{460.0f, 300.0f, 1000.0f, 480.0f};
	ChoiceLayout layout     = ChoiceLayout::Vertical;
	ChoiceAlignment alignment = ChoiceAlignment::Center;
	float buttonWidth       = 600.0f;
	float buttonHeight      = 48.0f;
	float spacing           = 8.0f;
	int gridColumns         = 2;       ///< Grid レイアウトの列数

	// アニメーション
	ChoiceAnimation entryAnimation = ChoiceAnimation::FadeIn;
	float animDurationSec     = 0.3f;     ///< 表示アニメーションの総時間
	float perItemDelaySec     = 0.08f;    ///< 項目間のずらし遅延
	float exitDurationSec     = 0.2f;     ///< 退場アニメーション時間

	// タイマー
	bool timedChoice          = false;    ///< カウントダウンタイマーを有効化
	float timeoutSec          = 10.0f;    ///< タイムアウトまでの秒数
	std::size_t defaultChoice = 0;        ///< タイムアウト時に選択される choice
	sgc::Colorf timerBarColor{1.0f, 0.6f, 0.0f, 0.9f};
	float timerBarHeight      = 4.0f;

	// スタイル
	ChoiceButtonStyle buttonStyle;
};

// ── ChoiceUI クラス ───────────────────────────────────────────

/// @brief VN 用の対話型 choice/選択 UI
///
/// @code
/// mitiru::vn::ChoiceUI choices;
/// choices.setChoices({
///     {"Go north", true},
///     {"Go south", true},
///     {"Stay here", false}
/// });
/// choices.onSelected([](std::size_t idx) {
///     // Handle selection...
/// });
/// choices.show();
///
/// // In game loop:
/// choices.update(dt);
/// batch.begin();
/// choices.draw(batch);
/// batch.end();
/// @endcode
class ChoiceUI
{
	ChoiceUIConfig m_config;
	ChoiceState m_state = ChoiceState::Hidden;

	std::vector<ChoiceEntry> m_choices;
	int m_focusedIndex = 0;       ///< キーボード/コントローラーのフォーカスインデックス
	int m_hoveredIndex = -1;      ///< マウスホバーインデックス（-1 = なし）
	int m_selectedIndex = -1;     ///< 最終的な選択（-1 = なし）

	// アニメーション
	float m_animTimer     = 0.0f;
	float m_exitTimer     = 0.0f;
	float m_exitAlpha     = 1.0f;

	// カウントダウンタイマー
	float m_countdownTimer = 0.0f;

	// 項目ごとのアニメーション進行度（0〜1）
	std::vector<float> m_itemProgress;

	// コールバック
	ChoiceSelectedCallback m_onSelected;
	ChoiceTimeoutCallback m_onTimeout;
	ChoiceTextRenderer m_textRenderer;

public:
	/// @brief デフォルト設定で構築する
	/// @param config choice UI 設定
	explicit ChoiceUI(ChoiceUIConfig config = {})
		: m_config(std::move(config))
	{
	}

	// ── 状態 ────────────────────────────────────────────────

	/// @brief 現在の状態
	[[nodiscard]] ChoiceState state() const noexcept { return m_state; }

	/// @brief UI が入力を受け付けているか
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_state == ChoiceState::Active;
	}

	/// @brief 選択された choice のインデックス（なければ -1）
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief フォーカス中の choice のインデックス
	[[nodiscard]] int focusedIndex() const noexcept { return m_focusedIndex; }

	/// @brief 残りカウントダウン時間（時間制限なしなら 0）
	[[nodiscard]] float remainingTime() const noexcept
	{
		return m_config.timedChoice
			? std::max(0.0f, m_config.timeoutSec - m_countdownTimer)
			: 0.0f;
	}

	/// @brief 設定へアクセスする
	[[nodiscard]] const ChoiceUIConfig& config() const noexcept { return m_config; }

	/// @brief choice リストへアクセスする
	[[nodiscard]] const std::vector<ChoiceEntry>& choices() const noexcept
	{
		return m_choices;
	}

	// ── セットアップ ────────────────────────────────────────────────

	/// @brief 設定を置き換える
	void setConfig(ChoiceUIConfig config) { m_config = std::move(config); }

	/// @brief choice のリストを設定する
	void setChoices(std::vector<ChoiceEntry> choices)
	{
		m_choices = std::move(choices);
		m_itemProgress.assign(m_choices.size(), 0.0f);
		m_focusedIndex = findFirstEnabled(0);
		m_hoveredIndex = -1;
		m_selectedIndex = -1;
	}

	/// @brief 選択コールバックを登録する
	void onSelected(ChoiceSelectedCallback cb) { m_onSelected = std::move(cb); }

	/// @brief タイムアウトコールバックを登録する
	void onTimeout(ChoiceTimeoutCallback cb) { m_onTimeout = std::move(cb); }

	/// @brief テキスト描画コールバックを設定する
	void setTextRenderer(ChoiceTextRenderer cb) { m_textRenderer = std::move(cb); }

	// ── コマンド ─────────────────────────────────────────────

	/// @brief 表示アニメーション付きで choice を表示する
	void show()
	{
		if (m_choices.empty()) return;

		m_state = (m_config.entryAnimation == ChoiceAnimation::None)
			? ChoiceState::Active
			: ChoiceState::Appearing;

		m_animTimer = 0.0f;
		m_exitTimer = 0.0f;
		m_exitAlpha = 1.0f;
		m_countdownTimer = 0.0f;
		m_selectedIndex = -1;
		m_focusedIndex = findFirstEnabled(0);

		if (m_config.entryAnimation == ChoiceAnimation::None)
		{
			std::fill(m_itemProgress.begin(), m_itemProgress.end(), 1.0f);
		}
		else
		{
			std::fill(m_itemProgress.begin(), m_itemProgress.end(), 0.0f);
		}
	}

	/// @brief choice を隠す（通常は選択後）
	void dismiss()
	{
		if (m_state == ChoiceState::Hidden) return;
		m_state = ChoiceState::Disappearing;
		m_exitTimer = 0.0f;
	}

	// ── 入力 ────────────────────────────────────────────────

	/// @brief フォーカスを上へ移動する（キーボード/d-pad）
	void focusUp()
	{
		if (!isActive()) return;
		const int prev = findPreviousEnabled(m_focusedIndex);
		if (prev >= 0) m_focusedIndex = prev;
	}

	/// @brief フォーカスを下へ移動する（キーボード/d-pad）
	void focusDown()
	{
		if (!isActive()) return;
		const int next = findNextEnabled(m_focusedIndex);
		if (next >= 0) m_focusedIndex = next;
	}

	/// @brief フォーカス中の choice を確定する（Enter/A ボタン）
	void confirm()
	{
		if (!isActive()) return;
		if (m_focusedIndex < 0
		    || static_cast<std::size_t>(m_focusedIndex) >= m_choices.size())
		{
			return;
		}
		if (!m_choices[static_cast<std::size_t>(m_focusedIndex)].isEnabled())
		{
			return;
		}

		selectChoice(static_cast<std::size_t>(m_focusedIndex));
	}

	/// @brief ホバー検出のためマウス移動を処理する
	/// @param screenX スクリーン座標系でのマウス X
	/// @param screenY スクリーン座標系でのマウス Y
	void onMouseMove(float screenX, float screenY)
	{
		if (!isActive()) return;

		m_hoveredIndex = -1;
		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			const sgc::Rectf rect = computeButtonRect(i);
			if (screenX >= rect.x() && screenX < rect.x() + rect.width()
			    && screenY >= rect.y() && screenY < rect.y() + rect.height())
			{
				m_hoveredIndex = static_cast<int>(i);
				if (m_choices[i].isEnabled())
				{
					m_focusedIndex = static_cast<int>(i);
				}
				break;
			}
		}
	}

	/// @brief マウスクリックを処理する
	/// @param screenX スクリーン座標系でのクリック X
	/// @param screenY スクリーン座標系でのクリック Y
	void onMouseClick(float screenX, float screenY)
	{
		if (!isActive()) return;

		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			const sgc::Rectf rect = computeButtonRect(i);
			if (screenX >= rect.x() && screenX < rect.x() + rect.width()
			    && screenY >= rect.y() && screenY < rect.y() + rect.height())
			{
				if (m_choices[i].isEnabled())
				{
					selectChoice(i);
				}
				break;
			}
		}
	}

	// ── 更新 ───────────────────────────────────────────────

	/// @brief アニメーションとタイマー状態を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		switch (m_state)
		{
		case ChoiceState::Hidden:
			break;

		case ChoiceState::Appearing:
			updateEntryAnimation(dt);
			break;

		case ChoiceState::Active:
			updateCountdown(dt);
			break;

		case ChoiceState::Selected:
			// 短く点滅してから自動的に閉じる
			m_exitTimer += dt;
			if (m_exitTimer >= 0.15f)
			{
				dismiss();
			}
			break;

		case ChoiceState::Disappearing:
			updateExitAnimation(dt);
			break;
		}
	}

	// ── 描画 ────────────────────────────────────────────

	/// @brief choice UI を SpriteBatch へ描画する
	/// @param batch SpriteBatch（begin/end の間で呼ぶこと）
	void draw(render::SpriteBatch& batch) const
	{
		if (m_state == ChoiceState::Hidden) return;

		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			drawChoiceButton(batch, i);
		}

		// タイマーバー
		if (m_config.timedChoice && m_state == ChoiceState::Active)
		{
			drawTimerBar(batch);
		}
	}

private:
	// ── 選択 ────────────────────────────────────────────

	void selectChoice(std::size_t index)
	{
		m_selectedIndex = static_cast<int>(index);
		m_state = ChoiceState::Selected;
		m_exitTimer = 0.0f;

		if (m_onSelected)
		{
			m_onSelected(index);
		}
	}

	// ── アニメーション ────────────────────────────────────────────

	void updateEntryAnimation(float dt)
	{
		m_animTimer += dt;

		bool allComplete = true;
		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			const float itemStart = static_cast<float>(i)
				* m_config.perItemDelaySec;
			const float elapsed = m_animTimer - itemStart;

			if (elapsed <= 0.0f)
			{
				m_itemProgress[i] = 0.0f;
				allComplete = false;
			}
			else if (elapsed < m_config.animDurationSec)
			{
				m_itemProgress[i] = elapsed / m_config.animDurationSec;
				allComplete = false;
			}
			else
			{
				m_itemProgress[i] = 1.0f;
			}
		}

		if (allComplete)
		{
			m_state = ChoiceState::Active;
		}
	}

	void updateExitAnimation(float dt)
	{
		m_exitTimer += dt;
		m_exitAlpha = 1.0f - std::min(1.0f,
			m_exitTimer / std::max(0.01f, m_config.exitDurationSec));

		if (m_exitAlpha <= 0.0f)
		{
			m_state = ChoiceState::Hidden;
		}
	}

	void updateCountdown(float dt)
	{
		if (!m_config.timedChoice) return;

		m_countdownTimer += dt;
		if (m_countdownTimer >= m_config.timeoutSec)
		{
			const std::size_t def = std::min(
				m_config.defaultChoice, m_choices.size() - 1);

			if (m_onTimeout)
			{
				m_onTimeout(def);
			}
			selectChoice(def);
		}
	}

	// ── レイアウト ───────────────────────────────────────────────

	/// @brief インデックスから choice ボタンのスクリーン矩形を計算する
	[[nodiscard]] sgc::Rectf computeButtonRect(std::size_t index) const noexcept
	{
		const auto& cb = m_config.containerBounds;
		const float bw = m_config.buttonWidth;
		const float bh = m_config.buttonHeight;
		const float sp = m_config.spacing;

		float x = cb.x();
		float y = cb.y();

		switch (m_config.layout)
		{
		case ChoiceLayout::Vertical:
			{
				const float totalH = static_cast<float>(m_choices.size())
					* bh + static_cast<float>(m_choices.size() - 1) * sp;

				// コンテナ内での垂直方向のセンタリング
				const float startY = cb.y()
					+ (cb.height() - totalH) * 0.5f;

				y = startY + static_cast<float>(index) * (bh + sp);

				// 水平方向の揃え
				switch (m_config.alignment)
				{
				case ChoiceAlignment::Left:
					x = cb.x();
					break;
				case ChoiceAlignment::Center:
					x = cb.x() + (cb.width() - bw) * 0.5f;
					break;
				case ChoiceAlignment::Right:
					x = cb.x() + cb.width() - bw;
					break;
				}
			}
			break;

		case ChoiceLayout::Horizontal:
			{
				const float totalW = static_cast<float>(m_choices.size())
					* bw + static_cast<float>(m_choices.size() - 1) * sp;
				const float startX = cb.x()
					+ (cb.width() - totalW) * 0.5f;
				x = startX + static_cast<float>(index) * (bw + sp);
				y = cb.y() + (cb.height() - bh) * 0.5f;
			}
			break;

		case ChoiceLayout::Grid:
			{
				const int cols = std::max(1, m_config.gridColumns);
				const int row = static_cast<int>(index) / cols;
				const int col = static_cast<int>(index) % cols;
				const float totalW = static_cast<float>(cols) * bw
					+ static_cast<float>(cols - 1) * sp;
				const float startX = cb.x()
					+ (cb.width() - totalW) * 0.5f;
				x = startX + static_cast<float>(col) * (bw + sp);
				y = cb.y() + static_cast<float>(row) * (bh + sp);
			}
			break;
		}

		return sgc::Rectf{x, y, bw, bh};
	}

	// ── 描画補助 ──────────────────────────────────────

	void drawChoiceButton(render::SpriteBatch& batch, std::size_t index) const
	{
		const auto& entry = m_choices[index];
		const sgc::Rectf rect = computeButtonRect(index);
		const auto& style = m_config.buttonStyle;

		// 項目ごとのアニメーション alpha
		float itemAlpha = (index < m_itemProgress.size())
			? m_itemProgress[index] : 1.0f;

		// 全体の退場 alpha
		itemAlpha *= m_exitAlpha;

		if (itemAlpha <= 0.0f) return;

		// ボタンの色を決定する
		const bool isEnabled = entry.isEnabled();
		const bool isFocused = (static_cast<int>(index) == m_focusedIndex);
		const bool isHovered = (static_cast<int>(index) == m_hoveredIndex);
		const bool isSelected = (static_cast<int>(index) == m_selectedIndex);

		sgc::Colorf bgColor;
		sgc::Colorf txtColor;

		if (!isEnabled)
		{
			bgColor = style.disabledColor;
			txtColor = style.disabledTextColor;
		}
		else if (isSelected)
		{
			bgColor = style.selectedColor;
			txtColor = style.textColor;
		}
		else if (isFocused || isHovered)
		{
			bgColor = style.hoverColor;
			txtColor = style.textColor;
		}
		else
		{
			bgColor = style.normalColor;
			txtColor = style.textColor;
		}

		bgColor.a *= itemAlpha;
		txtColor.a *= itemAlpha;

		// アニメーション中ならスライドインのオフセットを適用する
		sgc::Rectf drawRect = rect;
		if (m_config.entryAnimation == ChoiceAnimation::SlideIn
		    && itemAlpha < 1.0f)
		{
			const float offset = (1.0f - itemAlpha) * 50.0f;
			drawRect = sgc::Rectf{
				rect.x() + offset, rect.y(),
				rect.width(), rect.height()};
		}

		// 背景
		batch.drawRect(drawRect, bgColor);

		// 枠線
		if (style.borderWidth > 0.0f)
		{
			auto borderCol = style.borderColor;
			borderCol.a *= itemAlpha;
			batch.drawRectFrame(drawRect, borderCol, style.borderWidth);
		}

		// コールバック経由でテキストを描画する
		if (m_textRenderer)
		{
			const sgc::Rectf textArea{
				drawRect.x() + style.paddingH,
				drawRect.y() + style.paddingV,
				drawRect.width() - style.paddingH * 2.0f,
				drawRect.height() - style.paddingV * 2.0f
			};
			m_textRenderer(batch, entry.text, textArea, txtColor, style.fontSize);
		}
	}

	void drawTimerBar(render::SpriteBatch& batch) const
	{
		const float progress = 1.0f
			- std::min(1.0f, m_countdownTimer / m_config.timeoutSec);
		const auto& cb = m_config.containerBounds;

		// コンテナ上端を横切るタイマーバー
		const float barW = cb.width() * progress;
		const sgc::Rectf barRect{cb.x(), cb.y() - m_config.timerBarHeight - 2.0f,
		                         barW, m_config.timerBarHeight};

		batch.drawRect(barRect, m_config.timerBarColor);
	}

	// ── ナビゲーション補助 ───────────────────────────────────

	[[nodiscard]] int findFirstEnabled(int from) const noexcept
	{
		for (std::size_t i = static_cast<std::size_t>(std::max(0, from));
		     i < m_choices.size(); ++i)
		{
			if (m_choices[i].isEnabled())
			{
				return static_cast<int>(i);
			}
		}
		return 0;
	}

	[[nodiscard]] int findNextEnabled(int from) const noexcept
	{
		for (std::size_t i = static_cast<std::size_t>(from) + 1;
		     i < m_choices.size(); ++i)
		{
			if (m_choices[i].isEnabled())
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	[[nodiscard]] int findPreviousEnabled(int from) const noexcept
	{
		if (from <= 0) return -1;
		for (int i = from - 1; i >= 0; --i)
		{
			if (m_choices[static_cast<std::size_t>(i)].isEnabled())
			{
				return i;
			}
		}
		return -1;
	}
};

} // namespace mitiru::vn
