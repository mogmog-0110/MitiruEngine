#pragma once

/// @file FixedStep.hpp
/// @brief 可変フレーム dt を固定 step に分割する accumulator パターンの薄いラッパ。
/// @details game の物理 / シミュレーションを framerate 非依存にしたい時に使う。毎フレーム
///          `advance(frameDt)` を呼んで返り値の回数だけ固定 step を回す。spiral-of-death を防ぐ
///          ため 1 フレーム最大ステップ数で頭打ち。`interpolationAlpha()` は描画補間用 [0,1)。

#include <cmath>

namespace mitiru::time
{

class FixedStep
{
public:
	float dt              = 1.0f / 60.0f;  ///< 固定 step 秒。60 Hz が既定。
	int   maxStepsPerFrame = 5;            ///< 1 フレームの最大 step 数 (spiral 防止)。

	/// @brief frameDt を accumulator に加算し、流すべき step 数を返す。
	int advance(float frameDt) noexcept
	{
		if (dt <= 0.0f) { return 0; }
		m_accumulator += frameDt;
		int n = 0;
		while (m_accumulator >= dt && n < maxStepsPerFrame)
		{
			m_accumulator -= dt;
			++n;
		}
		// max step まで回しても dt 以上余っていれば backlog を捨てて wall-clock に
		// 再同期する (スローモーションを受け入れて時刻を取り戻す)。残余 [0,dt) は
		// 補間用に保つ。
		if (n >= maxStepsPerFrame && m_accumulator >= dt)
		{
			m_accumulator = std::fmod(m_accumulator, dt);
		}
		return n;
	}

	/// @brief 描画補間用の [0, 1) 値。次の step までどれだけ進んでるか。
	[[nodiscard]] float interpolationAlpha() const noexcept
	{
		if (dt <= 0.0f) { return 0.0f; }
		const float a = m_accumulator / dt;
		return (a < 0.0f) ? 0.0f : (a > 1.0f ? 1.0f : a);
	}

	[[nodiscard]] float accumulator() const noexcept { return m_accumulator; }
	void reset() noexcept { m_accumulator = 0.0f; }

private:
	float m_accumulator = 0.0f;
};

}  // namespace mitiru::time
