#pragma once

/// @file FixedTimestep.hpp
/// @brief フレームワーク非依存の固定タイムステップ物理更新
///
/// 可変フレームレートのゲームループで、物理演算を固定間隔で更新するためのユーティリティ。
/// 蓄積されたデルタタイムを固定ステップ幅で分割し、各ステップでコールバックを呼び出す。
///
/// @code
/// sgc::physics::FixedTimestep stepper{1.0 / 200.0}; // 200Hz
///
/// void update(double dt) {
///     stepper.update(dt, [&](double step) {
///         world.update(step);
///         checkCollisions();
///     });
/// }
/// @endcode

#include <concepts>

namespace sgc::physics
{

/// @brief 固定タイムステップ物理更新マネージャ
///
/// フレーム毎の可変デルタタイムを蓄積し、固定間隔で分割して
/// コールバックを呼び出すことで、決定論的な物理シミュレーションを実現する。
class FixedTimestep
{
public:
	/// @brief コンストラクタ
	/// @param stepSize 1ステップの時間幅（秒）。一般的には 1/60, 1/120, 1/200 等
	explicit constexpr FixedTimestep(double stepSize = 1.0 / 200.0) noexcept
		: m_stepSize{stepSize}
	{
	}

	/// @brief 固定タイムステップで更新を行う
	///
	/// 蓄積時間がstepSize以上ある限り、コールバックを繰り返し呼び出す。
	/// コールバックには固定ステップ幅が渡される。
	///
	/// スパイラル・オブ・デス防止のため、1回のupdate()呼び出しで実行される
	/// ステップ数はm_maxSteps（デフォルト10）に制限される。
	/// 超過分の蓄積時間は切り捨てられる。
	///
	/// @tparam Callback `void(double stepSize)` で呼び出し可能な型
	/// @param dt フレームのデルタタイム（秒）
	/// @param callback 各ステップで呼ばれるコールバック
	/// @return 実行されたステップ数
	///
	/// @code
	/// int steps = stepper.update(dt, [&](double step) {
	///     physics.step(step);
	///     collisionHandler.process();
	/// });
	/// @endcode
	template <std::invocable<double> Callback>
	int update(double dt, Callback&& callback)
	{
		m_accumulated += dt;
		int stepCount = 0;

		while (m_accumulated >= m_stepSize && stepCount < m_maxSteps)
		{
			callback(m_stepSize);
			m_accumulated -= m_stepSize;
			++stepCount;
		}

		// スパイラル・オブ・デス防止: 上限到達時は蓄積時間を切り捨てる
		if (stepCount >= m_maxSteps)
		{
			m_accumulated = 0.0;
		}

		return stepCount;
	}

	/// @brief 蓄積時間をリセットする（ステージ再構築時等に使う）
	void resetAccumulator() noexcept { m_accumulated = 0.0; }

	/// @brief ステップ幅を取得する
	[[nodiscard]] constexpr double stepSize() const noexcept { return m_stepSize; }

	/// @brief ステップ幅を変更する
	/// @param stepSize 新しいステップ幅（秒）
	void setStepSize(double stepSize) noexcept { m_stepSize = stepSize; }

	/// @brief 最大ステップ数を取得する
	/// @return 1回のupdate()で実行される最大ステップ数
	[[nodiscard]] constexpr int maxSteps() const noexcept { return m_maxSteps; }

	/// @brief 最大ステップ数を設定する（スパイラル・オブ・デス防止）
	/// @param steps 最大ステップ数（1以上を推奨）
	void setMaxSteps(int steps) noexcept { m_maxSteps = steps; }

	/// @brief 蓄積時間を取得する（補間描画用）
	///
	/// 前回のステップ終了からの残余時間。
	/// 描画時にこの値をステップ幅で割った比率で線形補間すると、
	/// なめらかな描画が可能になる。
	///
	/// @return 蓄積時間（秒）
	[[nodiscard]] double accumulated() const noexcept { return m_accumulated; }

	/// @brief 補間係数を取得する（0.0〜1.0）
	///
	/// `accumulated() / stepSize()` の値。描画の線形補間に使う。
	///
	/// @return 補間係数
	[[nodiscard]] double interpolationFactor() const noexcept
	{
		return (m_stepSize > 0.0) ? (m_accumulated / m_stepSize) : 0.0;
	}

private:
	double m_stepSize;
	double m_accumulated = 0.0;
	int m_maxSteps{10};  ///< スパイラル・オブ・デス防止用の最大ステップ数
};

} // namespace sgc::physics
