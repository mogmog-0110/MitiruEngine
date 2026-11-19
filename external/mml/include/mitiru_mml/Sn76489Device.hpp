#pragma once
/// @file Sn76489Device.hpp
/// @brief SN76489 (PSG) の Device (層 1): emu76489 (MIT) を包む
/// @details 矩形波 3 声とノイズ 1 声。音符や音量の知識は Sn76489Driver が持つ。
///          write() の port 0 が SN76489 本体の書き込み (reg は無視)、port 1 が Game Gear の
///          ステレオ指定レジスタ (value の bit4-7 が左、bit0-3 が右の各声の出力)。

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/DcBlocker.hpp>

#include <emu76489.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace mitiru_mml
{

/// @brief SN76489 のエミュレーター
class Sn76489Device final : public ChipDevice
{
public:
	/// @brief 標準のクロック (NTSC の色副搬送波 3.579545 MHz)
	static constexpr std::uint32_t CLOCK = 3579545;
	/// @brief ネイティブレート。チップの内部が進む速さ (クロック / 16) の整数部
	static constexpr std::uint32_t NATIVE_RATE = CLOCK / 16;
	/// @brief emu76489 に渡すクロック。レートの 16 倍にそろえ、1 回の計算がちょうど 1 サンプルになるようにする
	static constexpr std::uint32_t EMULATOR_CLOCK = NATIVE_RATE * 16;
	/// @brief 出力にかける係数 (1 声が最大で振幅 0.25 になるよう上げる)
	static constexpr float OUTPUT_GAIN = 2.0f;

	/// @param clock チップのクロック (Hz)。VGM の再生などで標準と違う値を使うときに指定する
	explicit Sn76489Device(std::uint32_t clock = CLOCK)
		: m_rate(clock / 16)
		, m_sng(SNG_new(m_rate * 16, m_rate), &SNG_delete)
		, m_dc(m_rate)
	{
		reset();
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "SN76489";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_rate;
	}

	void reset() override
	{
		SNG_reset(m_sng.get());
		m_dc.reset();
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		static_cast<void>(reg);
		if (port == 0)
		{
			SNG_writeIO(m_sng.get(), value & 0xFFu);
		}
		else
		{
			SNG_writeGGIO(m_sng.get(), value & 0xFFu);
		}
	}

	void generate(float* out, std::uint32_t frames) override
	{
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			std::int32_t lr[2];
			SNG_calc_stereo(m_sng.get(), lr);
			out[i * 2] = m_dc.process(0, static_cast<float>(lr[0]) / 32768.0f * OUTPUT_GAIN);
			out[i * 2 + 1] = m_dc.process(1, static_cast<float>(lr[1]) / 32768.0f * OUTPUT_GAIN);
		}
	}

	/// @brief 内部状態を保存する。emu76489 の構造体はポインターを持たないので、そのままの複製で足りる
	[[nodiscard]] std::vector<std::uint8_t> saveState() override
	{
		std::vector<std::uint8_t> buffer(sizeof(SNG) + sizeof(DcBlocker));
		std::memcpy(buffer.data(), m_sng.get(), sizeof(SNG));
		std::memcpy(buffer.data() + sizeof(SNG), &m_dc, sizeof(DcBlocker));
		return buffer;
	}

	bool loadState(const std::vector<std::uint8_t>& saved) override
	{
		if (saved.size() != sizeof(SNG) + sizeof(DcBlocker))
		{
			return false;
		}
		std::memcpy(m_sng.get(), saved.data(), sizeof(SNG));
		std::memcpy(&m_dc, saved.data() + sizeof(SNG), sizeof(DcBlocker));
		return true;
	}

private:
	std::uint32_t m_rate;
	std::unique_ptr<SNG, void (*)(SNG*)> m_sng;
	DcBlocker m_dc;
};

} // namespace mitiru_mml
