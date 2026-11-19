#pragma once
/// @file OpnaDevice.hpp
/// @brief YM2608 (OPNA) の Device (層 1): ymfm を包み、レジスタ書き込みとサンプル生成だけを持つ
/// @details 音符や FM の音色など音楽の知識は OpnaDriver が持つ。

#include <mitiru_mml/ChipDevice.hpp>

#include <ymfm_opn.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mitiru_mml
{

/// @brief ymfm のインターフェース実装 (OPNA 用)
/// @details ADPCM-A/B のリズムサンプルは ROM が無いので、無音の中心値 (0x80) を返す。
class OpnaInterface : public ymfm::ymfm_interface
{
public:
	/// @brief 外部メモリ読み出しコールバック
	std::uint8_t ymfm_external_read(ymfm::access_class type, std::uint32_t address) override
	{
		static_cast<void>(address);
		if (type == ymfm::ACCESS_ADPCM_A || type == ymfm::ACCESS_ADPCM_B)
		{
			return 0x80;
		}
		return 0;
	}

	/// @brief 外部メモリ書き込みコールバック (何もしない)
	void ymfm_external_write(ymfm::access_class type, std::uint32_t address, std::uint8_t data) override
	{
		static_cast<void>(type);
		static_cast<void>(address);
		static_cast<void>(data);
	}
};

/// @brief YM2608 (OPNA) のエミュレーター
class OpnaDevice final : public ChipDevice
{
public:
	/// @brief PC-98 標準の OPNA クロック (7.9872 MHz)
	static constexpr std::uint32_t CLOCK = 7987200;

	/// @brief ymfm が 1 回の generate() で返す 3 要素 (FM 左、FM 右、SSG)
	using RawFrame = ymfm::ym2608::output_data;

	/// @brief ネイティブレートの選び方。下げても ymfm の処理量はほとんど減らず、SSG の出力が変わる
	enum class Fidelity
	{
		Max,     ///< clock/8 (約 998 kHz)。FM は 18 回、SSG は 4 回ずつ同じ値を繰り返す
		Medium,  ///< clock/24 (約 333 kHz)
		Minimum, ///< clock/48 (約 166 kHz)
	};

	OpnaDevice()
		: m_interface(std::make_unique<OpnaInterface>())
		, m_chip(std::make_unique<ymfm::ym2608>(*m_interface))
	{
		m_chip->reset();
		m_sampleRate = m_chip->sample_rate(CLOCK);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "YM2608";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_sampleRate;
	}

	void reset() override
	{
		// ymfm の reset() はエンベロープや LFO のカウンターなどを戻さないので、作り直して電源投入直後に戻す。
		// 戻さないと、同じ曲を 2 回目に鳴らしたとき (ストリームの頭出しなど) 1 回目と違う PCM になる
		m_chip.reset();
		m_interface = std::make_unique<OpnaInterface>();
		m_chip = std::make_unique<ymfm::ym2608>(*m_interface);
		m_chip->reset();
		if (m_fidelity != Fidelity::Max)
		{
			applyFidelity();
		}
	}

	/// @brief ネイティブレートを切り替える。音を出す前 (リセット直後) に呼ぶこと
	void setFidelity(Fidelity fidelity)
	{
		m_fidelity = fidelity;
		applyFidelity();
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		if (port == 0)
		{
			m_chip->write_address(static_cast<std::uint8_t>(reg));
			m_chip->write_data(static_cast<std::uint8_t>(value));
		}
		else
		{
			m_chip->write_address_hi(static_cast<std::uint8_t>(reg));
			m_chip->write_data_hi(static_cast<std::uint8_t>(value));
		}
	}

	/// @brief float ステレオで生成する。FM は左右そのまま、SSG は SSG_GAIN 倍して両方に足す
	void generate(float* out, std::uint32_t frames) override
	{
		std::array<RawFrame, BLOCK> raw;
		while (frames > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(frames, BLOCK);
			m_chip->generate(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				const float ssg = static_cast<float>(raw[i].data[2]) / 32768.0f * SSG_GAIN;
				out[i * 2] = static_cast<float>(raw[i].data[0]) / 32768.0f + ssg;
				out[i * 2 + 1] = static_cast<float>(raw[i].data[1]) / 32768.0f + ssg;
			}
			out += static_cast<std::size_t>(n) * 2;
			frames -= n;
		}
	}

	/// @brief ymfm の出力 3 要素をそのまま返す (従来のモノラル出力の再現と、速度測定のため)
	void generateRaw(RawFrame* out, std::uint32_t frames)
	{
		m_chip->generate(out, frames);
	}

	/// @brief 従来の OpnaDriver::renderSamples と同じ混ぜ方で 16bit モノラル 1 サンプルにする
	/// @details FM 左右の平均に SSG * SSG_GAIN を足し、0.8 倍して三次のソフトクリップをかける。
	[[nodiscard]] static std::int16_t legacyMonoSample(const RawFrame& frame) noexcept
	{
		const float fmF = static_cast<float>(frame.data[0] + frame.data[1]) / 65536.0f;
		const float ssgF = static_cast<float>(frame.data[2]) / 32768.0f * SSG_GAIN;
		const float mixed = softClip((fmF + ssgF) * 0.8f);
		return static_cast<std::int16_t>(mixed * 32767.0f);
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
	/// @brief SSG を FM に混ぜるときの係数 (SSG は FM より大きく鳴るので下げる)
	static constexpr float SSG_GAIN = 0.3f;
	/// @brief generate() が一度に ymfm へ頼む最大フレーム数
	static constexpr std::uint32_t BLOCK = 512;

	static float softClip(float x) noexcept
	{
		if (x > 1.0f) return 1.0f;
		if (x < -1.0f) return -1.0f;
		return 1.5f * x - 0.5f * x * x * x;
	}

	void applyFidelity()
	{
		switch (m_fidelity)
		{
		case Fidelity::Max: m_chip->set_fidelity(ymfm::OPN_FIDELITY_MAX); break;
		case Fidelity::Medium: m_chip->set_fidelity(ymfm::OPN_FIDELITY_MED); break;
		case Fidelity::Minimum: m_chip->set_fidelity(ymfm::OPN_FIDELITY_MIN); break;
		}
		m_sampleRate = m_chip->sample_rate(CLOCK);
	}

	std::unique_ptr<OpnaInterface> m_interface; ///< チップが参照するので、Device を移動してもアドレスが変わらないように別に確保する
	std::unique_ptr<ymfm::ym2608> m_chip;
	Fidelity m_fidelity = Fidelity::Max;
	std::uint32_t m_sampleRate = 0;
};

} // namespace mitiru_mml
