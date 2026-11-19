#pragma once
/// @file OplDevice.hpp
/// @brief YM3526 (OPL)、YM3812 (OPL2)、YMF262 (OPL3) の Device (層 1): ymfm を包む
/// @details 3 つのチップはレジスタの配置がほぼ同じなので、1 つのクラスで包んで種類を実行時に選ぶ。
///          音符や音色など音楽の知識は OplDriver が持つ。

#include <mitiru_mml/ChipDevice.hpp>

#include <ymfm_opl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mitiru_mml
{

/// @brief OPL 系のチップの種類
enum class OplVariant
{
	Opl,  ///< YM3526。9ch、正弦波だけ
	Opl2, ///< YM3812。9ch、波形 4 種
	Opl3, ///< YMF262。2 オペレーターで 18ch、波形 8 種、ステレオ
};

/// @brief OPL 系のエミュレーター
class OplDevice final : public ChipDevice
{
public:
	/// @brief OPL と OPL2 の標準のクロック (3.579545 MHz)
	static constexpr std::uint32_t CLOCK_OPL2 = 3579545;
	/// @brief OPL3 の標準のクロック (14.31818 MHz)
	static constexpr std::uint32_t CLOCK_OPL3 = 14318180;

	/// @param clock チップのクロック (Hz)。0 なら標準の値 (OPL と OPL2 は 3.579545 MHz、OPL3 は 14.31818 MHz)
	explicit OplDevice(OplVariant variant, std::uint32_t clock = 0)
		: m_variant(variant)
		, m_clock(clock != 0 ? clock : (variant == OplVariant::Opl3 ? CLOCK_OPL3 : CLOCK_OPL2))
		, m_interface(std::make_unique<ymfm::ymfm_interface>())
	{
		reset();
	}

	[[nodiscard]] OplVariant variant() const noexcept
	{
		return m_variant;
	}

	/// @brief このチップのクロック (Hz)
	[[nodiscard]] std::uint32_t clock() const noexcept
	{
		return m_clock;
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		switch (m_variant)
		{
		case OplVariant::Opl: return "YM3526";
		case OplVariant::Opl2: return "YM3812";
		case OplVariant::Opl3: return "YMF262";
		}
		return "OPL";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_sampleRate;
	}

	void reset() override
	{
		// ymfm の reset() はエンベロープや LFO のカウンターなどを戻さないので、作り直して電源投入直後に戻す
		m_opl.reset();
		m_opl2.reset();
		m_opl3.reset();
		m_interface = std::make_unique<ymfm::ymfm_interface>();
		switch (m_variant)
		{
		case OplVariant::Opl:
			m_opl = std::make_unique<ymfm::ym3526>(*m_interface);
			m_opl->reset();
			m_sampleRate = m_opl->sample_rate(clock());
			break;
		case OplVariant::Opl2:
			m_opl2 = std::make_unique<ymfm::ym3812>(*m_interface);
			m_opl2->reset();
			m_sampleRate = m_opl2->sample_rate(clock());
			break;
		case OplVariant::Opl3:
			m_opl3 = std::make_unique<ymfm::ymf262>(*m_interface);
			m_opl3->reset();
			m_sampleRate = m_opl3->sample_rate(clock());
			break;
		}
	}

	/// @param port 0 が通常のレジスタ、1 が OPL3 の後半のレジスタ (0x100 以降)。ほかのチップは 0 だけ
	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		const auto r = static_cast<std::uint8_t>(reg);
		const auto v = static_cast<std::uint8_t>(value);
		switch (m_variant)
		{
		case OplVariant::Opl: m_opl->write_address(r); m_opl->write_data(v); break;
		case OplVariant::Opl2: m_opl2->write_address(r); m_opl2->write_data(v); break;
		case OplVariant::Opl3:
			if (port == 0)
			{
				m_opl3->write_address(r);
			}
			else
			{
				m_opl3->write_address_hi(r);
			}
			m_opl3->write_data(v);
			break;
		}
	}

	/// @brief OPL と OPL2 は出力が 1 本なので左右に同じ値を入れる。OPL3 は出力 0 が左、出力 1 が右
	void generate(float* out, std::uint32_t frames) override
	{
		switch (m_variant)
		{
		case OplVariant::Opl: generateMono(*m_opl, out, frames); break;
		case OplVariant::Opl2: generateMono(*m_opl2, out, frames); break;
		case OplVariant::Opl3: generateStereo(*m_opl3, out, frames); break;
		}
	}

	[[nodiscard]] std::vector<std::uint8_t> saveState() override
	{
		std::vector<std::uint8_t> buffer;
		ymfm::ymfm_saved_state state(buffer, true);
		saveRestore(state);
		return buffer;
	}

	bool loadState(const std::vector<std::uint8_t>& saved) override
	{
		std::vector<std::uint8_t> buffer = saved;
		ymfm::ymfm_saved_state state(buffer, false);
		saveRestore(state);
		return true;
	}

private:
	static constexpr std::uint32_t BLOCK = 512;

	void saveRestore(ymfm::ymfm_saved_state& state)
	{
		switch (m_variant)
		{
		case OplVariant::Opl: m_opl->save_restore(state); break;
		case OplVariant::Opl2: m_opl2->save_restore(state); break;
		case OplVariant::Opl3: m_opl3->save_restore(state); break;
		}
	}

	template <class Chip>
	static void generateMono(Chip& chip, float* out, std::uint32_t frames)
	{
		std::array<typename Chip::output_data, BLOCK> raw;
		while (frames > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(frames, BLOCK);
			chip.generate(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				const float s = static_cast<float>(raw[i].data[0]) / 32768.0f;
				out[i * 2] = s;
				out[i * 2 + 1] = s;
			}
			out += static_cast<std::size_t>(n) * 2;
			frames -= n;
		}
	}

	static void generateStereo(ymfm::ymf262& chip, float* out, std::uint32_t frames)
	{
		std::array<ymfm::ymf262::output_data, BLOCK> raw;
		while (frames > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(frames, BLOCK);
			chip.generate(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				out[i * 2] = static_cast<float>(raw[i].data[0]) / 32768.0f;
				out[i * 2 + 1] = static_cast<float>(raw[i].data[1]) / 32768.0f;
			}
			out += static_cast<std::size_t>(n) * 2;
			frames -= n;
		}
	}

	OplVariant m_variant;
	std::uint32_t m_clock;
	std::unique_ptr<ymfm::ymfm_interface> m_interface; ///< チップが参照するので、Device を移動してもアドレスが変わらないように別に確保する
	std::unique_ptr<ymfm::ym3526> m_opl;
	std::unique_ptr<ymfm::ym3812> m_opl2;
	std::unique_ptr<ymfm::ymf262> m_opl3;
	std::uint32_t m_sampleRate = 0;
};

} // namespace mitiru_mml
