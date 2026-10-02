#pragma once

/// @file IKGroundSystem.hpp
/// @brief アニメーション適用後に足を地面の高さへ合わせる IK システム
/// @details `UpdatePhase::PostAnim`（Anim と Camera の間）を専用フェーズとして使う。

#include <functional>
#include <vector>

#include "mitiru/animation/IKSolver.hpp"
#include "mitiru/scene/SystemRunner.hpp"
#include "mitiru/scene/UpdatePhase.hpp"

namespace mitiru::animation
{

/// @brief 地面の高さを問い合わせる関数
/// @details 物理エンジンへ直接依存しないようにする。呼び出し元が
///          レイキャストでもハイトマップ参照でも自由に実装できる。
using GroundHeightFn = std::function<float(float worldX, float worldZ)>;

/// @brief PostAnim フェーズで足を地面に接地させる IK システム
/// @details 各脚は `FABRIKSolver` 上のジョイントチェーンとして登録する。
///          エンドエフェクタ（チェーン末尾）の現在位置の x/z を保ったまま、
///          y だけを `GroundHeightFn` が返す高さへ向けて解決する。
class IKGroundSystem final : public mitiru::scene::ISystem
{
public:
	explicit IKGroundSystem(GroundHeightFn groundHeightFn)
		: m_groundHeightFn(std::move(groundHeightFn))
	{
	}

	[[nodiscard]] std::string name() const override { return "IKGround"; }

	[[nodiscard]] mitiru::scene::UpdatePhase defaultPhase() const noexcept override
	{
		return mitiru::scene::UpdatePhase::PostAnim;
	}

	/// @brief 内部の FABRIKSolver への参照
	/// @details 呼び出し側がジョイントの初期姿勢（アニメーション適用後の骨）を書き込む
	[[nodiscard]] FABRIKSolver& solver() noexcept { return m_solver; }

	/// @brief 接地させる脚を登録する
	/// @param chain エンドエフェクタが足先ジョイントになるように組んだ IK チェーン
	void addFoot(IKChain chain) { m_feet.push_back(std::move(chain)); }

	/// @brief 登録済みの脚の数
	[[nodiscard]] size_t footCount() const noexcept { return m_feet.size(); }

	void update(mitiru::scene::GameWorld&, float) override
	{
		for (auto& chain : m_feet)
		{
			solveOneFoot(chain);
		}
	}

private:
	/// @brief 1 本の脚のターゲットを現在位置と地面の高さから組み立てて解決する
	void solveOneFoot(IKChain& chain)
	{
		auto& joints = m_solver.joints();
		if (chain.jointIndices.empty()) return;

		const int footIdx = chain.jointIndices.back();
		if (footIdx < 0 || static_cast<size_t>(footIdx) >= joints.size()) return;

		const float* footPos = joints[static_cast<size_t>(footIdx)].position;
		chain.targetPos[0] = footPos[0];
		chain.targetPos[1] = m_groundHeightFn ? m_groundHeightFn(footPos[0], footPos[2]) : footPos[1];
		chain.targetPos[2] = footPos[2];

		m_solver.solve(chain);
	}

	FABRIKSolver m_solver;
	GroundHeightFn m_groundHeightFn;
	std::vector<IKChain> m_feet;
};

} // namespace mitiru::animation
