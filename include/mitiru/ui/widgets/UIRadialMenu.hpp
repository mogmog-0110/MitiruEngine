#pragma once

/// @file UIRadialMenu.hpp
/// @brief 素早い項目選択のための円形 / 扇形 radial menu widget。

#include <mitiru/ui/Easing.hpp>
#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief radial menu の開閉アニメーション種別。
enum class RadialMenuAnimation : std::uint8_t
{
	Scale, ///< 中心から外側へ拡大。
	Fade,  ///< フェードイン / アウト。
};

/// @brief radial menu の単一項目。
struct UIRadialMenuItem
{
	std::string label;                             ///< 表示ラベル。
	std::string iconImageKey;                      ///< renderer 用の icon image key。
	bool enabled            = true;                ///< この項目が選択可能か。
	std::string shortcutKey;                       ///< キーボード shortcut (例 "1", "Q")。
	std::vector<UIRadialMenuItem> subItems;        ///< 第 2 ring 用の入れ子 sub-item。
};

/// @brief radial menu の設定。
struct UIRadialMenuConfig
{
	float innerRadius           = 40.0f;           ///< 内半径 (dead zone 境界)。
	float outerRadius           = 150.0f;          ///< 外半径。
	std::vector<UIRadialMenuItem> items;            ///< menu 項目 (sector)。
	std::string centerIconImageKey;                ///< 中心領域の icon image key。
	std::string sectorBackgroundImageKey;          ///< sector 通常状態の image key。
	std::string sectorHoverImageKey;               ///< sector hover 状態の image key。
	std::string sectorDisabledImageKey;            ///< sector 無効状態の image key。
	float labelFontSize         = 14.0f;           ///< ラベルの font size。
	float iconSize              = 32.0f;           ///< icon の表示サイズ (pixel)。
	float animationDuration     = 0.2f;            ///< 開閉アニメーションの秒数。
	RadialMenuAnimation openAnimation = RadialMenuAnimation::Scale; ///< アニメーション種別。
	float centerDeadZone        = 30.0f;           ///< この半径内では sector を選択しない。
	float subRingInnerRadius    = 160.0f;          ///< sub-ring の内半径。
	float subRingOuterRadius    = 250.0f;          ///< sub-ring の外半径。
};

/// @brief 描画用に計算した sector ごとの geometry。
struct UIRadialSectorInfo
{
	int index                   = -1;              ///< items 配列内の項目 index。
	float startAngle            = 0.0f;            ///< 開始角 (radian)。
	float endAngle              = 0.0f;            ///< 終了角 (radian)。
	float midAngle              = 0.0f;            ///< 中心角 (radian)。
	float iconX                 = 0.0f;            ///< icon 中心 X (menu 中心からの相対)。
	float iconY                 = 0.0f;            ///< icon 中心 Y (menu 中心からの相対)。
	float labelX                = 0.0f;            ///< ラベル位置 X (menu 中心からの相対)。
	float labelY                = 0.0f;            ///< ラベル位置 Y (menu 中心からの相対)。
	bool hovered                = false;           ///< この sector が hover 中か。
	bool enabled                = true;            ///< この sector が有効か。
	bool hasSubItems            = false;           ///< この sector が入れ子 sub-item を持つか。
};

/// @brief マウス方向 or キーボードで素早く選択する円形 / 扇形 menu。
///
/// sector は円周上に均等配置される。中心からのマウス方向で hover 中の
/// sector が決まる。キーボードの数字キーで素早い選択ができる。subItems を
/// 持つ項目は hover で第 2 ring を展開する。
///
/// @code
///   UIRadialMenuConfig cfg;
///   cfg.innerRadius = 40.0f;
///   cfg.outerRadius = 160.0f;
///   cfg.items = {
///       {"Attack", "icon_sword", true, "1", {}},
///       {"Defend", "icon_shield", true, "2", {}},
///       {"Magic",  "icon_magic",  true, "3", {
///           {"Fire",  "icon_fire",  true, "1", {}},
///           {"Ice",   "icon_ice",   true, "2", {}},
///           {"Thunder","icon_thunder",true,"3", {}},
///       }},
///       {"Items",  "icon_bag",   true, "4", {}},
///   };
///   UIRadialMenu menu(cfg);
///
///   menu.setOnItemSelected([](int idx) { /* handle selection */ });
///   menu.open(400.0f, 300.0f);
///
///   // Each frame:
///   menu.update(mouseX, mouseY, dt);
///   // Render using menu.sectors(), menu.animationProgress(), etc.
/// @endcode
class UIRadialMenu
{
	std::shared_ptr<UINode> m_node;
	UIRadialMenuConfig m_config;
	bool m_open                 = false;
	float m_centerX             = 0.0f;
	float m_centerY             = 0.0f;
	int m_hoveredIndex          = -1;
	int m_hoveredSubIndex       = -1;
	bool m_subRingOpen          = false;

	// アニメーション状態。
	float m_animProgress        = 0.0f;    ///< 0 = 閉、1 = 完全に開。
	bool m_animating            = false;
	bool m_animOpening          = false;

	// 計算済み sector geometry。
	std::vector<UIRadialSectorInfo> m_sectors;
	std::vector<UIRadialSectorInfo> m_subSectors;

	// Callbacks。
	std::function<void(int)> m_onItemSelected;
	std::function<void(int, int)> m_onSubItemSelected;

public:
	/// @brief デフォルト設定で構築する。
	UIRadialMenu() { rebuildSectors(); }

	/// @brief カスタム設定で構築する。
	/// @param config menu 設定。
	explicit UIRadialMenu(const UIRadialMenuConfig& config)
		: m_config(config)
	{
		UINodeData data;
		data.id   = INVALID_UI_NODE;
		data.name = "radial_menu";
		data.role = UIRole::Custom;
		data.bounds = sgc::Rectf(
			0.0f, 0.0f,
			config.outerRadius * 2.0f,
			config.outerRadius * 2.0f);
		data.properties["widget_type"] = "radial_menu";

		m_node = std::make_shared<UINode>(std::move(data));
		rebuildSectors();
		syncNodeState();
	}

	/// @brief 基底の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の設定を取得する。
	[[nodiscard]] const UIRadialMenuConfig& config() const noexcept { return m_config; }

	/// @brief 新しい設定を反映し sector を再構築する。
	/// @param config 新しい設定。
	void setConfig(const UIRadialMenuConfig& config)
	{
		m_config = config;
		rebuildSectors();
		syncNodeState();
	}

	// ── 開閉 ───────────────────────────────────────────

	/// @brief 画面上の位置で menu を開く。
	/// @param centerX 中心 X 位置。
	/// @param centerY 中心 Y 位置。
	void open(float centerX, float centerY)
	{
		m_centerX = centerX;
		m_centerY = centerY;
		m_open = true;
		m_hoveredIndex = -1;
		m_hoveredSubIndex = -1;
		m_subRingOpen = false;
		m_animating = true;
		m_animOpening = true;
		m_animProgress = 0.0f;
		m_subSectors.clear();

		if (m_node)
		{
			m_node->setBounds(sgc::Rectf(
				centerX - m_config.outerRadius,
				centerY - m_config.outerRadius,
				m_config.outerRadius * 2.0f,
				m_config.outerRadius * 2.0f));
		}
		syncNodeState();
	}

	/// @brief menu を閉じる (閉じるアニメーションを開始)。
	void close()
	{
		if (!m_open) { return; }
		m_animating = true;
		m_animOpening = false;
		m_subRingOpen = false;
		m_subSectors.clear();
		syncNodeState();
	}

	/// @brief menu が開いているか (アニメーション中も含む)。
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief menu が現在アニメーション中か。
	[[nodiscard]] bool isAnimating() const noexcept { return m_animating; }

	/// @brief 現在のアニメーション進捗を取得する (0 = 閉、1 = 開)。
	[[nodiscard]] float animationProgress() const noexcept { return m_animProgress; }

	/// @brief menu の中心 X を取得する。
	[[nodiscard]] float centerX() const noexcept { return m_centerX; }

	/// @brief menu の中心 Y を取得する。
	[[nodiscard]] float centerY() const noexcept { return m_centerY; }

	// ── 更新 ───────────────────────────────────────────────

	/// @brief hover 状態とアニメーションを更新する。
	/// @param mouseX 現在のマウス X 位置。
	/// @param mouseY 現在のマウス Y 位置。
	/// @param dt 経過時間 (秒、アニメーション用)。
	void update(float mouseX, float mouseY, float dt)
	{
		// アニメーションを進める。
		if (m_animating)
		{
			const float speed = (m_config.animationDuration > 0.0f)
				? (1.0f / m_config.animationDuration) : 100.0f;

			if (m_animOpening)
			{
				m_animProgress = std::min(m_animProgress + speed * dt, 1.0f);
				if (m_animProgress >= 1.0f)
				{
					m_animating = false;
				}
			}
			else
			{
				m_animProgress = std::max(m_animProgress - speed * dt, 0.0f);
				if (m_animProgress <= 0.0f)
				{
					m_animating = false;
					m_open = false;
				}
			}
		}

		if (!m_open) { return; }

		// 中心からの方向を計算する。
		const float dx = mouseX - m_centerX;
		const float dy = mouseY - m_centerY;
		const float dist = std::sqrt(dx * dx + dy * dy);

		// hover 状態をリセットする。
		for (auto& sector : m_sectors) { sector.hovered = false; }
		for (auto& sector : m_subSectors) { sector.hovered = false; }

		const int prevHovered = m_hoveredIndex;

		if (dist < m_config.centerDeadZone)
		{
			// dead zone 内: 選択なし。
			m_hoveredIndex = -1;
			m_hoveredSubIndex = -1;
		}
		else if (m_subRingOpen && dist >= m_config.subRingInnerRadius && dist <= m_config.subRingOuterRadius)
		{
			// sub-ring を hover 中。
			m_hoveredSubIndex = findSectorAtAngle(m_subSectors, dx, dy);
			if (m_hoveredSubIndex >= 0 && m_hoveredSubIndex < static_cast<int>(m_subSectors.size()))
			{
				m_subSectors[static_cast<std::size_t>(m_hoveredSubIndex)].hovered = true;
			}
		}
		else if (dist >= m_config.innerRadius && dist <= m_config.outerRadius)
		{
			// main ring を hover 中。
			m_hoveredIndex = findSectorAtAngle(m_sectors, dx, dy);
			m_hoveredSubIndex = -1;

			if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_sectors.size()))
			{
				m_sectors[static_cast<std::size_t>(m_hoveredIndex)].hovered = true;

				// 項目が sub-item を持つなら sub-ring を開く。
				if (m_sectors[static_cast<std::size_t>(m_hoveredIndex)].hasSubItems
					&& m_hoveredIndex != prevHovered)
				{
					openSubRing(m_hoveredIndex);
				}
				else if (!m_sectors[static_cast<std::size_t>(m_hoveredIndex)].hasSubItems)
				{
					m_subRingOpen = false;
					m_subSectors.clear();
				}
			}
		}
		else
		{
			m_hoveredIndex = -1;
			m_hoveredSubIndex = -1;
		}

		syncNodeState();
	}

	/// @brief 現在 hover 中の項目を確定する (マウスクリック or gamepad 確定)。
	void confirm()
	{
		if (!m_open) { return; }

		// sub-ring の選択を優先する。
		if (m_hoveredSubIndex >= 0 && m_subRingOpen && m_hoveredIndex >= 0)
		{
			const auto& subItems = m_config.items[static_cast<std::size_t>(m_hoveredIndex)].subItems;
			if (m_hoveredSubIndex < static_cast<int>(subItems.size())
				&& subItems[static_cast<std::size_t>(m_hoveredSubIndex)].enabled)
			{
				if (m_onSubItemSelected)
				{
					m_onSubItemSelected(m_hoveredIndex, m_hoveredSubIndex);
				}
				close();
			}
			return;
		}

		// main ring の選択。
		if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_config.items.size()))
		{
			const auto& item = m_config.items[static_cast<std::size_t>(m_hoveredIndex)];
			if (item.enabled && item.subItems.empty())
			{
				if (m_onItemSelected)
				{
					m_onItemSelected(m_hoveredIndex);
				}
				close();
			}
		}
	}

	/// @brief キーボード shortcut で項目を選択する。
	/// @param key shortcut key 文字列 (例 "1", "2")。
	void selectByKey(const std::string& key)
	{
		if (!m_open) { return; }

		// 開いていれば先に sub-ring を調べる。
		if (m_subRingOpen && m_hoveredIndex >= 0)
		{
			const auto& subItems = m_config.items[static_cast<std::size_t>(m_hoveredIndex)].subItems;
			for (std::size_t i = 0; i < subItems.size(); ++i)
			{
				if (subItems[i].shortcutKey == key && subItems[i].enabled)
				{
					if (m_onSubItemSelected)
					{
						m_onSubItemSelected(m_hoveredIndex, static_cast<int>(i));
					}
					close();
					return;
				}
			}
		}

		// main ring を調べる。
		for (std::size_t i = 0; i < m_config.items.size(); ++i)
		{
			if (m_config.items[i].shortcutKey == key && m_config.items[i].enabled)
			{
				if (m_config.items[i].subItems.empty())
				{
					if (m_onItemSelected)
					{
						m_onItemSelected(static_cast<int>(i));
					}
					close();
				}
				else
				{
					// この項目の sub-ring を開く。
					m_hoveredIndex = static_cast<int>(i);
					openSubRing(m_hoveredIndex);
				}
				return;
			}
		}
	}

	// ── 問い合わせ ────────────────────────────────────────────────

	/// @brief 現在 hover 中の main sector index を取得する (なければ -1)。
	[[nodiscard]] int hoveredIndex() const noexcept { return m_hoveredIndex; }

	/// @brief 現在 hover 中の sub-sector index を取得する (なければ -1)。
	[[nodiscard]] int hoveredSubIndex() const noexcept { return m_hoveredSubIndex; }

	/// @brief sub-ring が開いているか。
	[[nodiscard]] bool isSubRingOpen() const noexcept { return m_subRingOpen; }

	/// @brief main ring の計算済み sector geometry を取得する (描画用)。
	[[nodiscard]] const std::vector<UIRadialSectorInfo>& sectors() const noexcept
	{
		return m_sectors;
	}

	/// @brief sub-ring の計算済み sector geometry を取得する (描画用)。
	[[nodiscard]] const std::vector<UIRadialSectorInfo>& subSectors() const noexcept
	{
		return m_subSectors;
	}

	/// @brief menu 項目を取得する。
	[[nodiscard]] const std::vector<UIRadialMenuItem>& items() const noexcept
	{
		return m_config.items;
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief main 項目が選択されたときの callback を設定する。
	/// @param callback 項目 index を受け取る。
	void setOnItemSelected(std::function<void(int)> callback)
	{
		m_onItemSelected = std::move(callback);
	}

	/// @brief sub-item が選択されたときの callback を設定する。
	/// @param callback (parentIndex, subItemIndex) を受け取る。
	void setOnSubItemSelected(std::function<void(int, int)> callback)
	{
		m_onSubItemSelected = std::move(callback);
	}

private:
	static constexpr float kPi = 3.14159265358979323846f;
	static constexpr float kTwoPi = 6.28318530717958647692f;

	/// @brief 現在の config items から sector geometry を再構築する。
	void rebuildSectors()
	{
		m_sectors.clear();
		const std::size_t count = m_config.items.size();
		if (count == 0) { return; }

		const float sectorAngle = kTwoPi / static_cast<float>(count);
		const float midRadius = (m_config.innerRadius + m_config.outerRadius) * 0.5f;

		// sector は上 (-pi/2) から時計回りに並ぶ。
		const float startOffset = -kPi * 0.5f - sectorAngle * 0.5f;

		for (std::size_t i = 0; i < count; ++i)
		{
			UIRadialSectorInfo sector;
			sector.index = static_cast<int>(i);
			sector.startAngle = startOffset + sectorAngle * static_cast<float>(i);
			sector.endAngle = sector.startAngle + sectorAngle;
			sector.midAngle = sector.startAngle + sectorAngle * 0.5f;
			sector.iconX = std::cos(sector.midAngle) * midRadius;
			sector.iconY = std::sin(sector.midAngle) * midRadius;
			sector.labelX = std::cos(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.labelY = std::sin(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.enabled = m_config.items[i].enabled;
			sector.hasSubItems = !m_config.items[i].subItems.empty();

			m_sectors.push_back(sector);
		}
	}

	/// @brief 親項目の subItems から sub-ring sector を構築する。
	/// @param parentIndex 親項目の index。
	void openSubRing(int parentIndex)
	{
		m_subSectors.clear();
		m_subRingOpen = false;

		if (parentIndex < 0 || parentIndex >= static_cast<int>(m_config.items.size()))
		{
			return;
		}

		const auto& subItems = m_config.items[static_cast<std::size_t>(parentIndex)].subItems;
		if (subItems.empty()) { return; }

		m_subRingOpen = true;
		const std::size_t count = subItems.size();
		const float sectorAngle = kTwoPi / static_cast<float>(count);
		const float midRadius = (m_config.subRingInnerRadius + m_config.subRingOuterRadius) * 0.5f;
		const float startOffset = -kPi * 0.5f - sectorAngle * 0.5f;

		for (std::size_t i = 0; i < count; ++i)
		{
			UIRadialSectorInfo sector;
			sector.index = static_cast<int>(i);
			sector.startAngle = startOffset + sectorAngle * static_cast<float>(i);
			sector.endAngle = sector.startAngle + sectorAngle;
			sector.midAngle = sector.startAngle + sectorAngle * 0.5f;
			sector.iconX = std::cos(sector.midAngle) * midRadius;
			sector.iconY = std::sin(sector.midAngle) * midRadius;
			sector.labelX = std::cos(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.labelY = std::sin(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.enabled = subItems[i].enabled;
			sector.hasSubItems = false;

			m_subSectors.push_back(sector);
		}
	}

	/// @brief 指定方向ベクトルを含む sector を探す。
	/// @param sectors 探索対象の sector リスト。
	/// @param dx 中心からの方向 X。
	/// @param dy 中心からの方向 Y。
	/// @return sector index、見つからなければ -1。
	[[nodiscard]] static int findSectorAtAngle(
		const std::vector<UIRadialSectorInfo>& sectors, float dx, float dy)
	{
		if (sectors.empty()) { return -1; }

		float angle = std::atan2(dy, dx);

		for (const auto& sector : sectors)
		{
			// sector 開始角を基準に角度を正規化する。
			float relAngle = angle - sector.startAngle;
			// [-pi, pi) に折り返す。
			while (relAngle > kPi) { relAngle -= kTwoPi; }
			while (relAngle < -kPi) { relAngle += kTwoPi; }

			const float sectorSpan = sector.endAngle - sector.startAngle;
			if (relAngle >= 0.0f && relAngle < sectorSpan)
			{
				return sector.index;
			}
		}

		return -1;
	}

	/// @brief 状態を UINode の properties に同期する。
	void syncNodeState()
	{
		if (!m_node) { return; }

		m_node->setProperty("open", m_open ? "true" : "false");
		m_node->setProperty("hovered", std::to_string(m_hoveredIndex));
		m_node->setProperty("hovered_sub", std::to_string(m_hoveredSubIndex));
		m_node->setProperty("sub_ring_open", m_subRingOpen ? "true" : "false");
		m_node->setProperty("anim_progress", std::to_string(m_animProgress));
		m_node->setProperty("center_x", std::to_string(m_centerX));
		m_node->setProperty("center_y", std::to_string(m_centerY));
		m_node->setProperty("item_count", std::to_string(m_config.items.size()));
	}
};

} // namespace mitiru::ui
