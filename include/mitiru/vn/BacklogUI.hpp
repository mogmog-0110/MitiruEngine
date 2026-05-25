#pragma once

/// @file BacklogUI.hpp
/// @brief VN 用スクロール可能なテキスト履歴ビューア
/// @details 過去に表示した全ダイアログを話者名付きで表示する。
///          滑らかなスクロール、voice 再生、jump-to、既読/未読
///          インジケータをサポート。スクロール物理は内部で ScrollContainer を使う。

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
#include <mitiru/vn/ScrollContainer.hpp>

namespace mitiru::vn
{

// ── 状態マシン ────────────────────────────────────────────

/// @brief Backlog オーバーレイの表示状態
enum class BacklogState : std::uint8_t
{
	Hidden,       ///< 非表示
	ScrollingIn,  ///< 開くアニメーション中
	Active,       ///< 表示中・操作可能
	ScrollingOut  ///< 閉じるアニメーション中
};

// ── Backlog エントリ ────────────────────────────────────────────

/// @brief ダイアログ backlog の 1 エントリ
struct BacklogEntry
{
	std::string speaker;        ///< 話者名（ナレーションは空）
	std::string text;           ///< ダイアログテキスト
	std::string voiceId;        ///< voice クリップ識別子（無ければ空）
	float timestamp = 0.0f;     ///< 表示時のゲーム時刻
	bool isChoice = false;      ///< このエントリが choice 結果を表すか
	std::string choiceText;     ///< 選択された choice（isChoice 時）
	bool isRead = true;         ///< プレイヤーがこのエントリを見たか
};

// ── コールバック ────────────────────────────────────────────────

/// @brief プレイヤーが voice 再生を要求したときに呼ばれる
/// @param voiceId voice クリップ識別子
using VoiceReplayCallback = std::function<void(const std::string& voiceId)>;

/// @brief プレイヤーが backlog エントリへ jump したときに呼ばれる
/// @param entryIndex エントリの 0 始まりインデックス
using JumpToCallback = std::function<void(std::size_t entryIndex)>;

/// @brief backlog エントリ用のテキスト描画コールバック
using BacklogTextRenderer = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& text,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

// ── スタイル ──────────────────────────────────────────────────

/// @brief backlog オーバーレイの視覚スタイル
struct BacklogStyle
{
	sgc::Colorf overlayColor{0.0f, 0.0f, 0.0f, 0.85f};       ///< 背景の暗転
	sgc::Colorf speakerColor{0.3f, 0.7f, 1.0f, 1.0f};        ///< 話者名の色
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};           ///< 本文テキストの色
	sgc::Colorf choiceColor{1.0f, 0.8f, 0.2f, 1.0f};         ///< choice インジケータの色
	sgc::Colorf unreadMark{1.0f, 0.3f, 0.3f, 0.8f};          ///< 未読ドットの色
	sgc::Colorf voiceButtonColor{0.2f, 0.6f, 0.9f, 0.8f};    ///< voice 再生ボタン
	sgc::Colorf entryHoverColor{0.2f, 0.2f, 0.3f, 0.4f};     ///< ホバーハイライト
	sgc::Colorf separatorColor{0.3f, 0.3f, 0.3f, 0.5f};      ///< エントリ間の区切り線

	float speakerFontSize = 16.0f;
	float textFontSize    = 18.0f;
	float entryPadding    = 12.0f;    ///< 各エントリ内側の余白
	float entrySpacing    = 4.0f;     ///< エントリ間の間隔
	float speakerHeight   = 24.0f;    ///< 話者行に確保する高さ
	float textLineHeight  = 22.0f;    ///< テキスト 1 行あたりの高さ（推定）
	float voiceButtonSize = 20.0f;    ///< voice 再生ボタンのサイズ
	float unreadDotSize   = 6.0f;     ///< 未読インジケータのドットサイズ
	float marginLeft      = 60.0f;    ///< 左余白（インジケータ用）
	float marginRight     = 30.0f;    ///< 右余白
};

// ── 設定 ────────────────────────────────────────────────

/// @brief backlog UI の全設定
struct BacklogUIConfig
{
	sgc::Rectf bounds{0.0f, 0.0f, 1920.0f, 1080.0f};  ///< オーバーレイの範囲
	std::size_t maxEntries = 500;                        ///< 保持する最大エントリ数
	float animDurationSec  = 0.3f;                       ///< 開閉アニメーション

	BacklogStyle style;
	ScrollConfig scrollConfig;
};

// ── BacklogUI クラス ──────────────────────────────────────────

/// @brief スクロール可能なテキスト履歴ビューアのオーバーレイ
///
/// @code
/// mitiru::vn::BacklogUI backlog;
/// backlog.addEntry({"Alice", "Hello!", "voice_001", 1.0f});
/// backlog.addEntry({"Bob",   "Hi there!", "", 2.0f});
///
/// backlog.show();
///
/// // In game loop:
/// backlog.update(dt);
/// batch.begin();
/// backlog.draw(batch);
/// batch.end();
/// @endcode
class BacklogUI
{
	BacklogUIConfig m_config;
	BacklogState m_state = BacklogState::Hidden;

	std::vector<BacklogEntry> m_entries;
	ScrollContainer m_scroll;

	// アニメーション
	float m_animTimer = 0.0f;
	float m_overlayAlpha = 0.0f;

	// 操作
	int m_hoveredEntry = -1;

	// コールバック
	VoiceReplayCallback m_onVoiceReplay;
	JumpToCallback m_onJumpTo;
	BacklogTextRenderer m_textRenderer;

public:
	/// @brief デフォルト設定で構築する
	/// @param config backlog 設定
	explicit BacklogUI(BacklogUIConfig config = {})
		: m_config(std::move(config))
		, m_scroll(m_config.bounds, 0.0f, 0.0f, m_config.scrollConfig)
	{
	}

	// ── 状態 ────────────────────────────────────────────────

	/// @brief 現在の状態
	[[nodiscard]] BacklogState state() const noexcept { return m_state; }

	/// @brief backlog が表示中か
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != BacklogState::Hidden;
	}

	/// @brief backlog のエントリ数
	[[nodiscard]] std::size_t entryCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief エントリ一覧へアクセスする
	[[nodiscard]] const std::vector<BacklogEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief インデックスでエントリへアクセスする
	/// @param index 0 始まりのインデックス
	/// @return エントリへのポインタ。範囲外なら nullptr
	[[nodiscard]] const BacklogEntry* entryAt(std::size_t index) const noexcept
	{
		return (index < m_entries.size()) ? &m_entries[index] : nullptr;
	}

	/// @brief 設定へアクセスする
	[[nodiscard]] const BacklogUIConfig& config() const noexcept { return m_config; }

	/// @brief 内部の scroll container へアクセスする
	[[nodiscard]] const ScrollContainer& scrollContainer() const noexcept
	{
		return m_scroll;
	}

	// ── セットアップ ────────────────────────────────────────────────

	/// @brief 設定を置き換える
	void setConfig(BacklogUIConfig config)
	{
		m_config = std::move(config);
		m_scroll.setViewport(m_config.bounds);
		m_scroll.setConfig(m_config.scrollConfig);
	}

	/// @brief voice 再生コールバックを登録する
	void onVoiceReplay(VoiceReplayCallback cb) { m_onVoiceReplay = std::move(cb); }

	/// @brief jump-to コールバックを登録する
	void onJumpTo(JumpToCallback cb) { m_onJumpTo = std::move(cb); }

	/// @brief テキスト描画コールバックを設定する
	void setTextRenderer(BacklogTextRenderer cb) { m_textRenderer = std::move(cb); }

	// ── エントリ管理 ─────────────────────────────────────

	/// @brief ダイアログエントリを backlog へ追加する
	/// @param entry 追加するエントリ
	void addEntry(BacklogEntry entry)
	{
		m_entries.push_back(std::move(entry));

		// 最大エントリ数を超えないようにする
		while (m_entries.size() > m_config.maxEntries)
		{
			m_entries.erase(m_entries.begin());
		}

		recalculateContentHeight();
	}

	/// @brief 全エントリをクリアする
	void clearEntries()
	{
		m_entries.clear();
		m_scroll.setContentHeight(0.0f);
		m_scroll.scrollToTop();
	}

	/// @brief 全エントリを既読にする
	void markAllRead()
	{
		for (auto& entry : m_entries)
		{
			entry.isRead = true;
		}
	}

	// ── コマンド ─────────────────────────────────────────────

	/// @brief backlog オーバーレイを表示する
	void show()
	{
		if (m_state != BacklogState::Hidden) return;

		recalculateContentHeight();

		m_state = BacklogState::ScrollingIn;
		m_animTimer = 0.0f;

		// 最下部（最新）までスクロールした状態で開始する
		m_scroll.scrollToBottom();
	}

	/// @brief backlog オーバーレイを隠す
	void hide()
	{
		if (m_state == BacklogState::Hidden
		    || m_state == BacklogState::ScrollingOut)
		{
			return;
		}

		m_state = BacklogState::ScrollingOut;
		m_animTimer = 0.0f;
	}

	/// @brief 表示をトグルする
	void toggle()
	{
		if (m_state == BacklogState::Hidden)
		{
			show();
		}
		else if (m_state == BacklogState::Active)
		{
			hide();
		}
	}

	// ── 入力 ────────────────────────────────────────────────

	/// @brief マウスホイールを処理する
	/// @param delta ホイールデルタ（負 = 下スクロール）
	void onMouseWheel(float delta)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onMouseWheel(delta);
		}
	}

	/// @brief キーによるスクロール（上/下）を処理する
	/// @param deltaPixels 正 = 下スクロール、負 = 上スクロール
	void onKeyScroll(float deltaPixels)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onMouseWheel(-deltaPixels / m_config.scrollConfig.wheelMultiplier);
		}
	}

	/// @brief ホバー検出のためマウス移動を処理する
	/// @param screenX マウス X
	/// @param screenY マウス Y
	void onMouseMove(float screenX, float screenY)
	{
		if (m_state != BacklogState::Active) return;

		m_hoveredEntry = -1;
		const float scrollY = m_scroll.scrollY();

		float yPos = m_config.bounds.y() - scrollY;
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			const float entryH = estimateEntryHeight(i);
			const sgc::Rectf entryRect{
				m_config.bounds.x(), yPos,
				m_config.bounds.width(), entryH};

			if (screenY >= entryRect.y()
			    && screenY < entryRect.y() + entryRect.height()
			    && screenX >= entryRect.x()
			    && screenX < entryRect.x() + entryRect.width())
			{
				// viewport 内のときのみ登録する
				if (entryRect.y() + entryH > m_config.bounds.y()
				    && entryRect.y() < m_config.bounds.y() + m_config.bounds.height())
				{
					m_hoveredEntry = static_cast<int>(i);
				}
				break;
			}
			yPos += entryH + m_config.style.entrySpacing;
		}
	}

	/// @brief backlog エントリ上のクリックを処理する
	/// @param screenX クリック X
	/// @param screenY クリック Y
	void onMouseClick(float screenX, float screenY)
	{
		if (m_state != BacklogState::Active || m_hoveredEntry < 0) return;

		const auto idx = static_cast<std::size_t>(m_hoveredEntry);
		if (idx >= m_entries.size()) return;

		const auto& entry = m_entries[idx];

		// クリックが voice 再生ボタン領域上かを確認する
		const float scrollY = m_scroll.scrollY();
		float yPos = m_config.bounds.y() - scrollY;
		for (std::size_t i = 0; i < idx; ++i)
		{
			yPos += estimateEntryHeight(i) + m_config.style.entrySpacing;
		}

		const float voiceX = m_config.bounds.x() + 10.0f;
		const float voiceY = yPos + m_config.style.entryPadding;
		const float voiceSize = m_config.style.voiceButtonSize;

		if (!entry.voiceId.empty()
		    && screenX >= voiceX && screenX < voiceX + voiceSize
		    && screenY >= voiceY && screenY < voiceY + voiceSize)
		{
			if (m_onVoiceReplay)
			{
				m_onVoiceReplay(entry.voiceId);
			}
		}
		else
		{
			// このダイアログ地点へ jump する
			if (m_onJumpTo)
			{
				m_onJumpTo(idx);
			}
		}
	}

	/// @brief ドラッグ開始を処理する
	void onDragBegin(float x, float y)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onDragBegin(x, y);
		}
	}

	/// @brief ドラッグ移動を処理する
	void onDragMove(float x, float y)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onDragMove(x, y);
		}
	}

	/// @brief ドラッグ終了を処理する
	void onDragEnd(float velX = 0.0f, float velY = 0.0f)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onDragEnd(velX, velY);
		}
	}

	// ── 更新 ───────────────────────────────────────────────

	/// @brief アニメーションとスクロール物理を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		switch (m_state)
		{
		case BacklogState::Hidden:
			m_overlayAlpha = 0.0f;
			break;

		case BacklogState::ScrollingIn:
			m_animTimer += dt;
			m_overlayAlpha = std::min(1.0f,
				m_animTimer / std::max(0.01f, m_config.animDurationSec));
			if (m_overlayAlpha >= 1.0f)
			{
				m_state = BacklogState::Active;
				m_overlayAlpha = 1.0f;
			}
			m_scroll.update(dt);
			break;

		case BacklogState::Active:
			m_overlayAlpha = 1.0f;
			m_scroll.update(dt);
			break;

		case BacklogState::ScrollingOut:
			m_animTimer += dt;
			m_overlayAlpha = 1.0f - std::min(1.0f,
				m_animTimer / std::max(0.01f, m_config.animDurationSec));
			if (m_overlayAlpha <= 0.0f)
			{
				m_state = BacklogState::Hidden;
				m_overlayAlpha = 0.0f;
			}
			m_scroll.update(dt);
			break;
		}
	}

	// ── 描画 ────────────────────────────────────────────

	/// @brief backlog オーバーレイを SpriteBatch へ描画する
	/// @param batch SpriteBatch（begin/end の間で呼ぶこと）
	void draw(render::SpriteBatch& batch) const
	{
		if (m_state == BacklogState::Hidden || m_overlayAlpha <= 0.0f)
		{
			return;
		}

		// 背景オーバーレイ
		auto overlayCol = m_config.style.overlayColor;
		overlayCol.a *= m_overlayAlpha;
		batch.drawRect(m_config.bounds, overlayCol);

		// エントリを描画する
		const float scrollY = m_scroll.scrollY();
		const float viewTop = m_config.bounds.y();
		const float viewBottom = viewTop + m_config.bounds.height();

		float yPos = m_config.bounds.y() - scrollY;

		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			const float entryH = estimateEntryHeight(i);
			const float entryBottom = yPos + entryH;

			// viewport の完全に上または下にあるエントリはスキップする
			if (entryBottom > viewTop && yPos < viewBottom)
			{
				drawEntry(batch, i, yPos, entryH);
			}

			yPos += entryH + m_config.style.entrySpacing;

			// viewport より下に来たら早期終了する
			if (yPos > viewBottom) break;
		}

		// スクロールバー
		m_scroll.drawScrollBar(batch);
	}

private:
	// ── エントリレイアウト補助 ─────────────────────────────────

	/// @brief エントリの描画高さを推定する
	[[nodiscard]] float estimateEntryHeight(std::size_t index) const noexcept
	{
		if (index >= m_entries.size()) return 0.0f;

		const auto& style = m_config.style;
		float h = style.entryPadding * 2.0f;

		// 話者行
		if (!m_entries[index].speaker.empty())
		{
			h += style.speakerHeight;
		}

		// テキスト行数（文字数と利用可能幅からの大まかな推定）
		const float availW = m_config.bounds.width()
			- style.marginLeft - style.marginRight;
		const float charsPerLine = std::max(1.0f, availW / (style.textFontSize * 0.6f));
		const auto textLen = static_cast<float>(m_entries[index].text.size());
		const float lines = std::max(1.0f, std::ceil(textLen / charsPerLine));
		h += lines * style.textLineHeight;

		// choice インジケータ
		if (m_entries[index].isChoice)
		{
			h += style.textLineHeight;
		}

		return h;
	}

	/// @brief scroll container 用の総コンテンツ高さを再計算する
	void recalculateContentHeight()
	{
		float totalH = 0.0f;
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			totalH += estimateEntryHeight(i) + m_config.style.entrySpacing;
		}
		m_scroll.setContentHeight(totalH);
	}

	// ── エントリ描画 ────────────────────────────────────────

	void drawEntry(render::SpriteBatch& batch,
	               std::size_t index, float yPos, float entryH) const
	{
		const auto& entry = m_entries[index];
		const auto& style = m_config.style;
		const float alpha = m_overlayAlpha;

		const sgc::Rectf entryRect{
			m_config.bounds.x(), yPos,
			m_config.bounds.width(), entryH};

		// ホバーハイライト
		if (static_cast<int>(index) == m_hoveredEntry)
		{
			auto hoverCol = style.entryHoverColor;
			hoverCol.a *= alpha;
			batch.drawRect(entryRect, hoverCol);
		}

		// 区切り線
		if (index > 0)
		{
			auto sepCol = style.separatorColor;
			sepCol.a *= alpha;
			batch.drawRect(
				sgc::Rectf{entryRect.x() + style.marginLeft, yPos,
				           entryRect.width() - style.marginLeft - style.marginRight,
				           1.0f},
				sepCol);
		}

		// 未読インジケータ
		if (!entry.isRead)
		{
			auto dotCol = style.unreadMark;
			dotCol.a *= alpha;
			const float dotX = m_config.bounds.x() + 10.0f;
			const float dotY = yPos + style.entryPadding + 4.0f;
			batch.drawRect(
				sgc::Rectf{dotX, dotY, style.unreadDotSize, style.unreadDotSize},
				dotCol);
		}

		// voice 再生ボタン
		if (!entry.voiceId.empty())
		{
			auto voiceCol = style.voiceButtonColor;
			voiceCol.a *= alpha;
			const float btnX = m_config.bounds.x() + 30.0f;
			const float btnY = yPos + style.entryPadding;
			batch.drawRect(
				sgc::Rectf{btnX, btnY, style.voiceButtonSize, style.voiceButtonSize},
				voiceCol);
		}

		// テキスト内容領域
		const float textX = m_config.bounds.x() + style.marginLeft;
		const float textW = m_config.bounds.width()
			- style.marginLeft - style.marginRight;
		float textY = yPos + style.entryPadding;

		// 話者名
		if (!entry.speaker.empty() && m_textRenderer)
		{
			auto speakerCol = style.speakerColor;
			speakerCol.a *= alpha;
			m_textRenderer(batch, entry.speaker,
				sgc::Rectf{textX, textY, textW, style.speakerHeight},
				speakerCol, style.speakerFontSize);
			textY += style.speakerHeight;
		}

		// 本文テキスト
		if (m_textRenderer)
		{
			auto textCol = style.textColor;
			textCol.a *= alpha;
			const float remainH = entryH - (textY - yPos) - style.entryPadding;
			m_textRenderer(batch, entry.text,
				sgc::Rectf{textX, textY, textW, remainH},
				textCol, style.textFontSize);
			textY += remainH;
		}

		// choice インジケータ
		if (entry.isChoice && !entry.choiceText.empty() && m_textRenderer)
		{
			auto choiceCol = style.choiceColor;
			choiceCol.a *= alpha;
			const std::string choiceLabel = "> " + entry.choiceText;
			m_textRenderer(batch, choiceLabel,
				sgc::Rectf{textX + 12.0f, textY - style.textLineHeight,
				           textW - 12.0f, style.textLineHeight},
				choiceCol, style.textFontSize);
		}
	}
};

} // namespace mitiru::vn
