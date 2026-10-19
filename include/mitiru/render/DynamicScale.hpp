#pragma once

/// @file DynamicScale.hpp
/// @brief フレーム時間に応じた 3D 内部解像度倍率の制御

#include <algorithm>

namespace mitiru::render
{

struct DynamicScaleConfig
{
	int   minScale20 = 0;
	int   maxScale20 = 0;
	float targetMs = 0;
	float slowRatio = 0;
	float fastRatio = 0;
	float smoothing = 0;
	float ignoreAboveMs = 0;
	int   slowFrames = 0;
	int   fastFrames = 0;
	int   downStep20 = 0;
	int   probationFrames = 0;
};

/// @brief 描画負荷の一時的な揺れで内部解像度が頻繁に変わらないようにする
class DynamicScale
{
public:
	explicit DynamicScale(const DynamicScaleConfig& config)
		: config_(config)
		, scale20_(config.maxScale20)
		, ceiling20_(config.maxScale20)
	{
		clampState();
	}

	/// 設定を差し替える。倍率は新しい上下限に収める。平均と数え方は残す
	void configure(const DynamicScaleConfig& config)
	{
		config_ = config;
		clampState();
	}

	/// 直近のフレーム時間を渡し、次のフレームの倍率 (1/20 刻み) を返す
	[[nodiscard]] int update(float frameMs)
	{
		if (frameMs <= 0.0f || frameMs > config_.ignoreAboveMs)
		{
			return scale20_;
		}

		if (framesSinceRaise_ >= 0)
		{
			++framesSinceRaise_;
		}

		if (!hasEma_)
		{
			ema_ = frameMs;
			hasEma_ = true;
		}
		else
		{
			ema_ += (frameMs - ema_) * config_.smoothing;
		}

		if (ema_ > config_.targetMs * config_.slowRatio)
		{
			++slowStreak_;
			fastStreak_ = 0;
		}
		else if (ema_ < config_.targetMs * config_.fastRatio)
		{
			++fastStreak_;
			slowStreak_ = 0;
		}
		else
		{
			slowStreak_ = 0;
			fastStreak_ = 0;
		}

		const int minimum = minimumScale20();
		if (slowStreak_ >= config_.slowFrames && scale20_ > minimum)
		{
			scale20_ = std::clamp(scale20_ - config_.downStep20, minimum, config_.maxScale20);
			if (framesSinceRaise_ >= 0 && framesSinceRaise_ <= config_.probationFrames)
			{
				ceiling20_ = scale20_;
			}
			slowStreak_ = 0;
			fastStreak_ = 0;
			framesSinceRaise_ = -1;
		}
		else
		{
			const int upper = std::min(config_.maxScale20, ceiling20_);
			if (fastStreak_ >= config_.fastFrames && scale20_ < upper)
			{
				++scale20_;
				slowStreak_ = 0;
				fastStreak_ = 0;
				framesSinceRaise_ = 0;
			}
		}

		if (framesSinceRaise_ > config_.probationFrames)
		{
			framesSinceRaise_ = -1;
		}
		return scale20_;
	}

	[[nodiscard]] int scale20() const noexcept
	{
		return scale20_;
	}

	/// 倍率を上限に戻す。品質の段が変わったときなどに呼ぶ
	void reset()
	{
		scale20_ = config_.maxScale20;
		ceiling20_ = config_.maxScale20;
		ema_ = 0.0f;
		slowStreak_ = 0;
		fastStreak_ = 0;
		framesSinceRaise_ = -1;
		hasEma_ = false;
		clampState();
	}

private:
	[[nodiscard]] int minimumScale20() const noexcept
	{
		return std::min(config_.minScale20, config_.maxScale20);
	}

	void clampState()
	{
		const int minimum = minimumScale20();
		scale20_ = std::clamp(scale20_, minimum, config_.maxScale20);
		ceiling20_ = std::clamp(ceiling20_, minimum, config_.maxScale20);
	}

	DynamicScaleConfig config_;
	int                scale20_ = 0;
	int                ceiling20_ = 0;
	float              ema_ = 0.0f;
	int                slowStreak_ = 0;
	int                fastStreak_ = 0;
	int                framesSinceRaise_ = -1;
	bool               hasEma_ = false;
};

} // namespace mitiru::render
