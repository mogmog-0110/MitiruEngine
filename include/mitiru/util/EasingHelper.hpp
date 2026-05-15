#pragma once

/// @file EasingHelper.hpp
/// @brief イージング関数とLerp補間ヘルパー

#include <cmath>
#include <algorithm>
#include <numbers>

namespace mitiru::util
{
	/// @brief 各種イージング関数を提供する構造体
	struct Easing
	{
		/// @brief 線形補間（変化なし）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static constexpr float linear(float t) noexcept
		{
			return t;
		}

		/// @brief 二次イーズイン（加速）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static constexpr float easeInQuad(float t) noexcept
		{
			return t * t;
		}

		/// @brief 二次イーズアウト（減速）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static constexpr float easeOutQuad(float t) noexcept
		{
			return t * (2.0f - t);
		}

		/// @brief 二次イーズインアウト（加速→減速）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static constexpr float easeInOutQuad(float t) noexcept
		{
			if (t < 0.5f)
			{
				return 2.0f * t * t;
			}
			return -1.0f + (4.0f - 2.0f * t) * t;
		}

		/// @brief バウンスイーズアウト（跳ね返り効果）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static inline float easeOutBounce(float t) noexcept
		{
			constexpr float n1 = 7.5625f;
			constexpr float d1 = 2.75f;

			if (t < 1.0f / d1)
			{
				return n1 * t * t;
			}
			else if (t < 2.0f / d1)
			{
				const float adj = t - 1.5f / d1;
				return n1 * adj * adj + 0.75f;
			}
			else if (t < 2.5f / d1)
			{
				const float adj = t - 2.25f / d1;
				return n1 * adj * adj + 0.9375f;
			}
			else
			{
				const float adj = t - 2.625f / d1;
				return n1 * adj * adj + 0.984375f;
			}
		}

		/// @brief エラスティックイーズアウト（弾性振動効果）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static inline float easeOutElastic(float t) noexcept
		{
			if (t <= 0.0f)
			{
				return 0.0f;
			}
			if (t >= 1.0f)
			{
				return 1.0f;
			}

			constexpr float pi = std::numbers::pi_v<float>;
			constexpr float c4 = (2.0f * pi) / 3.0f;

			return std::pow(2.0f, -10.0f * t)
				* std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
		}

		/// @brief 三次イーズインアウト（加速→減速、より滑らか）
		/// @param t 正規化時間 [0, 1]
		/// @return 補間値
		static constexpr float easeInOutCubic(float t) noexcept
		{
			if (t < 0.5f)
			{
				return 4.0f * t * t * t;
			}
			const float f = 2.0f * t - 2.0f;
			return 0.5f * f * f * f + 1.0f;
		}
	};

	/// @brief 線形補間（Lerp）ヘルパー構造体
	/// @note duration中のelapsed時間に基づいてfromからtoへ補間する
	struct Lerp
	{
		/// @brief 補間開始値
		float from = 0.0f;
		/// @brief 補間終了値
		float to = 1.0f;
		/// @brief 補間にかかる時間（秒）
		float duration = 1.0f;

		/// @brief 線形補間で現在値を取得する
		/// @param elapsed 経過時間（秒）
		/// @return 補間された値
		[[nodiscard]] float value(float elapsed) const noexcept
		{
			const float t = std::clamp(elapsed / duration, 0.0f, 1.0f);
			return from + (to - from) * t;
		}

		/// @brief イージング関数を適用して現在値を取得する
		/// @tparam EaseFunc イージング関数の型
		/// @param elapsed 経過時間（秒）
		/// @param ease イージング関数
		/// @return イージング適用後の補間値
		template <typename EaseFunc>
		[[nodiscard]] float value(float elapsed, EaseFunc ease) const noexcept
		{
			const float t = std::clamp(elapsed / duration, 0.0f, 1.0f);
			return from + (to - from) * ease(t);
		}
	};

} // namespace mitiru::util
