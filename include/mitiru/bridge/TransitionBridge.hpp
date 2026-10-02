#pragma once

/// @file TransitionBridge.hpp
/// @brief sgc シーン遷移統合ブリッジ
/// @details sgc の TransitionManager + FadeTransition を
///          Mitiru エンジンに統合する。フェードイン/アウトの制御を提供。

#include <memory>
#include <string>

#include <sgc/scene/Transition.hpp>
#include <mitiru/bridge/BridgeViewPush.hpp>

namespace mitiru::bridge
{

/// @brief sgc シーン遷移統合ブリッジ
/// @details TransitionManager をラップし、フェードイン/アウトの簡易 API を提供する。
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
	/// @brief BridgeViewPush を紐付ける（非所有ポインタ）
	/// @details 設定後、startFadeOut / startFadeIn / update / reset のたびに
	///          alpha / phase / active の 3 キーが view push される。
	///          nullptr を渡すと push が無効になる。
	///
	/// @param viewPush 呼び出し元が所有する BridgeViewPush。
	///                 本オブジェクトより長く生存しなければならない。
	///
	/// @code
	/// BridgeViewPush vp("transition", setSink, emitSink);
	/// transition.setViewPush(&vp);
	/// @endcode
	void setViewPush(BridgeViewPush* viewPush) noexcept
	{
		m_viewPush = viewPush;
	}

	/// @brief フェードアウトを開始する
	/// @param duration フェード時間（秒）。Out+In の合計は duration*2
	void startFadeOut(float duration)
	{
		m_manager.start(std::make_unique<sgc::FadeTransition>(duration));
		pushState();
	}

	/// @brief フェードインを開始する（内部的には Out→In の遷移）
	/// @param duration フェード時間（秒）
	/// @details FadeTransition は Out→In の 2 フェーズで動作するため、
	///          フェードインは TransitionManager 全体の Out→In シーケンスとなる。
	void startFadeIn(float duration)
	{
		m_manager.start(std::make_unique<sgc::FadeTransition>(duration));
		pushState();
	}

	/// @brief 遷移を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		m_manager.update(dt);
		pushState();
	}

	/// @brief 現在のアルファ値を取得する
	/// @return アルファ値（0.0=透明、1.0=不透明）
	[[nodiscard]] float alpha() const noexcept
	{
		return m_manager.getAlpha();
	}

	/// @brief 遷移がアクティブか
	/// @return アクティブ（Out または In 中）なら true
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_manager.isActive();
	}

	/// @brief 遷移が完了したか
	/// @return 完了済みなら true
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
		pushState();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief 遷移状態を JSON 文字列として返す
	/// @return JSON 形式の文字列
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
	[[nodiscard]] static std::string phaseToString(sgc::TransitionPhase p)
	{
		switch (p)
		{
		case sgc::TransitionPhase::Idle:     return "Idle";
		case sgc::TransitionPhase::Out:      return "Out";
		case sgc::TransitionPhase::In:       return "In";
		case sgc::TransitionPhase::Complete: return "Complete";
		}
		return "Unknown";
	}

	/// @brief alpha / phase / active の 3 キーを view push する。
	/// @details m_viewPush が nullptr のときは何もしない。
	void pushState()
	{
		if (m_viewPush == nullptr) { return; }

		m_viewPush->set("alpha",  std::to_string(alpha()));
		m_viewPush->set("phase",  "\"" + phaseToString(m_manager.phase()) + "\"");
		m_viewPush->set("active", isActive() ? "true" : "false");
	}

	sgc::TransitionManager m_manager;             ///< 遷移マネージャー
	BridgeViewPush*        m_viewPush = nullptr;  ///< 非所有。nullptr なら push 無効
};

} // namespace mitiru::bridge
