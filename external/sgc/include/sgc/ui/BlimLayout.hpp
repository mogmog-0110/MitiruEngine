#pragma once

/// @file BlimLayout.hpp
/// @brief BLIM式マルチパネルレイアウト
///
/// 動画配信・実況でよく使われるBLIM式の画面分割レイアウト。
/// メイン画面（ゲームビュー）+ サブパネル（数式ビルダー等）
/// + ボトムバー（数式プレビュー）+ キャラクタースロット を計算する。
///
/// @code
/// sgc::BlimLayoutConfig config;
/// config.mainRatio = 0.65f;
/// config.sidebarWidth = 300.0f;
/// config.bottomHeight = 60.0f;
///
/// auto layout = sgc::computeBlimLayout(1280.0f, 720.0f, config);
/// // layout.mainPanel  → ゲームビュー
/// // layout.sidePanel  → 数式ビルダー
/// // layout.bottomBar  → 式プレビュー
/// // layout.characterSlot → キャラ立ち絵
/// @endcode

#include "sgc/math/Rect.hpp"

namespace sgc
{

/// @brief BLIM式レイアウトの設定
struct BlimLayoutConfig
{
	/// @brief メインパネルの幅比率（0.0〜1.0、残りがサイドバー）
	float mainRatio = 0.65f;

	/// @brief サイドバー固定幅（0より大きい場合、mainRatioを無視してこちらを使用）
	float sidebarWidth = 0.0f;

	/// @brief ボトムバーの高さ
	float bottomHeight = 60.0f;

	/// @brief キャラクタースロットのサイズ（正方形、0で無効化）
	float characterSize = 80.0f;

	/// @brief パネル間の余白
	float padding = 4.0f;

	/// @brief 外側の余白
	float margin = 0.0f;

	/// @brief サイドパネルを右に表示するか（falseで左）
	bool sideOnRight = true;

	/// @brief ボトムバーがメインパネルだけにかかるか（falseで画面全幅）
	bool bottomMainOnly = false;

	/// @brief キャラクタースロットの位置（メインパネル左下基準）
	Vec2f characterOffset{8.0f, -8.0f};
};

/// @brief BLIM式レイアウトの計算結果
struct BlimLayoutResult
{
	Rectf mainPanel;       ///< メインパネル（ゲームビュー）
	Rectf sidePanel;       ///< サイドパネル（数式ビルダー等）
	Rectf bottomBar;       ///< ボトムバー（式プレビュー/ステータス）
	Rectf characterSlot;   ///< キャラクタースロット（立ち絵）
	Rectf fullArea;        ///< 利用可能な全体領域（margin適用後）
};

/// @brief BLIM式レイアウトを計算する
/// @param screenW 画面幅
/// @param screenH 画面高さ
/// @param config レイアウト設定
/// @return 各パネルの矩形
///
/// @code
/// auto layout = sgc::computeBlimLayout(1280.0f, 720.0f, {});
/// @endcode
[[nodiscard]] inline BlimLayoutResult computeBlimLayout(
	float screenW, float screenH, const BlimLayoutConfig& config)
{
	BlimLayoutResult result;

	// マージン適用後の全体領域
	const float m = config.margin;
	const float areaX = m;
	const float areaY = m;
	const float areaW = screenW - m * 2.0f;
	const float areaH = screenH - m * 2.0f;
	result.fullArea = Rectf{areaX, areaY, areaW, areaH};

	const float pad = config.padding;

	// サイドバー幅の決定
	float sideW = config.sidebarWidth;
	if (sideW <= 0.0f)
	{
		sideW = areaW * (1.0f - config.mainRatio);
	}

	// メインパネル幅
	const float mainW = areaW - sideW - pad;

	// ボトムバーの高さ（0以下なら無効）
	const float bottomH = (config.bottomHeight > 0.0f) ? config.bottomHeight : 0.0f;

	// メインパネルの高さ（ボトムバー分を引く）
	const float mainH = areaH - ((bottomH > 0.0f) ? (bottomH + pad) : 0.0f);

	// メインパネルの位置（サイドの左右に応じて変わる）
	float mainX = areaX;
	float sideX = areaX + mainW + pad;

	if (!config.sideOnRight)
	{
		sideX = areaX;
		mainX = areaX + sideW + pad;
	}

	result.mainPanel = Rectf{mainX, areaY, mainW, mainH};
	result.sidePanel = Rectf{sideX, areaY, sideW, mainH};

	// ボトムバー
	if (bottomH > 0.0f)
	{
		const float bottomY = areaY + mainH + pad;
		if (config.bottomMainOnly)
		{
			result.bottomBar = Rectf{mainX, bottomY, mainW, bottomH};
		}
		else
		{
			result.bottomBar = Rectf{areaX, bottomY, areaW, bottomH};
		}
	}

	// キャラクタースロット（メインパネル左下基準）
	if (config.characterSize > 0.0f)
	{
		const float charX = result.mainPanel.x() + config.characterOffset.x;
		const float charY = result.mainPanel.bottom()
			+ config.characterOffset.y - config.characterSize;
		result.characterSlot = Rectf{
			charX, charY, config.characterSize, config.characterSize};
	}

	return result;
}

/// @brief サイドパネルを上下に分割する
/// @param sidePanel サイドパネル矩形
/// @param topRatio 上パネルの高さ比率（0.0〜1.0）
/// @param gap パネル間の余白
/// @return {上パネル, 下パネル} のペア
[[nodiscard]] inline std::pair<Rectf, Rectf> splitSidePanelVertical(
	const Rectf& sidePanel, float topRatio = 0.6f, float gap = 4.0f)
{
	const float topH = (sidePanel.height() - gap) * topRatio;
	const float bottomH = sidePanel.height() - gap - topH;

	Rectf top{sidePanel.x(), sidePanel.y(), sidePanel.width(), topH};
	Rectf bottom{sidePanel.x(), sidePanel.y() + topH + gap, sidePanel.width(), bottomH};
	return {top, bottom};
}

/// @brief パネル内にパディングを適用した内部領域を返す
/// @param panel パネル矩形
/// @param padding パディング量
/// @return パディング適用後の矩形
[[nodiscard]] inline constexpr Rectf panelContent(const Rectf& panel, float padding = 8.0f)
{
	return Rectf{
		panel.x() + padding,
		panel.y() + padding,
		panel.width() - padding * 2.0f,
		panel.height() - padding * 2.0f
	};
}

/// @brief ヘッダー付きパネルのヘッダーとコンテンツ領域を計算する
/// @param panel パネル矩形
/// @param headerHeight ヘッダーの高さ
/// @param gap ヘッダーとコンテンツの間の余白
/// @return {ヘッダー矩形, コンテンツ矩形} のペア
[[nodiscard]] inline std::pair<Rectf, Rectf> panelWithHeader(
	const Rectf& panel, float headerHeight = 28.0f, float gap = 2.0f)
{
	Rectf header{panel.x(), panel.y(), panel.width(), headerHeight};
	Rectf content{
		panel.x(),
		panel.y() + headerHeight + gap,
		panel.width(),
		panel.height() - headerHeight - gap
	};
	return {header, content};
}

} // namespace sgc
