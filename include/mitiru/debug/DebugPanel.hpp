#pragma once

/// @file DebugPanel.hpp
/// @brief エンジン統計デバッグパネル
/// @details IDebugOverlayを使用してエンジン統計情報（FPS、フレーム番号、
///          エンティティ数、描画コール数、入力状態、シーン情報）を表示する。

#include <string>

#include <mitiru/debug/IDebugOverlay.hpp>
#include <mitiru/core/Engine.hpp>

namespace mitiru::debug
{

/// @brief エンジン統計デバッグパネル
/// @details IDebugOverlay を通じてエンジン内部統計を視覚化する。
///          toggle() で表示/非表示を切り替えられる。
///
/// @code
/// mitiru::debug::NullDebugOverlay overlay;
/// mitiru::debug::DebugPanel panel(&overlay);
/// panel.toggle();  // 表示を有効化
/// // メインループ内で:
/// panel.draw(engine);
/// @endcode
class DebugPanel
{
public:
	/// @brief コンストラクタ
	/// @param overlay デバッグオーバーレイへのポインタ（非所有、nullptrを許容）
	explicit DebugPanel(IDebugOverlay* overlay) noexcept
		: m_overlay(overlay)
	{
	}

	/// @brief デバッグパネルを描画する
	/// @param engine エンジンの参照（統計情報を取得するため）
	void draw(const Engine& engine)
	{
		if (!m_visible || !m_overlay)
		{
			return;
		}

		m_overlay->beginFrame();

		if (m_overlay->beginWindow("Engine Stats"))
		{
			/// FPSを計算して表示する
			const auto* clk = engine.clock();
			if (clk)
			{
				const float elapsed = clk->elapsed();
				const auto frame = clk->frameNumber();

				/// FPS計算（フレーム番号 / 経過時間）
				const float fps = (elapsed > 0.0f)
					? static_cast<float>(frame) / elapsed
					: 0.0f;

				m_overlay->text("FPS", std::to_string(static_cast<int>(fps + 0.5f)));
				m_overlay->text("Frame", std::to_string(frame));
			}

			m_overlay->separator();

			/// 描画コール数を表示する
			const auto* scr = engine.screen();
			if (scr)
			{
				m_overlay->text("Draw Calls",
					std::to_string(scr->drawCallCount()));
			}

			m_overlay->separator();

			/// 入力状態サマリーを表示する
			const auto& input = engine.inputState();
			const auto mousePos = input.mousePosition();
			const std::string mouseStr =
				"(" + std::to_string(static_cast<int>(mousePos.first))
				+ ", " + std::to_string(static_cast<int>(mousePos.second)) + ")";
			m_overlay->text("Mouse", mouseStr);

			m_overlay->endWindow();
		}

		m_overlay->endFrame();
	}

	/// @brief 表示/非表示を切り替える
	void toggle() noexcept
	{
		m_visible = !m_visible;
	}

	/// @brief 表示状態を取得する
	/// @return 表示中であれば true
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_visible;
	}

private:
	IDebugOverlay* m_overlay = nullptr;  ///< デバッグオーバーレイ（非所有）
	bool m_visible = false;              ///< 表示状態
};

} // namespace mitiru::debug
