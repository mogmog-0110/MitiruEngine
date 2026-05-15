#pragma once

/// @file VisualDebugOverlay.hpp
/// @brief レンダリング境界と問題を可視化するデバッグオーバーレイ
/// @details ゲーム画面の上にスクリーン境界・描画コール矩形・はみ出し箇所・
///          テキストバウンド・FPS/描画コール数・問題リストを描画する。

#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/validate/DrawCallValidator.hpp>

namespace mitiru::validate
{

/// @brief デバッグオーバーレイ
/// @details 有効化すると、ゲーム描画の上にバリデーション情報を重ねて表示する。
///
/// @code
/// mitiru::validate::VisualDebugOverlay overlay;
/// overlay.setEnabled(true);
///
/// // ゲーム描画後に呼ぶ
/// overlay.drawOverlay(screen, validator);
/// @endcode
class VisualDebugOverlay
{
public:
	/// @brief オーバーレイの有効/無効を設定する
	/// @param enabled 有効フラグ
	void setEnabled(bool enabled) noexcept
	{
		m_enabled = enabled;
	}

	/// @brief オーバーレイの有効/無効をトグルする
	void toggle() noexcept
	{
		m_enabled = !m_enabled;
	}

	/// @brief オーバーレイが有効か
	[[nodiscard]] bool isEnabled() const noexcept
	{
		return m_enabled;
	}

	/// @brief FPS値を設定する（外部から更新）
	/// @param fps 現在のFPS
	void setFps(float fps) noexcept
	{
		m_fps = fps;
	}

	/// @brief 描画コール矩形を記録する（フレーム内で呼ぶ）
	/// @param bounds 描画領域
	/// @param isText テキスト描画かどうか
	void recordDrawCall(const sgc::Rectf& bounds, bool isText = false)
	{
		if (!m_enabled) return;
		m_recordedRects.push_back({bounds, isText});
	}

	/// @brief フレーム開始時にリセットする
	void beginFrame()
	{
		if (!m_enabled) return;
		m_recordedRects.clear();
	}

	/// @brief オーバーレイを描画する
	/// @param screen 描画先 Screen
	/// @param validator 検証結果を持つバリデーター
	void drawOverlay(Screen& screen, const DrawCallValidator& validator) const
	{
		if (!m_enabled) return;

		// バリデーターを一時的に無効化（オーバーレイ自体の描画を検証しない）
		auto* savedValidator = &validator;
		static_cast<void>(savedValidator); // オーバーレイ描画はバリデーション対象外

		const float sw = static_cast<float>(screen.width());
		const float sh = static_cast<float>(screen.height());

		// ── 画面境界（緑の枠） ──────────────────────────────
		const sgc::Colorf green{0.0f, 0.8f, 0.0f, 0.6f};
		const float borderW = 2.0f;
		screen.drawRect(sgc::Rectf{0.0f, 0.0f, sw, borderW}, green);             // top
		screen.drawRect(sgc::Rectf{0.0f, sh - borderW, sw, borderW}, green);     // bottom
		screen.drawRect(sgc::Rectf{0.0f, 0.0f, borderW, sh}, green);             // left
		screen.drawRect(sgc::Rectf{sw - borderW, 0.0f, borderW, sh}, green);     // right

		// ── 描画コール矩形 ──────────────────────────────────
		const sgc::Colorf blueOutline{0.3f, 0.5f, 1.0f, 0.3f};
		const sgc::Colorf yellowOutline{1.0f, 1.0f, 0.0f, 0.4f};

		for (const auto& [rect, isText] : m_recordedRects)
		{
			const auto& outlineColor = isText ? yellowOutline : blueOutline;
			const float t = 1.0f; // outline thickness

			// 枠線を4本の細い矩形で描画
			screen.drawRect(sgc::Rectf{rect.x(), rect.y(), rect.width(), t}, outlineColor);
			screen.drawRect(sgc::Rectf{rect.x(), rect.y() + rect.height() - t, rect.width(), t}, outlineColor);
			screen.drawRect(sgc::Rectf{rect.x(), rect.y(), t, rect.height()}, outlineColor);
			screen.drawRect(sgc::Rectf{rect.x() + rect.width() - t, rect.y(), t, rect.height()}, outlineColor);
		}

		// ── はみ出し箇所（赤ハイライト） ─────────────────────
		const sgc::Colorf redHighlight{1.0f, 0.0f, 0.0f, 0.4f};
		for (const auto& issue : validator.getIssues())
		{
			if (issue.type == IssueType::OutOfBounds ||
			    issue.type == IssueType::PartialOverflow)
			{
				// 画面内に収まる部分だけ描画
				const float x1 = std::max(0.0f, issue.rect.x());
				const float y1 = std::max(0.0f, issue.rect.y());
				const float x2 = std::min(sw, issue.rect.x() + issue.rect.width());
				const float y2 = std::min(sh, issue.rect.y() + issue.rect.height());
				if (x2 > x1 && y2 > y1)
				{
					screen.drawRect(sgc::Rectf{x1, y1, x2 - x1, y2 - y1}, redHighlight);
				}

				// 画面外にはみ出した方向にマーカーを描画
				if (issue.rect.x() < 0.0f)
				{
					screen.drawRect(sgc::Rectf{0.0f, y1, 4.0f, y2 - y1}, redHighlight);
				}
				if (issue.rect.x() + issue.rect.width() > sw)
				{
					screen.drawRect(sgc::Rectf{sw - 4.0f, y1, 4.0f, y2 - y1}, redHighlight);
				}
				if (issue.rect.y() < 0.0f)
				{
					screen.drawRect(sgc::Rectf{x1, 0.0f, x2 - x1, 4.0f}, redHighlight);
				}
				if (issue.rect.y() + issue.rect.height() > sh)
				{
					screen.drawRect(sgc::Rectf{x1, sh - 4.0f, x2 - x1, 4.0f}, redHighlight);
				}
			}
		}

		// ── FPS + 描画コール数（左上） ───────────────────────
		{
			const sgc::Colorf bgColor{0.0f, 0.0f, 0.0f, 0.7f};
			const sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};

			screen.drawRect(sgc::Rectf{4.0f, 4.0f, 200.0f, 36.0f}, bgColor);

			const std::string fpsText = "FPS: " + std::to_string(static_cast<int>(m_fps));
			screen.drawText({8.0f, 6.0f}, fpsText, textColor, 8.0f);

			const std::string drawText = "Draws: " +
				std::to_string(validator.stats().totalDrawCalls);
			screen.drawText({8.0f, 20.0f}, drawText, textColor, 8.0f);
		}

		// ── 問題リスト（下パネル） ───────────────────────────
		const auto& issues = validator.getIssues();
		if (!issues.empty())
		{
			const int maxShow = std::min(static_cast<int>(issues.size()), 5);
			const float panelH = static_cast<float>(maxShow) * 14.0f + 20.0f;
			const float panelY = sh - panelH - 4.0f;

			const sgc::Colorf panelBg{0.0f, 0.0f, 0.0f, 0.8f};
			screen.drawRect(sgc::Rectf{4.0f, panelY, sw - 8.0f, panelH}, panelBg);

			const sgc::Colorf titleColor{1.0f, 0.6f, 0.0f, 1.0f};
			screen.drawText(
				{8.0f, panelY + 2.0f},
				"Issues: " + std::to_string(issues.size()),
				titleColor, 8.0f);

			const sgc::Colorf warnColor{1.0f, 1.0f, 0.0f, 1.0f};
			const sgc::Colorf errColor{1.0f, 0.3f, 0.3f, 1.0f};

			for (int i = 0; i < maxShow; ++i)
			{
				const auto& issue = issues[static_cast<std::size_t>(i)];
				const auto& color = (issue.severity == IssueSeverity::Error)
					? errColor : warnColor;

				// メッセージを短縮
				std::string msg = issue.message;
				if (msg.size() > 80)
				{
					msg = msg.substr(0, 77) + "...";
				}

				screen.drawText(
					{12.0f, panelY + 16.0f + static_cast<float>(i) * 14.0f},
					msg, color, 8.0f);
			}
		}
	}

private:
	bool m_enabled = false;
	float m_fps = 0.0f;

	struct RecordedRect
	{
		sgc::Rectf bounds;
		bool isText = false;
	};
	std::vector<RecordedRect> m_recordedRects;
};

} // namespace mitiru::validate
