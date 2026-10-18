#pragma once

/// @file BusFx.hpp
/// @brief hud.busPitch / hud.busLowPass で頼まれたバスの高さと low-pass を、傾きをかけて動かす (ABI v55、ADR 0080)
/// @details 添字は kSoundBus* と同じで、0 (Master) は全部のバスに掛かる。高さは倍率を、low-pass は周波数の log2 を
///          一定の速さで動かすので、耳には一定の速さのスイープに聞こえる。low-pass の 0 は「掛けない」で、動かす間は
///          kOpenHz として扱う。音声の device を持たないのでそのままテストできる。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace mitiru::audio::mix
{

class BusFx
{
public:
	static constexpr std::size_t kBusCount = 8;
	static constexpr std::size_t kMusicBus = 1;
	static constexpr std::size_t kVoiceBus = 3;
	/// これより高い周波数は、掛けないものとして扱う
	static constexpr float kOpenHz = 20000.0f;
	static constexpr float kMinHz = 20.0f;
	static constexpr float kMinPitch = 0.125f;
	static constexpr float kMaxPitch = 4.0f;

	BusFx()
	{
		for (Ramp& r : m_pitch) { r = Ramp{1.0f, 1.0f, 0.0f}; }
		for (Ramp& r : m_lowPass) { r = Ramp{kOpenLog2, kOpenLog2, 0.0f}; }
	}

	void setPitch(std::size_t bus, float pitch, float rampSec) noexcept
	{
		if (bus >= kBusCount) { return; }
		const float target = std::clamp((pitch > 0.0f) ? pitch : 1.0f, kMinPitch, kMaxPitch);
		retarget(m_pitch[bus], target, rampSec);
	}

	/// @param hz 0 以下または kOpenHz 以上で外す
	void setLowPass(std::size_t bus, float hz, float rampSec) noexcept
	{
		if (bus >= kBusCount) { return; }
		const float target = (hz > 0.0f && hz < kOpenHz) ? std::log2(std::max(hz, kMinHz)) : kOpenLog2;
		retarget(m_lowPass[bus], target, rampSec);
	}

	/// @return dt 秒進めたときに値が変わったか
	bool step(float dt) noexcept
	{
		bool changed = false;
		for (Ramp& r : m_pitch) { changed = advance(r, dt) || changed; }
		for (Ramp& r : m_lowPass) { changed = advance(r, dt) || changed; }
		return changed;
	}

	[[nodiscard]] float pitch(std::size_t bus) const noexcept { return (bus < kBusCount) ? m_pitch[bus].current : 1.0f; }

	/// @return 現在の周波数。掛けていなければ 0
	[[nodiscard]] float lowPassHz(std::size_t bus) const noexcept
	{
		if (bus >= kBusCount || m_lowPass[bus].current >= kOpenLog2) { return 0.0f; }
		return std::exp2(m_lowPass[bus].current);
	}

	/// @brief 効果音に掛けるピッチ。各バスの値に Master を掛ける
	[[nodiscard]] std::array<float, kBusCount> effectPitch() const noexcept
	{
		std::array<float, kBusCount> out{};
		for (std::size_t b = 0; b < kBusCount; ++b) { out[b] = pitch(b) * ((b == 0) ? 1.0f : pitch(0)); }
		out[kVoiceBus] = 1.0f;   // 台詞は聞き取れるよう、Voice のバスの効果音にも掛けない
		return out;
	}

	/// @brief 効果音に掛ける low-pass。各バスと Master の低い方を使う。0 は掛けない
	[[nodiscard]] std::array<float, kBusCount> effectLowPass() const noexcept
	{
		std::array<float, kBusCount> out{};
		for (std::size_t b = 0; b < kBusCount; ++b) { out[b] = lowerCutoff(lowPassHz(b), lowPassHz(0)); }
		out[kVoiceBus] = 0.0f;
		return out;
	}

	/// @brief 曲に掛ける low-pass。Music と Master の低い方を使う
	[[nodiscard]] float musicLowPassHz() const noexcept { return lowerCutoff(lowPassHz(kMusicBus), lowPassHz(0)); }

	/// @brief 2 つの low-pass の低い方を返す。0 は掛けない
	[[nodiscard]] static float lowerCutoff(float a, float b) noexcept
	{
		if (a <= 0.0f) { return b; }
		if (b <= 0.0f) { return a; }
		return std::min(a, b);
	}

private:
	struct Ramp
	{
		float current;
		float target;
		float perSec;   ///< 1 秒に動く量。0 は次の step ですぐ着く
	};

	static constexpr float kOpenLog2 = 14.287712f;   // log2(kOpenHz)

	static void retarget(Ramp& r, float target, float rampSec) noexcept
	{
		r.target = target;
		r.perSec = (rampSec > 0.0f) ? std::fabs(target - r.current) / rampSec : 0.0f;
		if (r.perSec <= 0.0f) { r.current = target; }
	}

	static bool advance(Ramp& r, float dt) noexcept
	{
		if (r.current == r.target) { return false; }
		const float stepBy = r.perSec * dt;
		const float gap = r.target - r.current;
		r.current = (std::fabs(gap) <= stepBy) ? r.target : r.current + std::copysign(stepBy, gap);
		return true;
	}

	std::array<Ramp, kBusCount> m_pitch{};
	std::array<Ramp, kBusCount> m_lowPass{};
};

}  // namespace mitiru::audio::mix
