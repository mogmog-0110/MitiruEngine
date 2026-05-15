#pragma once

/// @file GameLoop.hpp
/// @brief スタンドアロンゲームループユーティリティ
/// @details 固定タイムステップ＋可変レンダリングパターンの
///          汎用ゲームループ関数テンプレートを提供する。

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <type_traits>

namespace mitiru
{

/// @brief ゲームループ設定
struct GameLoopConfig
{
	float targetFps = 60.0f;             ///< 目標FPS
	float fixedDeltaTime = 1.0f / 60.0f; ///< 固定タイムステップ（秒）
	int maxFrameSkip = 5;                ///< 最大フレームスキップ数
};

/// @brief 汎用ゲームループを実行する
/// @tparam UpdateFn void(float dt) 型の呼び出し可能オブジェクト
/// @tparam RenderFn void() 型の呼び出し可能オブジェクト
/// @tparam QuitFn bool() 型の呼び出し可能オブジェクト
/// @param config ゲームループ設定
/// @param updateFn 固定タイムステップで呼ばれる更新関数
/// @param renderFn 毎フレーム呼ばれる描画関数
/// @param shouldQuitFn 終了判定関数（trueを返すとループ終了）
/// @return 実行したフレーム数
///
/// @code
/// mitiru::GameLoopConfig config;
/// config.targetFps = 60.0f;
/// config.fixedDeltaTime = 1.0f / 60.0f;
///
/// mitiru::runGameLoop(config,
///     [](float dt) { updatePhysics(dt); },
///     []() { render(); },
///     [&]() { return window.shouldClose(); });
/// @endcode
template <typename UpdateFn, typename RenderFn, typename QuitFn>
std::uint64_t runGameLoop(
	const GameLoopConfig& config,
	UpdateFn&& updateFn,
	RenderFn&& renderFn,
	QuitFn&& shouldQuitFn)
{
	using SteadyClock = std::chrono::steady_clock;
	using Duration = std::chrono::duration<float>;

	const float fixedDt = config.fixedDeltaTime;
	const float maxDelta = 0.1f; ///< スパイラルオブデス防止
	const float targetFrameTime = (config.targetFps > 0.0f)
		? (1.0f / config.targetFps)
		: 0.0f;

	auto previousTime = SteadyClock::now();
	float accumulator = 0.0f;
	std::uint64_t frameCount = 0;

	while (!shouldQuitFn())
	{
		const auto currentTime = SteadyClock::now();
		float frameTime = std::chrono::duration<float>(
			currentTime - previousTime).count();
		previousTime = currentTime;

		/// デルタタイムをキャップ
		frameTime = std::min(frameTime, maxDelta);
		accumulator += frameTime;

		/// 固定タイムステップで更新（最大スキップ制限付き）
		int steps = 0;
		while (accumulator >= fixedDt && steps < config.maxFrameSkip)
		{
			updateFn(fixedDt);
			accumulator -= fixedDt;
			++steps;
		}

		/// レンダリング
		renderFn();
		++frameCount;

		/// フレームレート制限（ターゲットFPSへスリープ）
		if (targetFrameTime > 0.0f)
		{
			const auto endTime = SteadyClock::now();
			const float elapsed = std::chrono::duration<float>(
				endTime - currentTime).count();
			const float sleepTime = targetFrameTime - elapsed;
			if (sleepTime > 0.0f)
			{
				std::this_thread::sleep_for(
					Duration(sleepTime));
			}
		}
	}

	return frameCount;
}

/// @brief フレーム数制限付きゲームループを実行する（テスト用）
/// @tparam UpdateFn void(float dt) 型の呼び出し可能オブジェクト
/// @tparam RenderFn void() 型の呼び出し可能オブジェクト
/// @param config ゲームループ設定
/// @param maxFrames 最大実行フレーム数
/// @param updateFn 固定タイムステップで呼ばれる更新関数
/// @param renderFn 毎フレーム呼ばれる描画関数
/// @return 実行したフレーム数
template <typename UpdateFn, typename RenderFn>
std::uint64_t runGameLoopFrames(
	const GameLoopConfig& config,
	std::uint64_t maxFrames,
	UpdateFn&& updateFn,
	RenderFn&& renderFn)
{
	std::uint64_t frame = 0;
	return runGameLoop(
		config,
		std::forward<UpdateFn>(updateFn),
		std::forward<RenderFn>(renderFn),
		[&]() { return frame++ >= maxFrames; });
}

} // namespace mitiru
