#pragma once
/// @file NesApuDevice.hpp
/// @brief NES (Famicom) の APU の Device (層 1): vgsound_emu (zlib License) の nes_apu_core を包む
/// @details 矩形波 2 声、三角波、ノイズ。DPCM は、サンプルを読む口 (nes_apu_intf の do_dmc_dma) を
///          実装していないので鳴らない。write() の reg はレジスタの下位 5 ビット (0x4000 を 0 とする) で、
///          port は無視する。
///          vgsound_emu には状態を保存する仕組みが無いので、saveState() は空を返し loadState() は false を返す。

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/DcBlocker.hpp>

#include <vgsound_emu/src/nes/nes.hpp>

#include <cstdint>
#include <memory>

namespace mitiru_mml
{

/// @brief NES APU のエミュレーター
class NesApuDevice final : public ChipDevice
{
public:
	/// @brief NTSC の CPU クロック (1.789773 MHz)。APU は CPU の 1 サイクルごとに 1 回進む
	static constexpr std::uint32_t CLOCK = 1789773;
	static constexpr std::uint32_t NATIVE_RATE = CLOCK;
	/// @brief 出力にかける係数
	static constexpr float OUTPUT_GAIN = 1.5f;

	/// @param clock CPU のクロック (Hz)。VGM の再生などで標準と違う値を使うときに指定する
	explicit NesApuDevice(std::uint32_t clock = CLOCK)
		: m_rate(clock)
		, m_interface(std::make_unique<vgsound_emu::nes_apu_intf>())
		, m_core(std::make_unique<vgsound_emu::nes_apu_core>(*m_interface, false, false, false))
		, m_dc(m_rate)
	{
		reset();
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "NES APU";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_rate;
	}

	void reset() override
	{
		m_core->reset();
		m_dc.reset();
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		static_cast<void>(port);
		m_core->write(static_cast<std::uint8_t>(reg & 0x1Fu), static_cast<std::uint8_t>(value));
	}

	void generate(float* out, std::uint32_t frames) override
	{
		for (std::uint32_t i = 0; i < frames; ++i)
		{
			m_core->tick();
			const float s = static_cast<float>(m_core->out()) / 32768.0f * OUTPUT_GAIN;
			out[i * 2] = m_dc.process(0, s);
			out[i * 2 + 1] = m_dc.process(1, s);
		}
	}

private:
	std::uint32_t m_rate;
	std::unique_ptr<vgsound_emu::nes_apu_intf> m_interface; ///< コアが参照するので、Device を移動してもアドレスが変わらないように別に確保する
	std::unique_ptr<vgsound_emu::nes_apu_core> m_core;
	DcBlocker m_dc;
};

} // namespace mitiru_mml
