#pragma once

/// @file AnimationPlayer.hpp
/// @brief Godot風プロパティアニメーションプレイヤー
/// @details 任意のfloatプロパティをキーフレームでアニメーションする。
///          複数トラック（位置X、位置Y、透明度等）を1つのアニメーションに束ねて再生可能。
///
/// @code
/// mitiru::animation::AnimationPlayer player;
/// mitiru::animation::Animation anim;
/// anim.name = "fade_in";
/// anim.duration = 1.0f;
/// anim.tracks.push_back({
///     "alpha", {{0, 0}, {1, 1}},
///     [&](float v) { alpha = v; }
/// });
/// player.addAnimation(anim);
/// player.play("fade_in");
/// // 毎フレーム: player.update(dt);
/// @endcode

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::animation
{

/// @brief キーフレーム
struct Keyframe
{
	float time;  ///< 時刻（秒）
	float value; ///< 値
};

/// @brief アニメーショントラック（1つのプロパティ用）
/// @details キーフレーム列と値セッターを持ち、指定時刻の補間値を計算する。
struct AnimTrack
{
	std::string property;              ///< プロパティ名
	std::vector<Keyframe> keys;        ///< キーフレーム列（時刻昇順）
	std::function<void(float)> setter; ///< 値セッター

	/// @brief 指定時刻での補間値を取得する
	/// @param t 時刻（秒）
	/// @return 補間された値
	[[nodiscard]] float evaluate(float t) const
	{
		if (keys.empty()) return 0;
		if (t <= keys.front().time) return keys.front().value;
		if (t >= keys.back().time) return keys.back().value;

		for (std::size_t i = 0; i + 1 < keys.size(); ++i)
		{
			if (t >= keys[i].time && t < keys[i + 1].time)
			{
				const float ratio = (t - keys[i].time) /
					(keys[i + 1].time - keys[i].time);
				return keys[i].value + (keys[i + 1].value - keys[i].value) * ratio;
			}
		}
		return keys.back().value;
	}
};

/// @brief アニメーション定義
/// @details 名前・長さ・ループ設定と複数のトラックを保持する。
struct Animation
{
	std::string name;                ///< アニメーション名
	float duration = 1.0f;           ///< 長さ（秒）
	bool loop = false;               ///< ループ再生するか
	std::vector<AnimTrack> tracks;   ///< トラック列
};

/// @brief アニメーションプレイヤー（Godot AnimationPlayer風）
/// @details 複数のアニメーションを登録し、名前で再生する。
///          update()を毎フレーム呼ぶと、現在のアニメーションの
///          各トラックが自動的に値を更新する。
class AnimationPlayer
{
public:
	/// @brief アニメーションを登録する
	/// @param anim アニメーション定義
	void addAnimation(const Animation& anim)
	{
		m_animations[anim.name] = anim;
	}

	/// @brief アニメーションを名前で再生する
	/// @param name アニメーション名
	void play(const std::string& name)
	{
		auto it = m_animations.find(name);
		if (it == m_animations.end()) return;
		m_current = &it->second;
		m_time = 0;
		m_playing = true;
	}

	/// @brief 再生を停止する
	void stop()
	{
		m_playing = false;
		m_current = nullptr;
	}

	/// @brief 毎フレームの更新
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (!m_playing || !m_current) return;
		m_time += dt * m_speed;

		if (m_time >= m_current->duration)
		{
			if (m_current->loop)
			{
				m_time -= m_current->duration;
			}
			else
			{
				m_time = m_current->duration;
				m_playing = false;
			}
		}

		// 全トラックに値を適用する
		for (auto& track : m_current->tracks)
		{
			const float val = track.evaluate(m_time);
			if (track.setter) track.setter(val);
		}
	}

	/// @brief 再生中か
	[[nodiscard]] bool isPlaying() const noexcept { return m_playing; }

	/// @brief 現在の再生時刻を取得する
	[[nodiscard]] float currentTime() const noexcept { return m_time; }

	/// @brief 再生速度を設定する
	/// @param s 速度倍率（1.0が通常速度）
	void setSpeed(float s) noexcept { m_speed = s; }

	/// @brief 再生速度を取得する
	[[nodiscard]] float speed() const noexcept { return m_speed; }

	/// @brief 現在再生中のアニメーション名を取得する
	/// @return アニメーション名（未再生時は空文字列）
	[[nodiscard]] std::string currentAnimation() const
	{
		return m_current ? m_current->name : std::string{};
	}

	/// @brief 登録済みアニメーション数を取得する
	[[nodiscard]] int animationCount() const noexcept
	{
		return static_cast<int>(m_animations.size());
	}

private:
	std::unordered_map<std::string, Animation> m_animations; ///< アニメーション辞書
	Animation* m_current = nullptr;                           ///< 現在再生中のアニメーション
	float m_time = 0;                                         ///< 現在時刻
	float m_speed = 1.0f;                                     ///< 再生速度
	bool m_playing = false;                                   ///< 再生中フラグ
};

} // namespace mitiru::animation
