#pragma once

/// @file Tweener.hpp
/// @brief 複数同時Tween管理マネージャー
/// @details add()でTweenを登録し、update(dt)で一括更新する。
///          コールバックでアニメーション中の値を受け取る。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include <mitiru/util/EasingHelper.hpp>

namespace mitiru::util
{

/// @brief Tween ID型
using TweenId = std::uint32_t;

/// @brief Tweenコールバック型（現在値を受け取る）
using TweenCallback = std::function<void(float)>;

/// @brief イージング関数型
using EaseFunc = std::function<float(float)>;

/// @brief 個別Tweenエントリ
struct TweenEntry
{
	TweenId id = 0;          ///< 一意ID
	float from = 0.0f;       ///< 開始値
	float to = 1.0f;         ///< 終了値
	float duration = 1.0f;   ///< 所要時間（秒）
	float elapsed = 0.0f;    ///< 経過時間
	TweenCallback callback;  ///< 毎フレームコールバック
	EaseFunc ease;            ///< イージング関数
	bool finished = false;   ///< 完了フラグ
};

/// @brief 複数同時Tween管理マネージャー
class Tweener
{
public:
	/// @brief Tweenを追加する
	/// @param from 開始値
	/// @param to 終了値
	/// @param duration 所要時間（秒）
	/// @param callback 毎フレーム値通知コールバック
	/// @param ease イージング関数（デフォルト: 線形）
	/// @return 追加されたTweenのID
	TweenId add(float from, float to, float duration,
	            TweenCallback callback,
	            EaseFunc ease = Easing::linear)
	{
		const TweenId id = m_nextId++;
		TweenEntry entry;
		entry.id = id;
		entry.from = from;
		entry.to = to;
		entry.duration = std::max(0.001f, duration);
		entry.callback = std::move(callback);
		entry.ease = std::move(ease);
		m_tweens.push_back(std::move(entry));
		return id;
	}

	/// @brief 全Tweenを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& tw : m_tweens)
		{
			if (tw.finished) continue;
			tw.elapsed += dt;
			float t = std::clamp(tw.elapsed / tw.duration, 0.0f, 1.0f);
			if (tw.ease)
			{
				t = tw.ease(t);
			}
			const float value = tw.from + (tw.to - tw.from) * t;
			if (tw.callback)
			{
				tw.callback(value);
			}
			if (tw.elapsed >= tw.duration)
			{
				tw.finished = true;
			}
		}

		/// 完了したTweenを除去する
		m_tweens.erase(
			std::remove_if(m_tweens.begin(), m_tweens.end(),
				[](const TweenEntry& e) { return e.finished; }),
			m_tweens.end());
	}

	/// @brief 指定IDのTweenをキャンセルする
	/// @param id キャンセルするTweenのID
	/// @return キャンセルに成功したらtrue
	bool cancel(TweenId id)
	{
		const auto it = std::find_if(m_tweens.begin(), m_tweens.end(),
			[id](const TweenEntry& e) { return e.id == id; });
		if (it != m_tweens.end())
		{
			m_tweens.erase(it);
			return true;
		}
		return false;
	}

	/// @brief 全Tweenをクリアする
	void clear()
	{
		m_tweens.clear();
	}

	/// @brief アクティブなTween数を返す
	[[nodiscard]] int activeCount() const noexcept
	{
		return static_cast<int>(m_tweens.size());
	}

	/// @brief アクティブなTweenが存在するか
	[[nodiscard]] bool hasActive() const noexcept
	{
		return !m_tweens.empty();
	}

private:
	std::vector<TweenEntry> m_tweens;   ///< アクティブなTween一覧
	TweenId m_nextId = 1;               ///< 次のID
};

} // namespace mitiru::util
