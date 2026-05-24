#pragma once

/// @file Transition.hpp
/// @brief シーン遷移エフェクトインターフェースと組み込み実装
///
/// フレームワーク非依存のシーン遷移エフェクトを定義する。
/// ITransition を実装して独自の遷移演出を追加できる。
///
/// @code
/// // フェード遷移（デフォルト）
/// sgc::FadeTransition fade{0.5f};
///
/// // スライド遷移
/// sgc::SlideTransition slide{0.4f, sgc::SlideDirection::Left};
///
/// // ワイプ遷移
/// sgc::WipeTransition wipe{0.6f, sgc::WipeDirection::Horizontal};
///
/// // TransitionManagerで管理
/// sgc::TransitionManager tm;
/// tm.start(std::make_unique<sgc::FadeTransition>(0.5f));
/// while (!tm.isComplete()) {
///     tm.update(dt);
///     float alpha = tm.getProgress();
/// }
/// @endcode

#include <algorithm>
#include <cstdint>
#include <memory>

namespace sgc
{

/// @brief 遷移フェーズ
enum class TransitionPhase : int32_t
{
	Idle,     ///< 遷移なし
	Out,      ///< 退場中（旧シーンが消えていく）
	In,       ///< 入場中（新シーンが現れる）
	Complete  ///< 遷移完了
};

/// @brief スライド方向
enum class SlideDirection : int32_t
{
	Left,   ///< 左方向へスライド
	Right,  ///< 右方向へスライド
	Up,     ///< 上方向へスライド
	Down    ///< 下方向へスライド
};

/// @brief ワイプ方向
enum class WipeDirection : int32_t
{
	Horizontal,  ///< 水平ワイプ（左→右）
	Vertical     ///< 垂直ワイプ（上→下）
};

/// @brief シーン遷移エフェクトの基底インターフェース
///
/// 遷移はOut（退場）とIn（入場）の2フェーズで構成される。
/// 各フェーズのprogressは0.0（開始）〜1.0（完了）で推移する。
class ITransition
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITransition() = default;

	/// @brief 遷移の合計時間を取得する（Out + In）
	/// @return 合計時間（秒）
	[[nodiscard]] virtual float totalDuration() const noexcept = 0;

	/// @brief Outフェーズの所要時間を取得する
	/// @return Out時間（秒）
	[[nodiscard]] virtual float outDuration() const noexcept = 0;

	/// @brief Inフェーズの所要時間を取得する
	/// @return In時間（秒）
	[[nodiscard]] virtual float inDuration() const noexcept = 0;

	/// @brief Outフェーズの描画パラメータを計算する
	/// @param progress 進行度（0.0〜1.0）
	/// @return オーバーレイのアルファ値（0.0=透明、1.0=不透明）
	[[nodiscard]] virtual float computeOutAlpha(float progress) const noexcept = 0;

	/// @brief Inフェーズの描画パラメータを計算する
	/// @param progress 進行度（0.0〜1.0）
	/// @return オーバーレイのアルファ値（0.0=透明、1.0=不透明）
	[[nodiscard]] virtual float computeInAlpha(float progress) const noexcept = 0;

	/// @brief Outフェーズのオフセットを計算する（スライド系で使用）
	/// @param progress 進行度（0.0〜1.0）
	/// @param screenWidth 画面幅
	/// @param screenHeight 画面高さ
	/// @return {x, y} オフセット
	[[nodiscard]] virtual std::pair<float, float> computeOutOffset(
		float /*progress*/, float /*screenWidth*/, float /*screenHeight*/) const noexcept
	{
		return {0.0f, 0.0f};
	}

	/// @brief Inフェーズのオフセットを計算する（スライド系で使用）
	/// @param progress 進行度（0.0〜1.0）
	/// @param screenWidth 画面幅
	/// @param screenHeight 画面高さ
	/// @return {x, y} オフセット
	[[nodiscard]] virtual std::pair<float, float> computeInOffset(
		float /*progress*/, float /*screenWidth*/, float /*screenHeight*/) const noexcept
	{
		return {0.0f, 0.0f};
	}
};

/// @brief フェード遷移（黒画面を介した暗転）
///
/// Out: アルファ 0→1（暗くなる）
/// In:  アルファ 1→0（明るくなる）
class FadeTransition : public ITransition
{
public:
	/// @brief コンストラクタ
	/// @param halfDuration 片方向のフェード時間（秒）。合計はこの2倍
	explicit FadeTransition(float halfDuration = 0.3f) noexcept
		: m_halfDuration{std::max(halfDuration, 0.0f)}
	{
	}

	[[nodiscard]] float totalDuration() const noexcept override
	{
		return m_halfDuration * 2.0f;
	}

	[[nodiscard]] float outDuration() const noexcept override
	{
		return m_halfDuration;
	}

	[[nodiscard]] float inDuration() const noexcept override
	{
		return m_halfDuration;
	}

	[[nodiscard]] float computeOutAlpha(float progress) const noexcept override
	{
		return std::clamp(progress, 0.0f, 1.0f);
	}

	[[nodiscard]] float computeInAlpha(float progress) const noexcept override
	{
		return std::clamp(1.0f - progress, 0.0f, 1.0f);
	}

private:
	float m_halfDuration;
};

/// @brief スライド遷移
///
/// Out: 旧シーンが指定方向にスライドアウト
/// In:  新シーンが反対方向からスライドイン
class SlideTransition : public ITransition
{
public:
	/// @brief コンストラクタ
	/// @param halfDuration 片方向の遷移時間（秒）
	/// @param direction スライド方向
	explicit SlideTransition(float halfDuration = 0.3f,
		SlideDirection direction = SlideDirection::Left) noexcept
		: m_halfDuration{std::max(halfDuration, 0.0f)}
		, m_direction{direction}
	{
	}

	[[nodiscard]] float totalDuration() const noexcept override
	{
		return m_halfDuration * 2.0f;
	}

	[[nodiscard]] float outDuration() const noexcept override
	{
		return m_halfDuration;
	}

	[[nodiscard]] float inDuration() const noexcept override
	{
		return m_halfDuration;
	}

	[[nodiscard]] float computeOutAlpha(float progress) const noexcept override
	{
		return std::clamp(progress, 0.0f, 1.0f);
	}

	[[nodiscard]] float computeInAlpha(float progress) const noexcept override
	{
		return std::clamp(1.0f - progress, 0.0f, 1.0f);
	}

	[[nodiscard]] std::pair<float, float> computeOutOffset(
		float progress, float screenWidth, float screenHeight) const noexcept override
	{
		const float t = std::clamp(progress, 0.0f, 1.0f);
		switch (m_direction)
		{
		case SlideDirection::Left:  return {-t * screenWidth, 0.0f};
		case SlideDirection::Right: return {t * screenWidth, 0.0f};
		case SlideDirection::Up:    return {0.0f, -t * screenHeight};
		case SlideDirection::Down:  return {0.0f, t * screenHeight};
		}
		return {0.0f, 0.0f};
	}

	[[nodiscard]] std::pair<float, float> computeInOffset(
		float progress, float screenWidth, float screenHeight) const noexcept override
	{
		const float t = std::clamp(1.0f - progress, 0.0f, 1.0f);
		switch (m_direction)
		{
		case SlideDirection::Left:  return {t * screenWidth, 0.0f};
		case SlideDirection::Right: return {-t * screenWidth, 0.0f};
		case SlideDirection::Up:    return {0.0f, t * screenHeight};
		case SlideDirection::Down:  return {0.0f, -t * screenHeight};
		}
		return {0.0f, 0.0f};
	}

private:
	float m_halfDuration;
	SlideDirection m_direction;
};

/// @brief ワイプ遷移（幕が閉じて開く演出）
///
/// Out: ワイプが閉じていく（coverage 0→1）
/// In:  ワイプが開いていく（coverage 1→0）
class WipeTransition : public ITransition
{
public:
	/// @brief コンストラクタ
	/// @param halfDuration 片方向の遷移時間（秒）
	/// @param direction ワイプ方向
	explicit WipeTransition(float halfDuration = 0.3f,
		WipeDirection direction = WipeDirection::Horizontal) noexcept
		: m_halfDuration{std::max(halfDuration, 0.0f)}
		, m_direction{direction}
	{
	}

	[[nodiscard]] float totalDuration() const noexcept override
	{
		return m_halfDuration * 2.0f;
	}

	[[nodiscard]] float outDuration() const noexcept override
	{
		return m_halfDuration;
	}

	[[nodiscard]] float inDuration() const noexcept override
	{
		return m_halfDuration;
	}

	/// @brief ワイプのカバー率を返す（Out: 0→1で閉じる）
	[[nodiscard]] float computeOutAlpha(float progress) const noexcept override
	{
		return std::clamp(progress, 0.0f, 1.0f);
	}

	/// @brief ワイプのカバー率を返す（In: 1→0で開く）
	[[nodiscard]] float computeInAlpha(float progress) const noexcept override
	{
		return std::clamp(1.0f - progress, 0.0f, 1.0f);
	}

	/// @brief ワイプ方向を取得する
	[[nodiscard]] WipeDirection direction() const noexcept { return m_direction; }

private:
	float m_halfDuration;
	WipeDirection m_direction;
};

/// @brief 遷移マネージャー
///
/// ITransition実装を管理し、フェーズの進行を制御する。
/// SceneManagerと連携して遷移演出を実現する。
class TransitionManager
{
public:
	/// @brief 遷移を開始する
	/// @param transition 遷移エフェクト
	void start(std::unique_ptr<ITransition> transition)
	{
		m_transition = std::move(transition);
		m_phase = TransitionPhase::Out;
		m_elapsed = 0.0f;
	}

	/// @brief 遷移を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (!m_transition || m_phase == TransitionPhase::Idle || m_phase == TransitionPhase::Complete)
		{
			return;
		}

		m_elapsed += dt;

		if (m_phase == TransitionPhase::Out)
		{
			if (m_elapsed >= m_transition->outDuration())
			{
				m_phase = TransitionPhase::In;
				m_elapsed = 0.0f;
				m_midpointReached = true;
			}
		}
		else if (m_phase == TransitionPhase::In)
		{
			if (m_elapsed >= m_transition->inDuration())
			{
				m_phase = TransitionPhase::Complete;
				m_elapsed = 0.0f;
			}
		}
	}

	/// @brief 現在のフェーズを取得する
	[[nodiscard]] TransitionPhase phase() const noexcept { return m_phase; }

	/// @brief 現在のフェーズの進行度を取得する
	/// @return 0.0〜1.0
	[[nodiscard]] float getProgress() const noexcept
	{
		if (!m_transition) return 0.0f;

		if (m_phase == TransitionPhase::Out)
		{
			const float dur = m_transition->outDuration();
			return dur > 0.0f ? std::min(m_elapsed / dur, 1.0f) : 1.0f;
		}
		else if (m_phase == TransitionPhase::In)
		{
			const float dur = m_transition->inDuration();
			return dur > 0.0f ? std::min(m_elapsed / dur, 1.0f) : 1.0f;
		}
		return 0.0f;
	}

	/// @brief 現在のアルファ値を取得する
	[[nodiscard]] float getAlpha() const noexcept
	{
		if (!m_transition) return 0.0f;

		if (m_phase == TransitionPhase::Out)
		{
			return m_transition->computeOutAlpha(getProgress());
		}
		else if (m_phase == TransitionPhase::In)
		{
			return m_transition->computeInAlpha(getProgress());
		}
		return 0.0f;
	}

	/// @brief 現在のオフセットを取得する
	/// @param screenWidth 画面幅
	/// @param screenHeight 画面高さ
	/// @return {x, y} オフセット
	[[nodiscard]] std::pair<float, float> getOffset(float screenWidth, float screenHeight) const noexcept
	{
		if (!m_transition) return {0.0f, 0.0f};

		if (m_phase == TransitionPhase::Out)
		{
			return m_transition->computeOutOffset(getProgress(), screenWidth, screenHeight);
		}
		else if (m_phase == TransitionPhase::In)
		{
			return m_transition->computeInOffset(getProgress(), screenWidth, screenHeight);
		}
		return {0.0f, 0.0f};
	}

	/// @brief 遷移が完了したか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_phase == TransitionPhase::Complete;
	}

	/// @brief 遷移がアクティブか（OutまたはIn中）
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_phase == TransitionPhase::Out || m_phase == TransitionPhase::In;
	}

	/// @brief 中間点（Out完了、In開始）に到達したか（1回だけtrue）
	[[nodiscard]] bool consumeMidpoint() noexcept
	{
		if (m_midpointReached)
		{
			m_midpointReached = false;
			return true;
		}
		return false;
	}

	/// @brief 遷移をリセットする
	void reset() noexcept
	{
		m_transition.reset();
		m_phase = TransitionPhase::Idle;
		m_elapsed = 0.0f;
		m_midpointReached = false;
	}

	/// @brief 現在の遷移エフェクトを取得する
	[[nodiscard]] const ITransition* transition() const noexcept
	{
		return m_transition.get();
	}

private:
	std::unique_ptr<ITransition> m_transition;
	TransitionPhase m_phase{TransitionPhase::Idle};
	float m_elapsed{0.0f};
	bool m_midpointReached{false};
};

} // namespace sgc
