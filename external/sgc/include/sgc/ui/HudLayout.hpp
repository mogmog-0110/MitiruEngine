#pragma once

/// @file HudLayout.hpp
/// @brief HUDレイアウトマネージャ
///
/// ハッシュIDキーでHUD要素を登録し、画面サイズに基づいて位置を一括計算する。
/// シーンのdraw()ではcomputedPositionを取得して描画するだけでよい。
///
/// @code
/// using namespace sgc::literals;
/// sgc::ui::HudLayout hud;
/// hud.add("score"_hash,  {sgc::ui::Anchor::TopLeft,     {10, 10}});
/// hud.add("title"_hash,  {sgc::ui::Anchor::TopCenter,   {0, 200}, {300, 60}});
/// hud.add("prompt"_hash, {sgc::ui::Anchor::Center,       {0, 50}});
/// hud.recalculate(sgc::ui::screenRect(800, 600));
///
/// auto pos = hud.position("score"_hash);
/// @endcode

#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief HUD要素の配置記述
struct HudElement
{
	Anchor anchor{Anchor::TopLeft};    ///< アンカーポイント
	Vec2f offset{};                     ///< アンカーからのオフセット
	Vec2f size{};                       ///< 要素サイズ（0なら位置のみ）
	bool visible{true};                 ///< 可視フラグ

	// ── 計算済みフィールド（recalculate後に有効）────
	Vec2f computedPosition{};           ///< 計算された描画位置
	Rectf computedBounds{};             ///< 計算された境界矩形
};

/// @brief HUDレイアウトマネージャ
///
/// ハッシュIDキーでHUD要素を登録し、画面サイズに基づいて位置を一括計算する。
/// シーンのdraw()ではcomputedPositionを取得して描画するだけでよい。
class HudLayout
{
public:
	/// @brief HUD要素を追加する
	/// @param key 要素キー（_hashリテラルで生成）
	/// @param elem 要素の配置定義
	void add(std::uint64_t key, HudElement elem)
	{
		m_elements.insert_or_assign(key, std::move(elem));
	}

	/// @brief 画面サイズに基づいて全要素の位置を再計算する
	/// @param screen 画面領域
	void recalculate(const Rectf& screen)
	{
		for (auto& [key, elem] : m_elements)
		{
			recalculateElement(screen, elem);
		}
	}

	/// @brief 計算済み描画位置を取得する
	/// @param key 要素キー
	/// @return 計算された描画位置
	/// @throw std::out_of_range 要素が存在しない場合
	[[nodiscard]] Vec2f position(std::uint64_t key) const
	{
		return findElement(key).computedPosition;
	}

	/// @brief 計算済み境界矩形を取得する
	/// @param key 要素キー
	/// @return 計算された境界矩形
	/// @throw std::out_of_range 要素が存在しない場合
	[[nodiscard]] Rectf bounds(std::uint64_t key) const
	{
		return findElement(key).computedBounds;
	}

	/// @brief 要素の参照を取得する（可視性変更等）
	/// @param key 要素キー
	/// @return 要素への参照
	/// @throw std::out_of_range 要素が存在しない場合
	[[nodiscard]] HudElement& element(std::uint64_t key)
	{
		return findElementMut(key);
	}

	/// @brief 要素のconst参照を取得する
	/// @param key 要素キー
	/// @return 要素へのconst参照
	/// @throw std::out_of_range 要素が存在しない場合
	[[nodiscard]] const HudElement& element(std::uint64_t key) const
	{
		return findElement(key);
	}

	/// @brief 要素の可視性を設定する
	/// @param key 要素キー
	/// @param visible 可視フラグ
	/// @throw std::out_of_range 要素が存在しない場合
	void setVisible(std::uint64_t key, bool visible)
	{
		findElementMut(key).visible = visible;
	}

	/// @brief 要素が存在するか
	/// @param key 要素キー
	/// @return 存在すればtrue
	[[nodiscard]] bool has(std::uint64_t key) const
	{
		return m_elements.find(key) != m_elements.end();
	}

	/// @brief 全要素を削除する
	void clear()
	{
		m_elements.clear();
	}

	/// @brief 要素数
	[[nodiscard]] std::size_t count() const noexcept
	{
		return m_elements.size();
	}

private:
	std::unordered_map<std::uint64_t, HudElement> m_elements;  ///< 登録済み要素

	/// @brief 要素の位置を再計算する
	/// @param screen 画面領域
	/// @param elem 計算対象の要素
	static void recalculateElement(const Rectf& screen, HudElement& elem)
	{
		if (elem.size.x > 0.0f || elem.size.y > 0.0f)
		{
			// サイズ指定あり: alignedRectで配置
			const Rectf aligned = alignedRect(screen, elem.size, elem.anchor);
			elem.computedBounds = Rectf{
				{aligned.x() + elem.offset.x, aligned.y() + elem.offset.y},
				elem.size
			};
			elem.computedPosition = elem.computedBounds.position;
		}
		else
		{
			// サイズ指定なし: 位置のみ計算
			elem.computedPosition = anchoredPosition(screen, elem.anchor, elem.offset);
			elem.computedBounds = Rectf{elem.computedPosition, {0.0f, 0.0f}};
		}
	}

	/// @brief 要素を検索する（const版）
	[[nodiscard]] const HudElement& findElement(std::uint64_t key) const
	{
		const auto it = m_elements.find(key);
		if (it == m_elements.end())
		{
			throw std::out_of_range("HudLayout: element not found");
		}
		return it->second;
	}

	/// @brief 要素を検索する（非const版）
	[[nodiscard]] HudElement& findElementMut(std::uint64_t key)
	{
		auto it = m_elements.find(key);
		if (it == m_elements.end())
		{
			throw std::out_of_range("HudLayout: element not found");
		}
		return it->second;
	}
};

} // namespace sgc::ui
