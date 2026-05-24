#pragma once

/// @file LSystem.hpp
/// @brief Lシステム文字列生成とタートルグラフィクス解釈
///
/// 確率的Lシステムによる文字列置換と、
/// タートルグラフィクスによる2D線分生成を提供する。
///
/// @code
/// sgc::procedural::LSystemConfig config;
/// config.axiom = "F";
/// config.rules = {{'F', "F[+F]F[-F]F"}};
/// config.iterations = 3;
/// auto lString = sgc::procedural::generateLSystem(config);
///
/// sgc::procedural::TurtleConfig turtle;
/// turtle.angleIncrement = 25.7f;
/// auto segments = sgc::procedural::interpretTurtle(lString, turtle);
/// @endcode

#include <cmath>
#include <cstdint>
#include <random>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "sgc/math/Vec2.hpp"

namespace sgc::procedural
{

/// @brief Lシステムの置換ルール
struct LSystemRule
{
	char predecessor;          ///< 置換対象の文字
	std::string successor;     ///< 置換後の文字列
	float probability = 1.0f;  ///< 適用確率（確率的Lシステム用, 0.0〜1.0）
};

/// @brief Lシステム設定
struct LSystemConfig
{
	std::string axiom;                   ///< 初期文字列（公理）
	std::vector<LSystemRule> rules;      ///< 置換ルール一覧
	int iterations = 4;                  ///< 置換反復回数
	uint32_t seed = 0;                   ///< 乱数シード（確率的ルール用）
};

/// @brief タートルグラフィクスの設定
struct TurtleConfig
{
	float stepLength = 10.0f;            ///< 前進距離
	float angleIncrement = 25.0f;        ///< 回転角度（度数）
	sgc::Vec2f startPosition{0.0f, 0.0f}; ///< 開始位置
	float startAngle = -90.0f;           ///< 開始角度（度数、-90=上向き）
};

/// @brief タートルグラフィクスの結果
struct TurtleResult
{
	std::vector<std::pair<sgc::Vec2f, sgc::Vec2f>> segments;  ///< 線分の始点・終点ペア
};

/// @brief Lシステム文字列を生成する
///
/// 公理に対して置換ルールを指定回数適用し、結果文字列を返す。
/// 同一文字に対する複数ルールは確率に基づいて選択される。
///
/// @param config Lシステム設定
/// @return 生成された文字列
[[nodiscard]] inline std::string generateLSystem(const LSystemConfig& config)
{
	if (config.axiom.empty())
	{
		return std::string{};
	}

	std::string current = config.axiom;
	std::mt19937 rng(config.seed);

	for (int iter = 0; iter < config.iterations; ++iter)
	{
		std::string next;
		next.reserve(current.size() * 2);

		for (const char ch : current)
		{
			// この文字に適用可能なルールを収集
			std::vector<const LSystemRule*> candidates;
			float totalProb = 0.0f;

			for (const auto& rule : config.rules)
			{
				if (rule.predecessor == ch)
				{
					candidates.push_back(&rule);
					totalProb += rule.probability;
				}
			}

			if (candidates.empty())
			{
				// ルールなし：文字をそのまま保持
				next += ch;
				continue;
			}

			// 確率的選択
			if (candidates.size() == 1 && candidates[0]->probability >= 1.0f)
			{
				// 単一ルール・確率1.0の場合は乱数を使わない
				next += candidates[0]->successor;
			}
			else
			{
				std::uniform_real_distribution<float> dist(0.0f, totalProb);
				float roll = dist(rng);
				bool applied = false;

				for (const auto* rule : candidates)
				{
					roll -= rule->probability;
					if (roll <= 0.0f)
					{
						next += rule->successor;
						applied = true;
						break;
					}
				}

				// フォールバック（丸め誤差対策）
				if (!applied)
				{
					next += candidates.back()->successor;
				}
			}
		}

		current = std::move(next);
	}

	return current;
}

namespace detail
{

/// @brief 度数をラジアンに変換する
/// @param degrees 角度（度数）
/// @return 角度（ラジアン）
[[nodiscard]] constexpr float degreesToRadians(float degrees) noexcept
{
	return degrees * 3.14159265358979323846f / 180.0f;
}

} // namespace detail

/// @brief Lシステム文字列をタートルグラフィクスとして解釈する
///
/// 以下の命令を認識する:
/// - F: 前進して線を描画
/// - f: 前進（描画なし）
/// - +: 左回転（反時計回り）
/// - -: 右回転（時計回り）
/// - [: 状態をプッシュ（分岐開始）
/// - ]: 状態をポップ（分岐終了）
///
/// @param lString Lシステム文字列
/// @param config タートル設定
/// @return 生成された線分データ
[[nodiscard]] inline TurtleResult interpretTurtle(
	const std::string& lString,
	const TurtleConfig& config)
{
	TurtleResult result;

	/// @brief タートルの状態
	struct TurtleState
	{
		sgc::Vec2f position;
		float angle;  ///< 度数
	};

	TurtleState state;
	state.position = config.startPosition;
	state.angle = config.startAngle;

	std::stack<TurtleState> stateStack;

	for (const char ch : lString)
	{
		switch (ch)
		{
		case 'F':
		{
			// 前進して線を描画
			const float rad = detail::degreesToRadians(state.angle);
			const sgc::Vec2f from = state.position;
			state.position = sgc::Vec2f{
				from.x + std::cos(rad) * config.stepLength,
				from.y + std::sin(rad) * config.stepLength
			};
			result.segments.emplace_back(from, state.position);
			break;
		}
		case 'f':
		{
			// 前進（描画なし）
			const float rad = detail::degreesToRadians(state.angle);
			state.position = sgc::Vec2f{
				state.position.x + std::cos(rad) * config.stepLength,
				state.position.y + std::sin(rad) * config.stepLength
			};
			break;
		}
		case '+':
		{
			// 左回転（反時計回り）
			state.angle -= config.angleIncrement;
			break;
		}
		case '-':
		{
			// 右回転（時計回り）
			state.angle += config.angleIncrement;
			break;
		}
		case '[':
		{
			// 状態プッシュ
			stateStack.push(state);
			break;
		}
		case ']':
		{
			// 状態ポップ
			if (!stateStack.empty())
			{
				state = stateStack.top();
				stateStack.pop();
			}
			break;
		}
		default:
			// その他の文字は無視
			break;
		}
	}

	return result;
}

} // namespace sgc::procedural
