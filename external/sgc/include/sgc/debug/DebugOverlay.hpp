#pragma once

/// @file DebugOverlay.hpp
/// @brief デバッグオーバーレイ
///
/// FPSやカスタム情報を画面上に表示するためのデバッグ用UIクラス。
/// ITextRendererを通じてフレームワーク非依存の描画を行う。
///
/// @code
/// sgc::debug::DebugOverlay overlay;
/// overlay.setEntry("Entities", std::to_string(entityCount));
///
/// // ゲームループ
/// overlay.update(deltaTime);
/// overlay.draw(textRenderer);
/// @endcode

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "sgc/debug/FpsCounter.hpp"
#include "sgc/graphics/ITextRenderer.hpp"
#include "sgc/types/Color.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::debug
{

/// @brief デバッグオーバーレイエントリ
struct DebugEntry
{
	std::string label;   ///< ラベル
	std::string value;   ///< 値の文字列表現
};

/// @brief デバッグ情報オーバーレイ
///
/// FPSやカスタム情報を画面上に表示するためのデバッグ用UIクラス。
/// ITextRendererを通じてフレームワーク非依存の描画を行う。
class DebugOverlay
{
public:
	/// @brief FPSカウンターを更新する
	/// @param deltaTime フレーム経過時間（秒）
	void update(float deltaTime) noexcept
	{
		m_fpsCounter.update(deltaTime);
	}

	/// @brief カスタムエントリを設定する（同じラベルは上書き）
	/// @param label ラベル
	/// @param value 値の文字列表現
	void setEntry(const std::string& label, const std::string& value)
	{
		for (auto& entry : m_entries)
		{
			if (entry.label == label)
			{
				entry.value = value;
				return;
			}
		}
		m_entries.push_back({label, value});
	}

	/// @brief エントリを削除する
	/// @param label 削除するラベル
	void removeEntry(const std::string& label)
	{
		std::erase_if(m_entries, [&](const DebugEntry& e) { return e.label == label; });
	}

	/// @brief 全エントリをクリアする
	void clearEntries() { m_entries.clear(); }

	/// @brief オーバーレイを描画する
	/// @param textRenderer テキスト描画インターフェース
	/// @param topLeft 描画開始位置（左上）
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param lineSpacing 行間隔（ピクセル）
	void draw(ITextRenderer& textRenderer, const Vec2f& topLeft = {8.0f, 8.0f},
	          float fontSize = 16.0f, float lineSpacing = 20.0f) const
	{
		if (!m_visible) return;

		const Colorf color = Colorf{0.0f, 1.0f, 0.0f, 1.0f};  // 緑

		// FPS表示
		char fpsBuf[64]{};
		std::snprintf(fpsBuf, sizeof(fpsBuf), "%.1f", static_cast<double>(m_fpsCounter.fps()));
		std::string fpsText = std::string("FPS: ") + fpsBuf;

		char msBuf[64]{};
		std::snprintf(msBuf, sizeof(msBuf), "%.2f", static_cast<double>(m_fpsCounter.frameTimeMs()));
		fpsText += std::string(" (") + msBuf + "ms)";

		float y = topLeft.y;
		textRenderer.drawText(fpsText, fontSize, {topLeft.x, y}, color);
		y += lineSpacing;

		// カスタムエントリ
		for (const auto& entry : m_entries)
		{
			std::string text = entry.label + ": " + entry.value;
			textRenderer.drawText(text, fontSize, {topLeft.x, y}, color);
			y += lineSpacing;
		}
	}

	/// @brief 表示/非表示を切り替える
	/// @param visible trueで表示
	void setVisible(bool visible) noexcept { m_visible = visible; }

	/// @brief 表示中か
	/// @return 表示中ならtrue
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief 表示をトグルする
	void toggleVisible() noexcept { m_visible = !m_visible; }

	/// @brief FPSカウンターへの参照を取得する
	/// @return FPSカウンター（const参照）
	[[nodiscard]] const FpsCounter& fpsCounter() const noexcept { return m_fpsCounter; }

	/// @brief 登録されたエントリ数を返す
	/// @return エントリ数
	[[nodiscard]] std::size_t entryCount() const noexcept { return m_entries.size(); }

private:
	FpsCounter m_fpsCounter;             ///< FPSカウンター
	std::vector<DebugEntry> m_entries;   ///< カスタムデバッグエントリ
	bool m_visible{true};                ///< 表示フラグ
};

} // namespace sgc::debug
