#pragma once

/// @file AnimationBridge.hpp
/// @brief sgcアニメーション統合ブリッジ
/// @details sgcのTween、TweenTimeline、AnimationStateMachineを
///          Mitiruエンジンに統合する。名前付きトゥイーンの管理と更新を提供。

#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/animation/Tween.hpp>
#include <sgc/animation/TweenTimeline.hpp>

namespace mitiru::bridge
{

/// @brief sgcアニメーション統合ブリッジ
/// @details 名前付きトゥイーンの登録・更新・値取得を一元管理する。
///
/// @code
/// mitiru::bridge::AnimationBridge anim;
///
/// sgc::Tweenf tween;
/// tween.from(0.0f).to(100.0f).during(1.0f);
/// anim.addTween("fadeIn", std::move(tween));
///
/// // 毎フレーム
/// anim.update(dt);
/// float alpha = anim.getTweenValue("fadeIn");
/// @endcode
class AnimationBridge
{
public:
	// ── トゥイーン管理 ──────────────────────────────────────

	/// @brief トゥイーンを追加する
	/// @param name トゥイーン名
	/// @param tween sgc::Tweenf
	void addTween(const std::string& name, sgc::Tween<float> tween)
	{
		m_tweens[name] = TweenEntry{std::move(tween), 0.0f};
	}

	/// @brief 全トゥイーンを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& [name, entry] : m_tweens)
		{
			if (!entry.tween.isFinished())
			{
				entry.lastValue = entry.tween.step(dt);
			}
		}
	}

	/// @brief トゥイーンの現在値を取得する
	/// @param name トゥイーン名
	/// @return 現在値（未登録時は0.0f）
	[[nodiscard]] float getTweenValue(const std::string& name) const
	{
		const auto it = m_tweens.find(name);
		if (it == m_tweens.end())
		{
			return 0.0f;
		}
		return it->second.lastValue;
	}

	/// @brief トゥイーンが完了しているか
	/// @param name トゥイーン名
	/// @return 完了済みならtrue（未登録時もtrue）
	[[nodiscard]] bool isTweenComplete(const std::string& name) const
	{
		const auto it = m_tweens.find(name);
		if (it == m_tweens.end())
		{
			return true;
		}
		return it->second.tween.isFinished();
	}

	/// @brief トゥイーンを削除する
	/// @param name トゥイーン名
	void removeTween(const std::string& name)
	{
		m_tweens.erase(name);
	}

	/// @brief 登録トゥイーン数を取得する
	/// @return トゥイーン数
	[[nodiscard]] std::size_t tweenCount() const noexcept
	{
		return m_tweens.size();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief アニメーション状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"tweenCount\":" + std::to_string(m_tweens.size()) + ",";
		json += "\"tweens\":[";

		bool first = true;
		for (const auto& [name, entry] : m_tweens)
		{
			if (!first) json += ",";
			json += "{\"name\":\"" + name + "\"";
			json += ",\"value\":" + std::to_string(entry.lastValue);
			json += ",\"finished\":" + std::string(entry.tween.isFinished() ? "true" : "false");
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief トゥイーン内部エントリ
	struct TweenEntry
	{
		sgc::Tween<float> tween;   ///< トゥイーン本体
		float lastValue{0.0f};     ///< 最新の値
	};

	/// @brief 名前 → トゥイーンエントリ
	std::unordered_map<std::string, TweenEntry> m_tweens;
};

} // namespace mitiru::bridge
