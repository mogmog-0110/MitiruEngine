#pragma once

/// @file FocusManager.hpp
/// @brief UIフォーカス管理システム
///
/// キーボード・ゲームパッドによるUIナビゲーションを管理する。
/// 空間ナビゲーション（方向キー）とタブ順ナビゲーションの両方をサポートする。
///
/// @code
/// sgc::ui::FocusManager fm;
/// fm.registerWidget({1, {10, 10, 100, 40}});
/// fm.registerWidget({2, {10, 60, 100, 40}});
/// fm.setFocus(1);
///
/// sgc::ui::FocusInput input{};
/// input.down = true;
/// auto result = fm.update(input);
/// // result.focusedId == 2
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include "sgc/ui/FocusableWidget.hpp"

namespace sgc::ui
{

/// @brief フォーカス移動方向
enum class FocusDirection
{
	Up,    ///< 上方向
	Down,  ///< 下方向
	Left,  ///< 左方向
	Right  ///< 右方向
};

/// @brief フォーカス入力情報
///
/// 1フレーム分の入力状態を表す。
/// 方向キー、確定、キャンセル、タブのトリガー状態を保持する。
struct FocusInput
{
	bool up{};       ///< 上方向入力
	bool down{};     ///< 下方向入力
	bool left{};     ///< 左方向入力
	bool right{};    ///< 右方向入力
	bool confirm{};  ///< 確定入力（Enter / Aボタン）
	bool cancel{};   ///< キャンセル入力（Escape / Bボタン）
	bool tabNext{};  ///< 次へ（Tab）
	bool tabPrev{};  ///< 前へ（Shift+Tab）
};

/// @brief フォーカス更新結果
///
/// update()の戻り値として、現在のフォーカス状態とアクションを返す。
struct FocusResult
{
	std::optional<FocusId> focusedId;    ///< 現在フォーカス中のウィジェットID
	std::optional<FocusId> activatedId;  ///< このフレームでアクティベートされたウィジェットID
	bool cancelPressed{};                ///< キャンセルが押されたか
};

/// @brief UIフォーカスマネージャ
///
/// ウィジェットの登録・解除とフォーカス移動を管理する。
/// 空間ナビゲーション: 方向キーで最も近いウィジェットにフォーカスを移動する。
/// タブナビゲーション: 登録順にウィジェットを巡回する。
class FocusManager
{
public:
	/// @brief ウィジェットを登録する
	/// @param widget 登録するウィジェット情報
	void registerWidget(FocusableWidget widget)
	{
		m_widgets.push_back(std::move(widget));
	}

	/// @brief ウィジェットを登録解除する
	/// @param id 解除するウィジェットのID
	void unregisterWidget(FocusId id)
	{
		m_widgets.erase(
			std::remove_if(m_widgets.begin(), m_widgets.end(),
				[id](const FocusableWidget& w) { return w.id == id; }),
			m_widgets.end());

		if (m_currentFocus == id)
		{
			m_currentFocus.reset();
		}
	}

	/// @brief 全ウィジェットを削除する
	void clear()
	{
		m_widgets.clear();
		m_currentFocus.reset();
	}

	/// @brief フォーカス状態を更新する
	/// @param input 1フレーム分の入力情報
	/// @return フォーカス更新結果
	[[nodiscard]] FocusResult update(const FocusInput& input)
	{
		FocusResult result;
		result.cancelPressed = input.cancel;

		// フォーカスが未設定の場合、方向入力またはタブ入力で最初の有効ウィジェットにフォーカス
		if (!m_currentFocus.has_value())
		{
			if (input.up || input.down || input.left || input.right ||
				input.tabNext || input.tabPrev)
			{
				m_currentFocus = findFirstEnabled();
			}
			result.focusedId = m_currentFocus;
			return result;
		}

		// 方向ナビゲーション
		if (input.up)
		{
			navigateTo(FocusDirection::Up);
		}
		else if (input.down)
		{
			navigateTo(FocusDirection::Down);
		}
		else if (input.left)
		{
			navigateTo(FocusDirection::Left);
		}
		else if (input.right)
		{
			navigateTo(FocusDirection::Right);
		}

		// タブナビゲーション
		if (input.tabNext)
		{
			auto next = findNext(*m_currentFocus);
			if (next.has_value())
			{
				m_currentFocus = next;
			}
		}
		else if (input.tabPrev)
		{
			auto prev = findPrev(*m_currentFocus);
			if (prev.has_value())
			{
				m_currentFocus = prev;
			}
		}

		// 確定アクション
		if (input.confirm && m_currentFocus.has_value())
		{
			result.activatedId = m_currentFocus;
		}

		result.focusedId = m_currentFocus;
		return result;
	}

	/// @brief 指定ウィジェットにフォーカスを設定する
	/// @param id フォーカス先のウィジェットID
	void setFocus(FocusId id)
	{
		m_currentFocus = id;
	}

	/// @brief フォーカスをクリアする
	void clearFocus()
	{
		m_currentFocus.reset();
	}

	/// @brief 現在フォーカス中のウィジェットIDを返す
	/// @return フォーカス中のID（なければnullopt）
	[[nodiscard]] std::optional<FocusId> currentFocus() const noexcept
	{
		return m_currentFocus;
	}

	/// @brief 指定ウィジェットがフォーカス中か判定する
	/// @param id 判定するウィジェットID
	/// @return フォーカス中ならtrue
	[[nodiscard]] bool isFocused(FocusId id) const noexcept
	{
		return m_currentFocus.has_value() && *m_currentFocus == id;
	}

	/// @brief 登録済みウィジェット数を返す
	/// @return ウィジェット数
	[[nodiscard]] std::size_t widgetCount() const noexcept
	{
		return m_widgets.size();
	}

private:
	/// @brief 方向ナビゲーションを実行する
	/// @param dir 移動方向
	void navigateTo(FocusDirection dir)
	{
		if (!m_currentFocus.has_value())
		{
			return;
		}

		// 明示的ナビゲーション先をチェック
		const auto* current = findWidget(*m_currentFocus);
		if (current != nullptr)
		{
			std::optional<FocusId> explicitTarget;
			switch (dir)
			{
			case FocusDirection::Up:    explicitTarget = current->navUp;    break;
			case FocusDirection::Down:  explicitTarget = current->navDown;  break;
			case FocusDirection::Left:  explicitTarget = current->navLeft;  break;
			case FocusDirection::Right: explicitTarget = current->navRight; break;
			}

			if (explicitTarget.has_value())
			{
				const auto* target = findWidget(*explicitTarget);
				if (target != nullptr && target->enabled)
				{
					m_currentFocus = *explicitTarget;
					return;
				}
			}
		}

		// 空間ナビゲーション
		auto nearest = findNearest(*m_currentFocus, dir);
		if (nearest.has_value())
		{
			m_currentFocus = nearest;
		}
	}

	/// @brief IDでウィジェットを検索する
	/// @param id 検索するID
	/// @return ウィジェットへのポインタ（見つからなければnullptr）
	[[nodiscard]] const FocusableWidget* findWidget(FocusId id) const
	{
		for (const auto& w : m_widgets)
		{
			if (w.id == id)
			{
				return &w;
			}
		}
		return nullptr;
	}

	/// @brief 最初の有効なウィジェットIDを返す
	/// @return 有効なウィジェットID（なければnullopt）
	[[nodiscard]] std::optional<FocusId> findFirstEnabled() const
	{
		for (const auto& w : m_widgets)
		{
			if (w.enabled)
			{
				return w.id;
			}
		}
		return std::nullopt;
	}

	/// @brief 空間ナビゲーション: 指定方向で最も近いウィジェットを探す
	///
	/// 現在のウィジェット中心から指定方向の半平面にある有効ウィジェットを探し、
	/// 主軸距離（小さいほど優先）+ 副軸距離（タイブレーカー）で評価する。
	///
	/// @param from 基点ウィジェットID
	/// @param dir 探索方向
	/// @return 最も近いウィジェットID（なければnullopt）
	[[nodiscard]] std::optional<FocusId> findNearest(FocusId from, FocusDirection dir) const
	{
		const auto* current = findWidget(from);
		if (current == nullptr)
		{
			return std::nullopt;
		}

		const auto center = current->bounds.center();
		float bestScore = std::numeric_limits<float>::max();
		std::optional<FocusId> bestId;

		for (const auto& w : m_widgets)
		{
			if (w.id == from || !w.enabled)
			{
				continue;
			}

			const auto candidateCenter = w.bounds.center();
			const float dx = candidateCenter.x - center.x;
			const float dy = candidateCenter.y - center.y;

			// 半平面フィルタ
			bool inHalfPlane = false;
			float primary = 0.0f;
			float secondary = 0.0f;

			switch (dir)
			{
			case FocusDirection::Up:
				inHalfPlane = (dy < 0.0f);
				primary = -dy;
				secondary = std::abs(dx);
				break;
			case FocusDirection::Down:
				inHalfPlane = (dy > 0.0f);
				primary = dy;
				secondary = std::abs(dx);
				break;
			case FocusDirection::Left:
				inHalfPlane = (dx < 0.0f);
				primary = -dx;
				secondary = std::abs(dy);
				break;
			case FocusDirection::Right:
				inHalfPlane = (dx > 0.0f);
				primary = dx;
				secondary = std::abs(dy);
				break;
			}

			if (!inHalfPlane)
			{
				continue;
			}

			// スコア: 主軸距離を優先し、副軸距離をタイブレーカーとする
			const float score = primary * 1000.0f + secondary;
			if (score < bestScore)
			{
				bestScore = score;
				bestId = w.id;
			}
		}

		return bestId;
	}

	/// @brief タブ順で次の有効ウィジェットを探す
	/// @param from 基点ウィジェットID
	/// @return 次のウィジェットID（なければnullopt）
	[[nodiscard]] std::optional<FocusId> findNext(FocusId from) const
	{
		if (m_widgets.empty())
		{
			return std::nullopt;
		}

		// 現在位置を探す
		std::size_t currentIdx = 0;
		for (std::size_t i = 0; i < m_widgets.size(); ++i)
		{
			if (m_widgets[i].id == from)
			{
				currentIdx = i;
				break;
			}
		}

		// 次の有効ウィジェットを循環探索
		for (std::size_t i = 1; i <= m_widgets.size(); ++i)
		{
			const std::size_t idx = (currentIdx + i) % m_widgets.size();
			if (m_widgets[idx].enabled)
			{
				return m_widgets[idx].id;
			}
		}

		return std::nullopt;
	}

	/// @brief タブ順で前の有効ウィジェットを探す
	/// @param from 基点ウィジェットID
	/// @return 前のウィジェットID（なければnullopt）
	[[nodiscard]] std::optional<FocusId> findPrev(FocusId from) const
	{
		if (m_widgets.empty())
		{
			return std::nullopt;
		}

		// 現在位置を探す
		std::size_t currentIdx = 0;
		for (std::size_t i = 0; i < m_widgets.size(); ++i)
		{
			if (m_widgets[i].id == from)
			{
				currentIdx = i;
				break;
			}
		}

		// 前の有効ウィジェットを循環探索
		for (std::size_t i = 1; i <= m_widgets.size(); ++i)
		{
			const std::size_t idx = (currentIdx + m_widgets.size() - i) % m_widgets.size();
			if (m_widgets[idx].enabled)
			{
				return m_widgets[idx].id;
			}
		}

		return std::nullopt;
	}

	std::vector<FocusableWidget> m_widgets;  ///< 登録済みウィジェット一覧
	std::optional<FocusId> m_currentFocus;   ///< 現在フォーカス中のウィジェットID
};

} // namespace sgc::ui
