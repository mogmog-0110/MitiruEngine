#pragma once

/// @file PhysicsBridge.hpp
/// @brief sgc物理エンジンとMitiruの統合ブリッジ
/// @details sgc::physics::FixedTimestepをラップし、
///          決定論的な物理更新をMitiruエンジンに統合する。

#include <functional>
#include <string>

#include <sgc/physics/FixedTimestep.hpp>

namespace mitiru::bridge
{

/// @brief 物理ブリッジ
/// @details sgcの固定タイムステップ物理更新をMitiruエンジンに統合する。
///
/// @code
/// mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
/// physics.step(dt, [](double stepDt) {
///     updatePhysicsWorld(stepDt);
/// });
/// @endcode
class PhysicsBridge
{
public:
	/// @brief コンストラクタ
	/// @param stepSize 固定ステップ幅（秒）。デフォルトは1/60
	explicit PhysicsBridge(double stepSize = 1.0 / 60.0)
		: m_stepper(stepSize)
	{
	}

	/// @brief 固定タイムステップで物理更新を行う
	/// @tparam Callback void(double) で呼び出し可能な型
	/// @param dt フレームデルタタイム（秒）
	/// @param callback 各ステップで呼ばれるコールバック
	/// @return 実行されたステップ数
	template <std::invocable<double> Callback>
	int step(double dt, Callback&& callback)
	{
		const int steps = m_stepper.update(dt, std::forward<Callback>(callback));
		m_totalSteps += steps;
		return steps;
	}

	/// @brief 蓄積時間をリセットする
	void resetAccumulator() noexcept
	{
		m_stepper.resetAccumulator();
	}

	/// @brief ステップ幅を取得する
	/// @return ステップ幅（秒）
	[[nodiscard]] double stepSize() const noexcept
	{
		return m_stepper.stepSize();
	}

	/// @brief ステップ幅を変更する
	/// @param stepSize 新しいステップ幅（秒）
	void setStepSize(double stepSize) noexcept
	{
		m_stepper.setStepSize(stepSize);
	}

	/// @brief 補間係数を取得する（描画の線形補間用）
	/// @return 0.0〜1.0の補間係数
	[[nodiscard]] double interpolationFactor() const noexcept
	{
		return m_stepper.interpolationFactor();
	}

	/// @brief 累計ステップ数を取得する
	/// @return 累計ステップ数
	[[nodiscard]] int totalSteps() const noexcept
	{
		return m_totalSteps;
	}

	/// @brief 最大ステップ数を設定する（スパイラル・オブ・デス防止）
	/// @param steps 最大ステップ数
	void setMaxSteps(int steps) noexcept
	{
		m_stepper.setMaxSteps(steps);
	}

	/// @brief 物理状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"stepSize\":" + std::to_string(m_stepper.stepSize()) + ",";
		json += "\"accumulated\":" + std::to_string(m_stepper.accumulated()) + ",";
		json += "\"interpolationFactor\":" + std::to_string(m_stepper.interpolationFactor()) + ",";
		json += "\"totalSteps\":" + std::to_string(m_totalSteps) + ",";
		json += "\"maxSteps\":" + std::to_string(m_stepper.maxSteps());
		json += "}";
		return json;
	}

private:
	sgc::physics::FixedTimestep m_stepper;   ///< 固定タイムステップ
	int m_totalSteps = 0;                    ///< 累計ステップ数
};

} // namespace mitiru::bridge
