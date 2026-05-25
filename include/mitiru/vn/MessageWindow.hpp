#pragma once

/// @file MessageWindow.hpp
/// @brief VN用メッセージウィンドウコンポーネント
/// @details ADV/NVLモード、話者ネームプレート、1文字ずつのテキスト表示、
///          クリック待ちインジケータ、window skin（単色/9-slice/カスタム）、
///          表示/非表示アニメーションを備えた多機能なダイアログ表示。

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
#include <mitiru/vn/NineSlice.hpp>

namespace mitiru::vn
{

// ── 状態機械 ────────────────────────────────────────────

/// @brief メッセージウィンドウの表示状態
enum class MessageWindowState : std::uint8_t
{
	Hidden,        ///< 非表示
	Appearing,     ///< 表示アニメーション進行中
	Idle,          ///< 表示中だがテキスト表示なし
	Displaying,    ///< 1文字ずつ表示中
	WaitingClick,  ///< 全テキスト表示済み、プレイヤーのクリック待ち
	Disappearing   ///< 非表示アニメーション進行中
};

/// @brief メッセージウィンドウの表示モード
enum class MessageMode : std::uint8_t
{
	ADV,   ///< 画面下部のテキストボックス（標準的なアドベンチャーゲーム）
	NVL    ///< スクロール付き全画面テキスト（ノベルモード）
};

/// @brief 表示/非表示アニメーションの種別
enum class WindowAnimation : std::uint8_t
{
	None,      ///< 即時表示/非表示
	Fade,      ///< アルファフェード
	SlideUp    ///< 下からスライドアップ
};

// ── Window skin ──────────────────────────────────────────────

/// @brief window skin の種別セレクタ
enum class WindowSkinType : std::uint8_t
{
	SolidColor,     ///< 枠線・アルファ付きの単色
	Image9Slice,    ///< 9-slice 拡縮可能な背景
	Custom          ///< ユーザー指定の描画コールバック
};

/// @brief 単色 skin のパラメータ
struct SolidColorSkin
{
	sgc::Colorf fillColor{0.0f, 0.0f, 0.0f, 0.75f};
	sgc::Colorf borderColor{0.4f, 0.4f, 0.4f, 1.0f};
	float borderWidth = 2.0f;
	float cornerRadius = 0.0f;  ///< 将来の角丸用に予約
};

/// @brief カスタム描画コールバックのシグネチャ
/// @param batch  描画先の SpriteBatch
/// @param rect   ウィンドウ矩形
/// @param alpha  現在のアルファ（アニメーション由来）
using CustomSkinRenderer = std::function<
	void(render::SpriteBatch& batch, const sgc::Rectf& rect, float alpha)>;

/// @brief window skin の設定
struct WindowSkin
{
	WindowSkinType type = WindowSkinType::SolidColor;
	SolidColorSkin solidColor;
	NineSliceConfig nineSlice;
	CustomSkinRenderer customRenderer;
};

// ── クリック待ちインジケータ ─────────────────────────────────────

/// @brief クリック待ちインジケータのグリフ設定
struct ClickWaitIndicator
{
	bool enabled         = true;
	float size           = 12.0f;        ///< インジケータのサイズ（ピクセル）
	float offsetX        = -20.0f;       ///< 右端からのオフセット
	float offsetY        = -20.0f;       ///< 下端からのオフセット
	float blinkSpeed     = 3.0f;         ///< 1秒あたりの点滅回数
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
};

// ── テキスト表示コールバック ─────────────────────────────────────

/// @brief テキスト描画用コールバック
/// @param batch  SpriteBatch
/// @param text   全文文字列
/// @param visibleChars 表示する文字数（0 = 全部）
/// @param area   テキスト描画領域
/// @param color  テキスト色
/// @param fontSize テキストサイズ
using TextRenderCallback = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& text,
	     std::size_t visibleChars,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

/// @brief 話者名描画用コールバック
using NameRenderCallback = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& name,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

// ── 設定 ────────────────────────────────────────────────

/// @brief メッセージウィンドウの全設定
struct MessageWindowConfig
{
	// レイアウト
	sgc::Rectf bounds{0.0f, 700.0f, 1920.0f, 300.0f};  ///< ウィンドウ範囲
	float paddingLeft   = 24.0f;
	float paddingRight  = 24.0f;
	float paddingTop    = 20.0f;
	float paddingBottom = 20.0f;

	// ネームプレート
	bool showNamePlate          = true;
	sgc::Rectf namePlateBounds{24.0f, 660.0f, 220.0f, 40.0f};
	sgc::Colorf namePlateColor{0.0f, 0.0f, 0.0f, 0.85f};
	sgc::Colorf namePlateBorder{0.6f, 0.6f, 0.6f, 1.0f};
	sgc::Colorf nameTextColor{1.0f, 1.0f, 1.0f, 1.0f};
	float nameFontSize = 18.0f;

	// テキスト
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
	float fontSize       = 22.0f;
	float charsPerSecond = 30.0f;  ///< 文字表示速度

	// モード
	MessageMode mode = MessageMode::ADV;

	// NVL専用
	float nvlMaxLines    = 20;           ///< NVLモードでの最大表示行数
	sgc::Rectf nvlBounds{100.0f, 50.0f, 1720.0f, 980.0f};

	// Skin
	WindowSkin skin;

	// アニメーション
	WindowAnimation showAnimation = WindowAnimation::Fade;
	WindowAnimation hideAnimation = WindowAnimation::Fade;
	float animationDurationSec    = 0.25f;

	// クリック待ちインジケータ
	ClickWaitIndicator clickWait;
};

// ── NVL行エントリ ───────────────────────────────────────────

/// @brief NVLモードの蓄積テキストバッファ内の1行
struct NvlLine
{
	std::string speaker;  ///< 話者名（地の文なら空）
	std::string text;     ///< 行テキスト
};

// ── MessageWindow クラス ──────────────────────────────────────

/// @brief VN用のメインダイアログ表示コンポーネント
///
/// @code
/// mitiru::vn::MessageWindowConfig cfg;
/// cfg.bounds = {0, 700, 1920, 300};
/// mitiru::vn::MessageWindow window(cfg);
///
/// window.show();
/// window.setText("Alice", "Hello! Nice to meet you.");
///
/// // In game loop:
/// window.update(dt);
/// batch.begin();
/// window.draw(batch);
/// batch.end();
///
/// if (window.state() == mitiru::vn::MessageWindowState::WaitingClick)
/// {
///     if (clicked) window.advance();
/// }
/// @endcode
class MessageWindow
{
	MessageWindowConfig m_config;
	MessageWindowState m_state = MessageWindowState::Hidden;

	// テキスト状態
	std::string m_speaker;
	std::string m_text;
	std::size_t m_visibleChars = 0;
	float m_charTimer = 0.0f;

	// NVL蓄積行
	std::vector<NvlLine> m_nvlLines;
	bool m_nvlNewLineRevealing = false;

	// アニメーション状態
	float m_animProgress = 0.0f;  ///< 0 = 開始, 1 = 完了
	float m_alpha = 0.0f;         ///< 現在の実効アルファ

	// クリック待ちインジケータ
	float m_indicatorTimer = 0.0f;

	// ウィンドウ内スクロール用のページ履歴（現在ページのみ）
	std::vector<std::string> m_pageHistory;
	int m_pageHistoryIndex = -1;

	// 9-slice レンダラ（遅延初期化）
	NineSlice m_nineSlice{NineSliceConfig{}};

	// 描画コールバック
	TextRenderCallback m_textRenderer;
	NameRenderCallback m_nameRenderer;

public:
	/// @brief 指定した設定で構築する
	/// @param config ウィンドウ設定
	explicit MessageWindow(MessageWindowConfig config = {})
		: m_config(std::move(config))
		, m_nineSlice(m_config.skin.nineSlice)
	{
	}

	// ── 状態クエリ ────────────────────────────────────────

	/// @brief 現在の状態
	[[nodiscard]] MessageWindowState state() const noexcept { return m_state; }

	/// @brief 現在の表示モード
	[[nodiscard]] MessageMode mode() const noexcept { return m_config.mode; }

	/// @brief 現在の実効アルファ（0-1）
	[[nodiscard]] float alpha() const noexcept { return m_alpha; }

	/// @brief 全テキストが完全に表示されているか
	[[nodiscard]] bool isTextComplete() const noexcept
	{
		return m_visibleChars >= m_text.size();
	}

	/// @brief ウィンドウが表示中か（Hidden以外のすべての状態）
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != MessageWindowState::Hidden;
	}

	/// @brief 現在の話者名
	[[nodiscard]] const std::string& speaker() const noexcept { return m_speaker; }

	/// @brief 現在の全文
	[[nodiscard]] const std::string& text() const noexcept { return m_text; }

	/// @brief 表示中の文字数
	[[nodiscard]] std::size_t visibleChars() const noexcept { return m_visibleChars; }

	/// @brief 設定へアクセスする
	[[nodiscard]] const MessageWindowConfig& config() const noexcept { return m_config; }

	/// @brief 有効なウィンドウ範囲（ADV/NVLモードを考慮）
	[[nodiscard]] const sgc::Rectf& activeBounds() const noexcept
	{
		return (m_config.mode == MessageMode::NVL)
			? m_config.nvlBounds
			: m_config.bounds;
	}

	// ── 設定 ────────────────────────────────────────────

	/// @brief 設定を丸ごと差し替える
	void setConfig(MessageWindowConfig config)
	{
		m_config = std::move(config);
		m_nineSlice.setConfig(m_config.skin.nineSlice);
	}

	/// @brief 表示モードを設定する
	void setMode(MessageMode mode) noexcept { m_config.mode = mode; }

	/// @brief テキスト描画コールバックを設定する
	void setTextRenderer(TextRenderCallback cb) { m_textRenderer = std::move(cb); }

	/// @brief 話者名描画コールバックを設定する
	void setNameRenderer(NameRenderCallback cb) { m_nameRenderer = std::move(cb); }

	// ── コマンド ─────────────────────────────────────────────

	/// @brief アニメーション付きでウィンドウを表示する
	void show()
	{
		if (m_state != MessageWindowState::Hidden
		    && m_state != MessageWindowState::Disappearing)
		{
			return;
		}

		if (m_config.showAnimation == WindowAnimation::None)
		{
			m_state = MessageWindowState::Idle;
			m_alpha = 1.0f;
			m_animProgress = 1.0f;
		}
		else
		{
			m_state = MessageWindowState::Appearing;
			m_animProgress = 0.0f;
		}
	}

	/// @brief アニメーション付きでウィンドウを非表示にする
	void hide()
	{
		if (m_state == MessageWindowState::Hidden
		    || m_state == MessageWindowState::Disappearing)
		{
			return;
		}

		if (m_config.hideAnimation == WindowAnimation::None)
		{
			m_state = MessageWindowState::Hidden;
			m_alpha = 0.0f;
			m_animProgress = 0.0f;
		}
		else
		{
			m_state = MessageWindowState::Disappearing;
			m_animProgress = 0.0f;
		}
	}

	/// @brief 新しいダイアログテキストを設定する
	/// @param speaker 話者名（地の文なら空）
	/// @param text ダイアログテキスト
	void setText(const std::string& speaker, const std::string& text)
	{
		if (m_config.mode == MessageMode::NVL)
		{
			m_nvlLines.push_back(NvlLine{speaker, text});
			m_nvlNewLineRevealing = true;

			// 上限を超えた古い行を切り詰める
			const auto maxLines = static_cast<std::size_t>(m_config.nvlMaxLines);
			while (m_nvlLines.size() > maxLines)
			{
				m_nvlLines.erase(m_nvlLines.begin());
			}
		}

		// ページ履歴に保存する
		if (!m_text.empty())
		{
			m_pageHistory.push_back(m_text);
		}
		m_pageHistoryIndex = -1;

		m_speaker = speaker;
		m_text = text;
		m_visibleChars = 0;
		m_charTimer = 0.0f;

		if (m_state == MessageWindowState::Idle
		    || m_state == MessageWindowState::WaitingClick)
		{
			m_state = MessageWindowState::Displaying;
		}
	}

	/// @brief 残りのテキストを即座に全表示する
	void revealAll() noexcept
	{
		m_visibleChars = m_text.size();
		if (m_state == MessageWindowState::Displaying)
		{
			m_state = MessageWindowState::WaitingClick;
		}
		m_nvlNewLineRevealing = false;
	}

	/// @brief クリック待ちを進める（プレイヤーがクリックしたときに呼ぶ）
	void advance()
	{
		if (m_state == MessageWindowState::Displaying)
		{
			// 1回目のクリック: 即時全表示
			revealAll();
		}
		else if (m_state == MessageWindowState::WaitingClick)
		{
			// 2回目のクリック: 次のテキストの準備完了
			m_state = MessageWindowState::Idle;
		}
	}

	/// @brief NVLモードの蓄積テキストをクリアする
	void clearNvl()
	{
		m_nvlLines.clear();
		m_nvlNewLineRevealing = false;
	}

	/// @brief ページ履歴を上にスクロールする（前のテキストを表示）
	/// @return スクロールしたらtrue、先頭ならfalse
	[[nodiscard]] bool scrollHistoryUp()
	{
		if (m_pageHistory.empty()) return false;

		if (m_pageHistoryIndex < 0)
		{
			m_pageHistoryIndex = static_cast<int>(m_pageHistory.size()) - 1;
		}
		else if (m_pageHistoryIndex > 0)
		{
			--m_pageHistoryIndex;
		}
		else
		{
			return false;
		}
		return true;
	}

	/// @brief ページ履歴を下にスクロールする（次のテキストを表示）
	/// @return 現在テキストまで戻ったらtrue、履歴中でなければfalse
	[[nodiscard]] bool scrollHistoryDown()
	{
		if (m_pageHistoryIndex < 0) return false;

		if (m_pageHistoryIndex < static_cast<int>(m_pageHistory.size()) - 1)
		{
			++m_pageHistoryIndex;
		}
		else
		{
			m_pageHistoryIndex = -1;  // 現在テキストへ戻る
		}
		return true;
	}

	// ── 更新 ───────────────────────────────────────────────

	/// @brief 状態とアニメーションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		switch (m_state)
		{
		case MessageWindowState::Hidden:
			m_alpha = 0.0f;
			break;

		case MessageWindowState::Appearing:
			updateAnimation(dt, true);
			break;

		case MessageWindowState::Idle:
			m_alpha = 1.0f;
			break;

		case MessageWindowState::Displaying:
			m_alpha = 1.0f;
			updateTextReveal(dt);
			break;

		case MessageWindowState::WaitingClick:
			m_alpha = 1.0f;
			m_indicatorTimer += dt;
			break;

		case MessageWindowState::Disappearing:
			updateAnimation(dt, false);
			break;
		}
	}

	// ── 描画 ────────────────────────────────────────────

	/// @brief メッセージウィンドウを SpriteBatch に描画する
	/// @param batch SpriteBatch（begin/endの間で呼ぶこと）
	void draw(render::SpriteBatch& batch) const
	{
		if (m_state == MessageWindowState::Hidden || m_alpha <= 0.0f)
		{
			return;
		}

		const sgc::Rectf& bounds = activeBounds();
		const float yOffset = computeSlideOffset();

		const sgc::Rectf drawBounds{
			bounds.x(),
			bounds.y() + yOffset,
			bounds.width(),
			bounds.height()
		};

		// ウィンドウ背景を描画
		drawSkin(batch, drawBounds);

		// ネームプレートを描画（ADVモードのみ）
		if (m_config.mode == MessageMode::ADV && m_config.showNamePlate
		    && !m_speaker.empty())
		{
			drawNamePlate(batch, yOffset);
		}

		// テキストを描画
		drawText(batch, drawBounds);

		// クリック待ちインジケータを描画
		if (m_state == MessageWindowState::WaitingClick
		    && m_config.clickWait.enabled)
		{
			drawClickWaitIndicator(batch, drawBounds);
		}
	}

private:
	// ── アニメーション補助 ────────────────────────────────────

	void updateAnimation(float dt, bool appearing)
	{
		const float duration = m_config.animationDurationSec;
		if (duration <= 0.0f)
		{
			m_animProgress = 1.0f;
		}
		else
		{
			m_animProgress += dt / duration;
			m_animProgress = std::min(1.0f, m_animProgress);
		}

		const float t = smoothstep(m_animProgress);

		if (appearing)
		{
			m_alpha = t;
			if (m_animProgress >= 1.0f)
			{
				m_state = MessageWindowState::Idle;
				m_alpha = 1.0f;
			}
		}
		else
		{
			m_alpha = 1.0f - t;
			if (m_animProgress >= 1.0f)
			{
				m_state = MessageWindowState::Hidden;
				m_alpha = 0.0f;
			}
		}
	}

	void updateTextReveal(float dt)
	{
		if (m_config.charsPerSecond <= 0.0f || m_visibleChars >= m_text.size())
		{
			m_visibleChars = m_text.size();
			m_state = MessageWindowState::WaitingClick;
			m_nvlNewLineRevealing = false;
			return;
		}

		m_charTimer += dt;
		const float interval = 1.0f / m_config.charsPerSecond;

		while (m_charTimer >= interval && m_visibleChars < m_text.size())
		{
			m_charTimer -= interval;
			++m_visibleChars;
		}

		if (m_visibleChars >= m_text.size())
		{
			m_state = MessageWindowState::WaitingClick;
			m_nvlNewLineRevealing = false;
		}
	}

	/// @brief スライドアニメーション用の垂直オフセットを計算する
	[[nodiscard]] float computeSlideOffset() const noexcept
	{
		const bool isAppearing = (m_state == MessageWindowState::Appearing);
		const bool isDisappearing = (m_state == MessageWindowState::Disappearing);

		WindowAnimation anim = isAppearing
			? m_config.showAnimation
			: (isDisappearing ? m_config.hideAnimation : WindowAnimation::None);

		if (anim != WindowAnimation::SlideUp)
		{
			return 0.0f;
		}

		const float slideDistance = activeBounds().height();

		if (isAppearing)
		{
			return slideDistance * (1.0f - smoothstep(m_animProgress));
		}
		return slideDistance * smoothstep(m_animProgress);
	}

	// ── skin 描画 ─────────────────────────────────────────

	void drawSkin(render::SpriteBatch& batch, const sgc::Rectf& rect) const
	{
		switch (m_config.skin.type)
		{
		case WindowSkinType::SolidColor:
			drawSolidSkin(batch, rect);
			break;

		case WindowSkinType::Image9Slice:
			{
				auto tint = sgc::Colorf{1.0f, 1.0f, 1.0f, m_alpha};
				m_nineSlice.draw(batch, rect, tint);
			}
			break;

		case WindowSkinType::Custom:
			if (m_config.skin.customRenderer)
			{
				m_config.skin.customRenderer(batch, rect, m_alpha);
			}
			break;
		}
	}

	void drawSolidSkin(render::SpriteBatch& batch, const sgc::Rectf& rect) const
	{
		const auto& skin = m_config.skin.solidColor;

		// 塗りつぶし
		auto fill = skin.fillColor;
		fill.a *= m_alpha;
		batch.drawRect(rect, fill);

		// 枠線
		if (skin.borderWidth > 0.0f)
		{
			auto border = skin.borderColor;
			border.a *= m_alpha;
			batch.drawRectFrame(rect, border, skin.borderWidth);
		}
	}

	// ── ネームプレート描画 ───────────────────────────────────

	void drawNamePlate(render::SpriteBatch& batch, float yOffset) const
	{
		const sgc::Rectf npRect{
			m_config.namePlateBounds.x(),
			m_config.namePlateBounds.y() + yOffset,
			m_config.namePlateBounds.width(),
			m_config.namePlateBounds.height()
		};

		// 背景
		auto bg = m_config.namePlateColor;
		bg.a *= m_alpha;
		batch.drawRect(npRect, bg);

		// 枠線
		auto border = m_config.namePlateBorder;
		border.a *= m_alpha;
		batch.drawRectFrame(npRect, border, 1.0f);

		// コールバック経由で話者名テキストを描画
		if (m_nameRenderer)
		{
			auto col = m_config.nameTextColor;
			col.a *= m_alpha;
			const sgc::Rectf textArea{
				npRect.x() + 8.0f,
				npRect.y() + 4.0f,
				npRect.width() - 16.0f,
				npRect.height() - 8.0f
			};
			m_nameRenderer(batch, m_speaker, textArea, col, m_config.nameFontSize);
		}
	}

	// ── テキスト描画 ─────────────────────────────────────────

	void drawText(render::SpriteBatch& batch, const sgc::Rectf& bounds) const
	{
		if (!m_textRenderer) return;

		const sgc::Rectf textArea{
			bounds.x() + m_config.paddingLeft,
			bounds.y() + m_config.paddingTop,
			bounds.width() - m_config.paddingLeft - m_config.paddingRight,
			bounds.height() - m_config.paddingTop - m_config.paddingBottom
		};

		auto col = m_config.textColor;
		col.a *= m_alpha;

		if (m_config.mode == MessageMode::NVL)
		{
			drawNvlText(batch, textArea, col);
		}
		else
		{
			// ADVモード: 現在テキストまたは履歴ページを表示
			const std::string& displayText = (m_pageHistoryIndex >= 0
				&& m_pageHistoryIndex < static_cast<int>(m_pageHistory.size()))
				? m_pageHistory[static_cast<std::size_t>(m_pageHistoryIndex)]
				: m_text;

			const std::size_t chars = (m_pageHistoryIndex >= 0)
				? displayText.size()
				: m_visibleChars;

			m_textRenderer(batch, displayText, chars, textArea,
			               col, m_config.fontSize);
		}
	}

	void drawNvlText(render::SpriteBatch& batch,
	                 const sgc::Rectf& area,
	                 const sgc::Colorf& col) const
	{
		// NVLモードでは全行を連結し1ブロックとして描画する。
		// 最終行は部分的に表示中の場合がある。
		std::string fullText;
		for (std::size_t i = 0; i < m_nvlLines.size(); ++i)
		{
			const auto& line = m_nvlLines[i];
			if (!line.speaker.empty())
			{
				fullText += line.speaker + ": ";
			}
			fullText += line.text;
			if (i + 1 < m_nvlLines.size())
			{
				fullText += '\n';
			}
		}

		// 最終行を表示中でない限り全文字を表示する
		std::size_t totalVisible = fullText.size();
		if (m_nvlNewLineRevealing && !m_nvlLines.empty())
		{
			// 最終行のみが部分的に表示中
			const std::size_t precedingLen = fullText.size() - m_text.size();
			totalVisible = precedingLen + m_visibleChars;
		}

		m_textRenderer(batch, fullText, totalVisible, area,
		               col, m_config.fontSize);
	}

	// ── クリック待ちインジケータ ─────────────────────────────────

	void drawClickWaitIndicator(render::SpriteBatch& batch,
	                            const sgc::Rectf& bounds) const
	{
		const auto& cw = m_config.clickWait;
		const float blinkAlpha = (std::sin(m_indicatorTimer * cw.blinkSpeed
		                                   * 6.2831853f) + 1.0f) * 0.5f;

		auto col = cw.color;
		col.a *= m_alpha * blinkAlpha;

		const float x = bounds.x() + bounds.width() + cw.offsetX;
		const float y = bounds.y() + bounds.height() + cw.offsetY;

		// 下向きの小さな三角形として描画する
		const sgc::Rectf indicator{x, y, cw.size, cw.size};
		batch.drawRect(indicator, col);
	}

	// ── 数学ユーティリティ ───────────────────────────────────────

	/// @brief スムーズステップ補間（ease in-out）
	[[nodiscard]] static float smoothstep(float t) noexcept
	{
		t = std::max(0.0f, std::min(1.0f, t));
		return t * t * (3.0f - 2.0f * t);
	}
};

} // namespace mitiru::vn
