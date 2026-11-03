#pragma once

/// @file BusDistortion.hpp
/// @brief バスに掛けるディストーション (ソフトクリップ)。mix.json で書いたときだけ掛かる
/// @details 入力を driveDb だけ持ち上げて tanh に通す。tanh は 0 付近の傾きが 1 なので、小さい音は drive の分だけ大きくなり、
///          大きい音ほど天井 (±1) へ潰れて奇数次の倍音が増える。出力は levelDb で下げ、mix で元の音と混ぜる。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mitiru::audio::mix
{

struct DistortionSettings
{
	float driveDb = 12.0f;   ///< 0..40
	float mix = 1.0f;        ///< 0 は元の音だけ、1 はディストーションを通した音だけ
	float levelDb = 0.0f;    ///< -24..0。歪んだ後に下げる量
};

[[nodiscard]] inline const char* validateDistortion(const DistortionSettings& d) noexcept
{
	if (!(d.driveDb >= 0.0f && d.driveDb <= 40.0f)) { return "driveDb は 0 以上、40 以下"; }
	if (!(d.mix >= 0.0f && d.mix <= 1.0f)) { return "mix は 0 以上、1 以下"; }
	if (!(d.levelDb <= 0.0f && d.levelDb >= -24.0f)) { return "levelDb は -24 以上、0 以下"; }
	return "";
}

/// @brief 1 標本のディストーション。試験と mitiru verify の確かめ用に外へ出してある
[[nodiscard]] inline float softClip(float x, float drive) noexcept
{
	return std::tanh(x * drive);
}

class Distortion
{
public:
	void init(float, std::uint32_t channels) noexcept { m_channels = std::min<std::uint32_t>(channels, 2); }

	void setSettings(const DistortionSettings& s) noexcept
	{
		m_settings = s;
		m_drive = std::pow(10.0f, s.driveDb / 20.0f);
		m_level = std::pow(10.0f, s.levelDb / 20.0f);
	}

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		const float mix = m_settings.mix;
		const std::size_t n = static_cast<std::size_t>(frames) * m_channels;
		for (std::size_t i = 0; i < n; ++i)
		{
			const float x = in[i];
			out[i] = (x * (1.0f - mix) + softClip(x, m_drive) * mix) * m_level;
		}
	}

private:
	DistortionSettings m_settings;
	float              m_drive = 1.0f;
	float              m_level = 1.0f;
	std::uint32_t      m_channels = 2;
};

}  // namespace mitiru::audio::mix
