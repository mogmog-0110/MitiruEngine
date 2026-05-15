#pragma once

/// @file UIAnchor.hpp
/// @brief スクリーンエッジアンカリングシステム
/// @details UIノードをスクリーン端や親ノードに相対的に配置する。
///          画面リサイズ時に全アンカー付きノードを再計算し、
///          レスポンシブなUI配置を実現する。

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

// ── アンカープリセット ──────────────────────────────────────────

/// @brief 画面端アンカーのプリセット
enum class AnchorPreset : std::uint8_t
{
	TopLeft,            ///< 左上固定
	TopCenter,          ///< 上中央固定
	TopRight,           ///< 右上固定
	CenterLeft,         ///< 左中央固定
	Center,             ///< 中央固定
	CenterRight,        ///< 右中央固定
	BottomLeft,         ///< 左下固定
	BottomCenter,       ///< 下中央固定
	BottomRight,        ///< 右下固定
	StretchHorizontal,  ///< 水平ストレッチ（上下マージン保持）
	StretchVertical,    ///< 垂直ストレッチ（左右マージン保持）
	StretchAll,         ///< 全方向ストレッチ
};

// ── アンカー設定 ────────────────────────────────────────────────

/// @brief ノード単位のアンカー設定
struct UIAnchorConfig
{
	AnchorPreset preset = AnchorPreset::TopLeft; ///< アンカープリセット

	float marginLeft   = 0.0f; ///< 左マージン（ピクセル）
	float marginRight  = 0.0f; ///< 右マージン（ピクセル）
	float marginTop    = 0.0f; ///< 上マージン（ピクセル）
	float marginBottom = 0.0f; ///< 下マージン（ピクセル）

	float pivotX = 0.0f; ///< ピボットX（0.0=左端、0.5=中央、1.0=右端）
	float pivotY = 0.0f; ///< ピボットY（0.0=上端、0.5=中央、1.0=下端）

	bool keepAspectRatio = false; ///< アスペクト比維持フラグ（ストレッチ時）

	/// @brief 均一マージンの簡易設定
	/// @param preset アンカープリセット
	/// @param margin 全辺マージン
	/// @return UIAnchorConfig
	[[nodiscard]] static UIAnchorConfig uniform(AnchorPreset preset, float margin) noexcept
	{
		UIAnchorConfig cfg;
		cfg.preset = preset;
		cfg.marginLeft = margin;
		cfg.marginRight = margin;
		cfg.marginTop = margin;
		cfg.marginBottom = margin;
		return cfg;
	}

	/// @brief ピボットを中央に設定する
	/// @return 自身のコピー
	[[nodiscard]] UIAnchorConfig withCenterPivot() const noexcept
	{
		UIAnchorConfig copy = *this;
		copy.pivotX = 0.5f;
		copy.pivotY = 0.5f;
		return copy;
	}
};

// ── 解決済みバウンズ ────────────────────────────────────────────

/// @brief アンカー解決後のバウンズ
struct UIResolvedBounds
{
	UINodeId nodeId = 0;     ///< ノードID
	sgc::Rectf bounds;       ///< 解決済みバウンズ
	int zOrder = 0;          ///< Z順序
};

// ── アンカーエントリ ────────────────────────────────────────────

/// @brief 内部管理用エントリ
struct UIAnchorEntry
{
	UINode* node = nullptr;        ///< 対象ノード
	UIAnchorConfig config;         ///< アンカー設定
	UINode* parentNode = nullptr;  ///< 親ノード（nullptr=スクリーン基準）
	float elementW = 0.0f;         ///< 要素幅（ストレッチ時は無視）
	float elementH = 0.0f;         ///< 要素高さ（ストレッチ時は無視）
	int zOrder = 100;              ///< Z順序（デフォルトで手前寄り）
};

// ── UIAnchorLayout ──────────────────────────────────────────────

/// @brief スクリーンアンカーレイアウト管理クラス
/// @details ノードにアンカー設定を付与し、resolve()で画面サイズに応じた
///          最終バウンズを計算する。画面リサイズ時に再計算することで
///          レスポンシブな配置を実現する。
///
/// @code
/// mitiru::ui::UIAnchorLayout anchorLayout;
///
/// mitiru::ui::UIAnchorConfig hpConfig;
/// hpConfig.preset = mitiru::ui::AnchorPreset::TopLeft;
/// hpConfig.marginLeft = 10.0f;
/// hpConfig.marginTop = 10.0f;
/// anchorLayout.setAnchor(hpBarNode, hpConfig, 200.0f, 24.0f);
///
/// mitiru::ui::UIAnchorConfig minimapConfig;
/// minimapConfig.preset = mitiru::ui::AnchorPreset::BottomRight;
/// minimapConfig.marginRight = 10.0f;
/// minimapConfig.marginBottom = 10.0f;
/// anchorLayout.setAnchor(minimapNode, minimapConfig, 160.0f, 160.0f);
///
/// // 画面リサイズ時やフレーム更新時
/// auto resolved = anchorLayout.resolve(1920.0f, 1080.0f);
/// @endcode
class UIAnchorLayout
{
	std::vector<UIAnchorEntry> m_entries;

public:
	/// @brief ノードにアンカーを設定する（スクリーン基準）
	/// @param node 対象ノード
	/// @param config アンカー設定
	/// @param elementW 要素幅
	/// @param elementH 要素高さ
	/// @param zOrder Z順序
	void setAnchor(UINode& node, const UIAnchorConfig& config,
	               float elementW = 0.0f, float elementH = 0.0f,
	               int zOrder = 100)
	{
		// 既存エントリの更新チェック
		for (auto& entry : m_entries)
		{
			if (entry.node && entry.node->id() == node.id())
			{
				entry.config = config;
				entry.elementW = (elementW > 0.0f) ? elementW : node.bounds().width();
				entry.elementH = (elementH > 0.0f) ? elementH : node.bounds().height();
				entry.zOrder = zOrder;
				entry.parentNode = nullptr;
				return;
			}
		}

		UIAnchorEntry entry;
		entry.node = &node;
		entry.config = config;
		entry.elementW = (elementW > 0.0f) ? elementW : node.bounds().width();
		entry.elementH = (elementH > 0.0f) ? elementH : node.bounds().height();
		entry.zOrder = zOrder;
		m_entries.push_back(entry);
	}

	/// @brief ノードにアンカーを設定する（親ノード基準）
	/// @param node 対象ノード
	/// @param parent 親ノード
	/// @param config アンカー設定
	/// @param elementW 要素幅
	/// @param elementH 要素高さ
	/// @param zOrder Z順序
	void setAnchorRelative(UINode& node, UINode& parent,
	                       const UIAnchorConfig& config,
	                       float elementW = 0.0f, float elementH = 0.0f,
	                       int zOrder = 100)
	{
		for (auto& entry : m_entries)
		{
			if (entry.node && entry.node->id() == node.id())
			{
				entry.config = config;
				entry.parentNode = &parent;
				entry.elementW = (elementW > 0.0f) ? elementW : node.bounds().width();
				entry.elementH = (elementH > 0.0f) ? elementH : node.bounds().height();
				entry.zOrder = zOrder;
				return;
			}
		}

		UIAnchorEntry entry;
		entry.node = &node;
		entry.config = config;
		entry.parentNode = &parent;
		entry.elementW = (elementW > 0.0f) ? elementW : node.bounds().width();
		entry.elementH = (elementH > 0.0f) ? elementH : node.bounds().height();
		entry.zOrder = zOrder;
		m_entries.push_back(entry);
	}

	/// @brief ノードのアンカーを解除する
	/// @param nodeId 対象ノードID
	void removeAnchor(UINodeId nodeId)
	{
		m_entries.erase(
			std::remove_if(m_entries.begin(), m_entries.end(),
				[nodeId](const UIAnchorEntry& e) {
					return e.node && e.node->id() == nodeId;
				}),
			m_entries.end());
	}

	/// @brief 全アンカーを解決し、ノードにバウンズを適用する
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	/// @return 解決済みバウンズのリスト（Z順序でソート済み）
	[[nodiscard]] std::vector<UIResolvedBounds> resolve(float screenW, float screenH)
	{
		std::vector<UIResolvedBounds> results;
		results.reserve(m_entries.size());

		for (auto& entry : m_entries)
		{
			if (!entry.node) continue;

			// 参照領域の決定
			sgc::Rectf refBounds{0.0f, 0.0f, screenW, screenH};
			if (entry.parentNode)
			{
				refBounds = entry.parentNode->bounds();
			}

			const auto resolved = resolveEntry(entry, refBounds);

			// ノードにバウンズ適用
			entry.node->setBounds(resolved);
			entry.node->setZIndex(entry.zOrder);

			UIResolvedBounds rb;
			rb.nodeId = entry.node->id();
			rb.bounds = resolved;
			rb.zOrder = entry.zOrder;
			results.push_back(rb);
		}

		// Z順序でソート（低い値が奥）
		std::sort(results.begin(), results.end(),
			[](const UIResolvedBounds& a, const UIResolvedBounds& b)
			{
				return a.zOrder < b.zOrder;
			});

		return results;
	}

	/// @brief アンカー付きノード数を取得する
	[[nodiscard]] std::size_t count() const noexcept { return m_entries.size(); }

	/// @brief 全アンカーをクリアする
	void clear() { m_entries.clear(); }

private:
	/// @brief 個別エントリのバウンズを解決する
	[[nodiscard]] static sgc::Rectf resolveEntry(
		const UIAnchorEntry& entry, const sgc::Rectf& ref)
	{
		const auto& cfg = entry.config;
		const float refX = ref.x();
		const float refY = ref.y();
		const float refW = ref.width();
		const float refH = ref.height();

		float w = entry.elementW;
		float h = entry.elementH;
		float x = 0.0f;
		float y = 0.0f;

		switch (cfg.preset)
		{
		case AnchorPreset::TopLeft:
			x = refX + cfg.marginLeft;
			y = refY + cfg.marginTop;
			break;

		case AnchorPreset::TopCenter:
			x = refX + (refW - w) * 0.5f;
			y = refY + cfg.marginTop;
			break;

		case AnchorPreset::TopRight:
			x = refX + refW - w - cfg.marginRight;
			y = refY + cfg.marginTop;
			break;

		case AnchorPreset::CenterLeft:
			x = refX + cfg.marginLeft;
			y = refY + (refH - h) * 0.5f;
			break;

		case AnchorPreset::Center:
			x = refX + (refW - w) * 0.5f;
			y = refY + (refH - h) * 0.5f;
			break;

		case AnchorPreset::CenterRight:
			x = refX + refW - w - cfg.marginRight;
			y = refY + (refH - h) * 0.5f;
			break;

		case AnchorPreset::BottomLeft:
			x = refX + cfg.marginLeft;
			y = refY + refH - h - cfg.marginBottom;
			break;

		case AnchorPreset::BottomCenter:
			x = refX + (refW - w) * 0.5f;
			y = refY + refH - h - cfg.marginBottom;
			break;

		case AnchorPreset::BottomRight:
			x = refX + refW - w - cfg.marginRight;
			y = refY + refH - h - cfg.marginBottom;
			break;

		case AnchorPreset::StretchHorizontal:
			x = refX + cfg.marginLeft;
			y = refY + cfg.marginTop;
			w = refW - cfg.marginLeft - cfg.marginRight;
			if (cfg.keepAspectRatio && entry.elementW > 0.0f)
			{
				h = w * (entry.elementH / entry.elementW);
			}
			break;

		case AnchorPreset::StretchVertical:
			x = refX + cfg.marginLeft;
			y = refY + cfg.marginTop;
			h = refH - cfg.marginTop - cfg.marginBottom;
			if (cfg.keepAspectRatio && entry.elementH > 0.0f)
			{
				w = h * (entry.elementW / entry.elementH);
			}
			break;

		case AnchorPreset::StretchAll:
			x = refX + cfg.marginLeft;
			y = refY + cfg.marginTop;
			w = refW - cfg.marginLeft - cfg.marginRight;
			h = refH - cfg.marginTop - cfg.marginBottom;
			break;
		}

		// ピボット適用
		x -= w * cfg.pivotX;
		y -= h * cfg.pivotY;

		// 最小サイズ保証
		w = std::max(0.0f, w);
		h = std::max(0.0f, h);

		return sgc::Rectf{x, y, w, h};
	}
};

} // namespace mitiru::ui
