#pragma once

/// @file TransitionBridge.hpp
/// @brief sgcシーン遷移統合ブリッジ
/// @details sgcのTransitionManager + FadeTransitionを
///          Mitiruエンジンに統合する。フェードイン/アウトの制御を提供。

#include <memory>
#include <string>

#include <sgc/scene/Transition.hpp>

namespace mitiru::bridge
{

/// @brief sgcシーン遷移統合ブリッジ
/// @details TransitionManagerをラップし、フェードイン/アウトの簡易APIを提供する。
///
/// @code
/// mitiru::bridge::TransitionBridge transition;
/// transition.startFadeOut(0.5f);
///
/// // 毎フレーム
/// transition.update(dt);
/// float alpha = transition.alpha();
/// if (transition.isComplete()) { /* 遷移完了 */ }
/// @endcode
class TransitionBridge
{
public:
	/// @brief フェードアウトを開始する
	/// @param duration フェード時間（秒）。Out+Inの合計は duration*2
	void startFadeOut(float duration)
	{
		m_manager.start(std::make_unique<sgc::FadeTransition>(duration));
	}

	/// @brief フェードインを開始する（内部的にはOut→Inの遷移）
	/// @param duration フェード時間（秒）
	/// @details FadeTransitionはOut→Inの2フェーズで動作するため、
	///          フェードインはTransitionManager全体のOut→Inシーケンスとなる。
	void startFadeIn(float duration)
	{
		m_manager.start(std::make_unique<sgc::FadeTransition>(duration));
	}

	/// @brief 遷移を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		m_manager.update(dt);
	}

	/// @brief 現在のアルファ値を取得する
	/// @return アルファ値（0.0=透明、1.0=不透明）
	[[nodiscard]] float alpha() const noexcept
	{
		return m_manager.getAlpha();
	}

	/// @brief 遷移がアクティブか
	/// @return アクティブ（OutまたはIn中）ならtrue
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_manager.isActive();
	}

	/// @brief 遷移が完了したか
	/// @return 完了済みならtrue
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_manager.isComplete();
	}

	/// @brief 現在の遷移フェーズを取得する
	/// @return TransitionPhase
	[[nodiscard]] sgc::TransitionPhase phase() const noexcept
	{
		return m_manager.phase();
	}

	/// @brief 遷移をリセットする
	void reset() noexcept
	{
		m_manager.reset();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief 遷移状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"active\":" + std::string(isActive() ? "true" : "false");
		json += ",\"complete\":" + std::string(isComplete() ? "true" : "false");
		json += ",\"alpha\":" + std::to_string(alpha());
		json += ",\"phase\":\"" + phaseToString(m_manager.phase()) + "\"";
		json += "}";
		return json;
	}

private:
	/// @brief フェーズを文字列に変換する
	[[nodiscard]] static std::string phaseToString(sgc::TransitionPhase phase)
	{
		switch (phase)
		{
		case sgc::TransitionPhase::Idle:     return "Idle";
		case sgc::TransitionPhase::Out:      return "Out";
		case sgc::TransitionPhase::In:       return "In";
		case sgc::TransitionPhase::Complete: return "Complete";
		}
		return "Unknown";
	}

	sgc::TransitionManager m_manager;  ///< 遷移マネージャー
};

} // namespace mitiru::bridge
