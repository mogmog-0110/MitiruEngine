#pragma once
/// @file OpmDevice.hpp
/// @brief YM2151 (OPM) の Device (層 1): ymfm を包み、レジスタ書き込みとサンプル生成だけを持つ
/// @details 音符や FM の音色など音楽の知識は OpmDriver が持つ。

#include <mitiru_mml/ChipDevice.hpp>

#include <ymfm_opm.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mitiru_mml
{

/// @brief YM2151 (OPM) のエミュレーター
/// @details レジスタ空間は 1 つだけなので、write() の port は無視する。
class OpmDevice final : public ChipDevice
{
public:
	/// @brief X68000 などで使われた標準のクロック (3.579545 MHz)
	static constexpr std::uint32_t CLOCK = 3579545;

	/// @param clock チップのクロック (Hz)。VGM の再生などで標準と違う値を使うときに指定する
	explicit OpmDevice(std::uint32_t clock = CLOCK)
		: m_interface(std::make_unique<ymfm::ymfm_interface>())
		, m_chip(std::make_unique<ymfm::ym2151>(*m_interface))
		, m_sampleRate(m_chip->sample_rate(clock))
	{
		m_chip->reset();
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "YM2151";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_sampleRate;
	}

	void reset() override
	{
		// ymfm の reset() はエンベロープや LFO のカウンターなどを戻さないので、作り直して電源投入直後に戻す
		m_chip.reset();
		m_interface = std::make_unique<ymfm::ymfm_interface>();
		m_chip = std::make_unique<ymfm::ym2151>(*m_interface);
		m_chip->reset();
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		static_cast<void>(port);
		m_chip->write_address(static_cast<std::uint8_t>(reg));
		m_chip->write_data(static_cast<std::uint8_t>(value));
	}

	void generate(float* out, std::uint32_t frames) override
	{
		std::array<ymfm::ym2151::output_data, BLOCK> raw;
		while (frames > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(frames, BLOCK);
			m_chip->generate(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				out[i * 2] = static_cast<float>(raw[i].data[0]) / 32768.0f;
				out[i * 2 + 1] = static_cast<float>(raw[i].data[1]) / 32768.0f;
			}
			out += static_cast<std::size_t>(n) * 2;
			frames -= n;
		}
	}

	[[nodiscard]] std::vector<std::uint8_t> saveState() override
	{
		std::vector<std::uint8_t> buffer;
		ymfm::ymfm_saved_state state(buffer, true);
		m_chip->save_restore(state);
		return buffer;
	}

	bool loadState(const std::vector<std::uint8_t>& saved) override
	{
		std::vector<std::uint8_t> buffer = saved;
		ymfm::ymfm_saved_state state(buffer, false);
		m_chip->save_restore(state);
		return true;
	}

private:
	static constexpr std::uint32_t BLOCK = 512;

	std::unique_ptr<ymfm::ymfm_interface> m_interface; ///< チップが参照するので、Device を移動してもアドレスが変わらないように別に確保する
	std::unique_ptr<ymfm::ym2151> m_chip;
	std::uint32_t m_sampleRate;
};

} // namespace mitiru_mml
