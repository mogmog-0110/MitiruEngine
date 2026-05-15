#pragma once

/// @file ProfilerGUI.hpp
/// @brief ビジュアルプロファイラー（フレームグラフ付き）
/// @details フレーム毎のセクション計測を行い、オーバーレイとして
///          FPSカウンター・フレーム時間グラフ・セクションバーチャートを描画する。
///
/// @code
/// mitiru::debug::ProfilerGUI profiler;
/// profiler.beginFrame();
///   profiler.beginSection("Render");
///   // ... render work ...
///   profiler.endSection();
/// profiler.endFrame();
/// profiler.drawOverlay(screen);
/// @endcode

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sgc/types/Color.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::debug
{

/// @brief セクション計測データ
struct SectionTiming
{
	std::string name;           ///< セクション名
	float durationMs = 0.0f;    ///< 今フレームの所要時間（ミリ秒）
	float minMs = 1e9f;         ///< 最小所要時間
	float maxMs = 0.0f;         ///< 最大所要時間
	float avgMs = 0.0f;         ///< 平均所要時間
	float totalMs = 0.0f;       ///< 累計所要時間
	std::uint32_t sampleCount = 0; ///< サンプル数
	int depth = 0;              ///< ネスト深度
};

/// @brief フレームデータ
struct FrameData
{
	float frameDurationMs = 0.0f;                  ///< フレーム全体の所要時間
	std::vector<SectionTiming> sections;           ///< セクション計測結果
};

/// @brief ビジュアルプロファイラー
/// @details フレーム単位のセクション計測・統計・オーバーレイ描画を行う。
class ProfilerGUI
{
public:
	/// @brief オーバーレイ表示位置
	enum class OverlayPosition : std::uint8_t
	{
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
	};

	/// @brief コンストラクタ
	/// @param historySize フレーム時間の履歴サイズ（デフォルト300フレーム）
	explicit ProfilerGUI(std::size_t historySize = 300)
		: m_historySize(historySize)
	{
		m_frameHistory.reserve(historySize);
	}

	// ── フレーム計測 ──

	/// @brief フレーム計測を開始する
	void beginFrame()
	{
		m_frameStart = Clock::now();
		m_currentSections.clear();
		m_sectionStack.clear();
	}

	/// @brief フレーム計測を終了する
	void endFrame()
	{
		const auto now = Clock::now();
		const float frameMs = toMs(m_frameStart, now);

		// フレーム履歴をリングバッファに記録する
		if (m_frameHistory.size() >= m_historySize)
		{
			m_frameHistory.erase(m_frameHistory.begin());
		}
		m_frameHistory.push_back(frameMs);

		// セクション統計を更新する
		for (auto& [name, timing] : m_currentSections)
		{
			auto& stats = m_sectionStats[name];
			stats.name = name;
			stats.durationMs = timing.durationMs;
			stats.minMs = std::min(stats.minMs, timing.durationMs);
			stats.maxMs = std::max(stats.maxMs, timing.durationMs);
			stats.totalMs += timing.durationMs;
			++stats.sampleCount;
			stats.avgMs = stats.totalMs / static_cast<float>(stats.sampleCount);
			stats.depth = timing.depth;
		}

		m_lastFrameMs = frameMs;
	}

	/// @brief セクション計測を開始する
	/// @param name セクション名
	void beginSection(std::string_view name)
	{
		const int depth = static_cast<int>(m_sectionStack.size());
		m_sectionStack.push_back(SectionEntry{std::string(name), Clock::now(), depth});
	}

	/// @brief セクション計測を終了する
	void endSection()
	{
		if (m_sectionStack.empty()) return;

		const auto entry = m_sectionStack.back();
		m_sectionStack.pop_back();

		const float durationMs = toMs(entry.startTime, Clock::now());
		auto& timing = m_currentSections[entry.name];
		timing.name = entry.name;
		timing.durationMs += durationMs;
		timing.depth = entry.depth;
	}

	/// @brief 最新フレームのデータを取得する
	/// @return フレームデータ
	[[nodiscard]] FrameData getFrameData() const
	{
		FrameData data;
		data.frameDurationMs = m_lastFrameMs;
		data.sections.reserve(m_sectionStats.size());
		for (const auto& [name, stats] : m_sectionStats)
		{
			data.sections.push_back(stats);
		}
		return data;
	}

	// ── 描画 ──

	/// @brief オーバーレイ描画（FPSカウンター・フレーム時間グラフ・上位Nセクション）
	/// @param screen 描画先スクリーン
	void drawOverlay(Screen& screen) const
	{
		if (!m_visible) return;

		const float ox = overlayX(screen);
		const float oy = overlayY(screen);

		// 背景パネル
		const sgc::Colorf bgColor{0.0f, 0.0f, 0.0f, m_transparency};
		screen.drawRect(sgc::Rectf{ox, oy, kOverlayWidth, kOverlayHeight}, bgColor);

		// FPSカウンター
		const float fps = (m_lastFrameMs > 0.001f) ? (1000.0f / m_lastFrameMs) : 0.0f;
		const sgc::Colorf fpsColor = fpsToColor(fps);
		const std::string fpsText = "FPS: " + formatFloat(fps, 1) + "  (" + formatFloat(m_lastFrameMs, 2) + "ms)";
		screen.drawText({ox + 4.0f, oy + 4.0f}, fpsText, fpsColor, 16.0f);

		// フレーム時間グラフ
		drawFrameGraph(screen, ox + 4.0f, oy + 24.0f, kOverlayWidth - 8.0f, 60.0f);

		// 上位セクションバーチャート
		drawSectionBars(screen, ox + 4.0f, oy + 90.0f, kOverlayWidth - 8.0f, m_topNSections);
	}

	/// @brief 詳細タイムライン描画（セクションネスト・GPU/CPU分割表示）
	/// @param screen 描画先スクリーン
	void drawDetailed(Screen& screen) const
	{
		if (!m_visible) return;

		const float ox = 10.0f;
		const float oy = 10.0f;
		const float panelW = static_cast<float>(screen.width()) - 20.0f;
		const float panelH = static_cast<float>(screen.height()) - 20.0f;

		// 背景
		const sgc::Colorf bgColor{0.05f, 0.05f, 0.1f, m_transparency};
		screen.drawRect(sgc::Rectf{ox, oy, panelW, panelH}, bgColor);

		// タイトル
		screen.drawText({ox + 8.0f, oy + 8.0f}, "Profiler Timeline", {1.0f, 1.0f, 1.0f, 1.0f}, 16.0f);

		// タイムライン（セクション毎にネスト深度に応じた帯を描画）
		const float timelineY = oy + 32.0f;
		const float timelineH = panelH - 44.0f;
		const float barHeight = 20.0f;
		float curY = timelineY;

		// フレーム時間を基準にスケーリングする
		const float maxMs = std::max(m_lastFrameMs, 1.0f);
		const float scaleX = (panelW - 80.0f) / maxMs;

		for (const auto& [name, stats] : m_sectionStats)
		{
			if (curY + barHeight > oy + panelH) break;

			const float indent = static_cast<float>(stats.depth) * 16.0f;
			const float barW = std::max(2.0f, stats.durationMs * scaleX);
			const sgc::Colorf sectionColor = sectionToColor(name);

			// セクションバー
			screen.drawRect(
				sgc::Rectf{ox + 80.0f + indent, curY, barW, barHeight - 2.0f},
				sectionColor);

			// セクション名とタイミング
			const std::string label = name + " " + formatFloat(stats.durationMs, 2) + "ms";
			screen.drawText(
				{ox + 82.0f + indent, curY + 2.0f},
				label, {1.0f, 1.0f, 1.0f, 0.9f}, 8.0f);

			// 統計（min/max/avg）
			const std::string statsText =
				"min:" + formatFloat(stats.minMs, 2) +
				" max:" + formatFloat(stats.maxMs, 2) +
				" avg:" + formatFloat(stats.avgMs, 2);
			screen.drawText(
				{ox + 82.0f + indent + barW + 4.0f, curY + 2.0f},
				statsText, {0.7f, 0.7f, 0.7f, 0.8f}, 8.0f);

			curY += barHeight;
		}
	}

	// ── 設定 ──

	/// @brief オーバーレイの表示/非表示を切り替える
	void setVisible(bool visible) noexcept { m_visible = visible; }

	/// @brief オーバーレイの表示状態を取得する
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief オーバーレイ表示位置を設定する
	void setOverlayPosition(OverlayPosition pos) noexcept { m_position = pos; }

	/// @brief オーバーレイ透明度を設定する（0.0 = 完全透明, 1.0 = 不透明）
	void setTransparency(float alpha) noexcept { m_transparency = std::clamp(alpha, 0.0f, 1.0f); }

	/// @brief 表示するトップNセクション数を設定する
	void setTopNSections(int n) noexcept { m_topNSections = std::max(1, n); }

	/// @brief 統計をリセットする
	void reset()
	{
		m_frameHistory.clear();
		m_sectionStats.clear();
		m_lastFrameMs = 0.0f;
	}

private:
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = Clock::time_point;

	/// @brief セクションスタックエントリ
	struct SectionEntry
	{
		std::string name;
		TimePoint startTime;
		int depth = 0;
	};

	static constexpr float kOverlayWidth = 280.0f;
	static constexpr float kOverlayHeight = 200.0f;

	std::size_t m_historySize;
	std::vector<float> m_frameHistory;                             ///< フレーム時間リングバッファ
	std::unordered_map<std::string, SectionTiming> m_sectionStats; ///< セクション統計
	std::unordered_map<std::string, SectionTiming> m_currentSections; ///< 今フレームのセクション

	std::vector<SectionEntry> m_sectionStack;                      ///< セクション計測スタック
	TimePoint m_frameStart{};                                      ///< フレーム開始時刻
	float m_lastFrameMs = 0.0f;                                    ///< 最新フレーム時間

	bool m_visible = true;
	OverlayPosition m_position = OverlayPosition::TopRight;
	float m_transparency = 0.75f;
	int m_topNSections = 5;

	/// @brief 2つのTimePoint間のミリ秒を計算する
	[[nodiscard]] static float toMs(TimePoint start, TimePoint end) noexcept
	{
		const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
		return static_cast<float>(ns) / 1'000'000.0f;
	}

	/// @brief float値を指定桁数で文字列化する
	[[nodiscard]] static std::string formatFloat(float value, int decimals)
	{
		// 単純な固定小数点フォーマット
		const float factor = (decimals == 1) ? 10.0f : (decimals == 2) ? 100.0f : 1000.0f;
		const int rounded = static_cast<int>(value * factor + 0.5f);
		const int intPart = rounded / static_cast<int>(factor);
		const int fracPart = rounded % static_cast<int>(factor);

		std::string result = std::to_string(intPart) + ".";
		const std::string frac = std::to_string(fracPart);
		for (int i = static_cast<int>(frac.size()); i < decimals; ++i)
		{
			result += '0';
		}
		result += frac;
		return result;
	}

	/// @brief FPS値に応じた色を返す
	[[nodiscard]] static sgc::Colorf fpsToColor(float fps) noexcept
	{
		if (fps >= 55.0f) return {0.0f, 1.0f, 0.3f, 1.0f};  // 緑
		if (fps >= 30.0f) return {1.0f, 1.0f, 0.0f, 1.0f};  // 黄
		return {1.0f, 0.2f, 0.2f, 1.0f};                      // 赤
	}

	/// @brief セクション名に応じた色を返す
	[[nodiscard]] static sgc::Colorf sectionToColor(const std::string& name) noexcept
	{
		// カテゴリ別の色割り当て
		if (name.find("Render") != std::string::npos || name.find("render") != std::string::npos
		    || name.find("Draw") != std::string::npos)
		{
			return {0.3f, 0.5f, 1.0f, 0.85f};   // 青: レンダリング
		}
		if (name.find("Physics") != std::string::npos || name.find("physics") != std::string::npos
		    || name.find("Collision") != std::string::npos)
		{
			return {0.2f, 0.9f, 0.3f, 0.85f};   // 緑: 物理
		}
		if (name.find("Audio") != std::string::npos || name.find("audio") != std::string::npos
		    || name.find("Sound") != std::string::npos)
		{
			return {1.0f, 0.9f, 0.2f, 0.85f};   // 黄: オーディオ
		}
		if (name.find("Input") != std::string::npos || name.find("input") != std::string::npos)
		{
			return {0.9f, 0.4f, 0.1f, 0.85f};   // オレンジ: 入力
		}
		if (name.find("Script") != std::string::npos || name.find("script") != std::string::npos)
		{
			return {0.8f, 0.3f, 0.9f, 0.85f};   // 紫: スクリプト
		}
		// デフォルト: シアン
		return {0.4f, 0.8f, 0.8f, 0.85f};
	}

	/// @brief オーバーレイX座標を計算する
	[[nodiscard]] float overlayX(const Screen& screen) const noexcept
	{
		switch (m_position)
		{
		case OverlayPosition::TopLeft:
		case OverlayPosition::BottomLeft:
			return 8.0f;
		case OverlayPosition::TopRight:
		case OverlayPosition::BottomRight:
			return static_cast<float>(screen.width()) - kOverlayWidth - 8.0f;
		}
		return 8.0f;
	}

	/// @brief オーバーレイY座標を計算する
	[[nodiscard]] float overlayY(const Screen& screen) const noexcept
	{
		switch (m_position)
		{
		case OverlayPosition::TopLeft:
		case OverlayPosition::TopRight:
			return 8.0f;
		case OverlayPosition::BottomLeft:
		case OverlayPosition::BottomRight:
			return static_cast<float>(screen.height()) - kOverlayHeight - 8.0f;
		}
		return 8.0f;
	}

	/// @brief フレーム時間グラフを描画する
	void drawFrameGraph(Screen& screen, float x, float y, float w, float h) const
	{
		if (m_frameHistory.empty()) return;

		// 背景
		screen.drawRect(sgc::Rectf{x, y, w, h}, {0.1f, 0.1f, 0.1f, 0.5f});

		// 16.6msライン（60FPS基準）
		const float maxDisplayMs = 33.3f;
		const float line60 = y + h * (1.0f - 16.67f / maxDisplayMs);
		screen.drawLine({x, line60}, {x + w, line60}, {0.3f, 0.3f, 0.3f, 0.6f}, 1.0f);

		// フレーム時間バー
		const float barW = w / static_cast<float>(m_historySize);
		const auto startIdx = (m_frameHistory.size() > m_historySize)
			? m_frameHistory.size() - m_historySize : 0;

		for (std::size_t i = startIdx; i < m_frameHistory.size(); ++i)
		{
			const float ms = m_frameHistory[i];
			const float barH = std::min(h, h * (ms / maxDisplayMs));
			const float bx = x + static_cast<float>(i - startIdx) * barW;
			const float by = y + h - barH;

			screen.drawRect(
				sgc::Rectf{bx, by, std::max(1.0f, barW - 0.5f), barH},
				fpsToColor(1000.0f / std::max(ms, 0.001f)));
		}
	}

	/// @brief トップNセクションのバーチャートを描画する
	void drawSectionBars(Screen& screen, float x, float y, float w, int topN) const
	{
		// セクションを所要時間降順でソートする
		std::vector<const SectionTiming*> sorted;
		sorted.reserve(m_sectionStats.size());
		for (const auto& [name, stats] : m_sectionStats)
		{
			sorted.push_back(&stats);
		}
		std::sort(sorted.begin(), sorted.end(),
			[](const SectionTiming* a, const SectionTiming* b) {
				return a->durationMs > b->durationMs;
			});

		const float barH = 14.0f;
		const float maxMs = (sorted.empty()) ? 1.0f : std::max(sorted[0]->durationMs, 0.001f);
		float curY = y;

		for (int i = 0; i < topN && i < static_cast<int>(sorted.size()); ++i)
		{
			const auto& s = *sorted[static_cast<std::size_t>(i)];
			const float barW = (w - 4.0f) * (s.durationMs / maxMs);
			const sgc::Colorf color = sectionToColor(s.name);

			screen.drawRect(sgc::Rectf{x, curY, barW, barH - 2.0f}, color);

			const std::string label = s.name + " " + formatFloat(s.durationMs, 2) + "ms";
			screen.drawText({x + 2.0f, curY + 1.0f}, label, {1.0f, 1.0f, 1.0f, 0.9f}, 8.0f);

			curY += barH;
		}
	}
};

} // namespace mitiru::debug
