#pragma once
/// @file Ay8910Device.hpp
/// @brief AY-3-8910 / YM2149 (PSG) の Device (層 1): ymfm の ym2149 (BSD-3-Clause) を包む
/// @details 矩形波 3 声とノイズ 1 本、ハードウェアエンベロープ 1 本。音符や音量の知識は Ay8910Driver が持つ。
///          3 声の出力は声ごとの定位 (setChannelPan) で左右に振って混ぜる。

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/DcBlocker.hpp>

#include <ymfm_misc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace mitiru_mml
{

/// @brief AY-3-8910 / YM2149 のエミュレーター
/// @details レジスタ空間は 1 つだけなので、write() の port は無視する。
class Ay8910Device final : public ChipDevice
{
public:
	/// @brief MSX などで使われた標準のクロック (1.789773 MHz)
	static constexpr std::uint32_t CLOCK = 1789773;
	/// @brief ネイティブレート。ymfm の SSG は クロック / 8 で 1 サンプルずつ進む
	/// @details ymfm の ym2149::sample_rate() は クロック / 64 を返すが、generate() は クロック / 8 の刻みで
	///          進む。声の周期の単位が クロック / 16 で、矩形波は周期の半分ごとに反転するため。
	static constexpr std::uint32_t NATIVE_RATE = CLOCK / 8;
	/// @brief 1 声が最大の音量で鳴るときの振幅が 0.25 になる係数
	static constexpr float OUTPUT_GAIN = 0.5f;

	/// @param clock チップのクロック (Hz)。VGM の再生などで標準と違う値を使うときに指定する
	explicit Ay8910Device(std::uint32_t clock = CLOCK)
		: m_rate(clock / 8)
		, m_interface(std::make_unique<ymfm::ymfm_interface>())
		, m_chip(std::make_unique<ymfm::ym2149>(*m_interface))
		, m_dc(m_rate)
	{
		reset();
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "AY-3-8910";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_rate;
	}

	void reset() override
	{
		m_chip->reset();
		m_dc.reset();
		m_pan.fill(1);
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		static_cast<void>(port);
		m_chip->write_address(static_cast<std::uint8_t>(reg));
		m_chip->write_data(static_cast<std::uint8_t>(value));
	}

	/// @brief 声 channel (0-2) の定位 (0 = 左、1 = 中央、2 = 右)。中央は左右の両方に同じ大きさで出す
	void setChannelPan(int channel, int pan) noexcept
	{
		if (channel >= 0 && channel < 3)
		{
			m_pan[static_cast<std::size_t>(channel)] = std::clamp(pan, 0, 2);
		}
	}

	void generate(float* out, std::uint32_t frames) override
	{
		std::array<ymfm::ym2149::output_data, BLOCK> raw;
		while (frames > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(frames, BLOCK);
			m_chip->generate(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				float left = 0.0f;
				float right = 0.0f;
				for (std::size_t c = 0; c < 3; ++c)
				{
					const float v = static_cast<float>(raw[i].data[c]) / 32768.0f * OUTPUT_GAIN;
					if (m_pan[c] != 2)
					{
						left += v;
					}
					if (m_pan[c] != 0)
					{
						right += v;
					}
				}
				out[i * 2] = m_dc.process(0, left);
				out[i * 2 + 1] = m_dc.process(1, right);
			}
			out += static_cast<std::size_t>(n) * 2;
			frames -= n;
		}
	}

	/// @brief 保存の形式: チップの状態のバイト数 (4 バイト)、チップの状態、直流除去、定位
	[[nodiscard]] std::vector<std::uint8_t> saveState() override
	{
		std::vector<std::uint8_t> chip;
		ymfm::ymfm_saved_state state(chip, true);
		m_chip->save_restore(state);
		const auto chipSize = static_cast<std::uint32_t>(chip.size());
		std::vector<std::uint8_t> buffer(sizeof(chipSize) + chip.size() + sizeof(DcBlocker) + sizeof(m_pan));
		std::uint8_t* p = buffer.data();
		std::memcpy(p, &chipSize, sizeof(chipSize));
		p += sizeof(chipSize);
		std::memcpy(p, chip.data(), chip.size());
		p += chip.size();
		std::memcpy(p, &m_dc, sizeof(DcBlocker));
		p += sizeof(DcBlocker);
		std::memcpy(p, m_pan.data(), sizeof(m_pan));
		return buffer;
	}

	bool loadState(const std::vector<std::uint8_t>& saved) override
	{
		std::uint32_t chipSize = 0;
		if (saved.size() < sizeof(chipSize))
		{
			return false;
		}
		std::memcpy(&chipSize, saved.data(), sizeof(chipSize));
		if (saved.size() != sizeof(chipSize) + chipSize + sizeof(DcBlocker) + sizeof(m_pan))
		{
			return false;
		}
		const std::uint8_t* p = saved.data() + sizeof(chipSize);
		std::vector<std::uint8_t> chip(p, p + chipSize);
		ymfm::ymfm_saved_state state(chip, false);
		m_chip->save_restore(state);
		p += chipSize;
		std::memcpy(&m_dc, p, sizeof(DcBlocker));
		p += sizeof(DcBlocker);
		std::memcpy(m_pan.data(), p, sizeof(m_pan));
		return true;
	}

private:
	static constexpr std::uint32_t BLOCK = 512;

	std::uint32_t m_rate;
	std::unique_ptr<ymfm::ymfm_interface> m_interface; ///< チップが参照するので、Device を移動してもアドレスが変わらないように別に確保する
	std::unique_ptr<ymfm::ym2149> m_chip;
	DcBlocker m_dc;
	std::array<int, 3> m_pan{};
};

} // namespace mitiru_mml
