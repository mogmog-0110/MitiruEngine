#pragma once
/// @file VgmMultiPlayer.hpp
/// @brief 複数のチップを持つ VGM を、チップごとの Device で鳴らすプレイヤー (float ステレオ、44100 Hz)
/// @details 対応するチップ: SN76489 (Game Gear のステレオ指定を含む)、YM2151、YM2608、YM3812、YM3526、
///          YMF262、AY-3-8910、NES APU。ヘッダーのクロックの下位 30bit を周波数、bit30 を 2 個目のチップの
///          有無として読み、2 個目は 0xAn 系のコマンドや、レジスタ番号の bit7 で区別する。
///          チップごとにネイティブレートで進め、書き込みの時刻 (44100 Hz のサンプル数) までを生成してから
///          書くので、チップ間の時間の関係は VGM のとおりになる。
///          対応していないチップのコマンドは、引数の長さを数えて読み飛ばす。Loop Base、Loop Modifier、
///          DAC ストリーム、YM2612 の DAC は扱わない。
///          従来の VgmPlayer (YM2608 専用、16bit モノラル) は変えていない。

#include <mitiru_mml/Ay8910Device.hpp>
#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/NesApuDevice.hpp>
#include <mitiru_mml/OplDevice.hpp>
#include <mitiru_mml/OpmDevice.hpp>
#include <mitiru_mml/Sn76489Device.hpp>
#include <mitiru_mml/StereoBuffer.hpp>
#include <mitiru_mml/VgmPlayer.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief VGM が使うチップの種類
enum class VgmChipType
{
	Sn76489,
	Ym2151,
	Ym2608,
	Ym3812,
	Ym3526,
	Ymf262,
	Ay8910,
	NesApu,
};

/// @brief ヘッダーで見つかったチップ 1 個
struct VgmChipUse
{
	VgmChipType type = VgmChipType::Sn76489;
	int instance = 0;            ///< 0 が 1 個目、1 が 2 個目
	std::uint32_t clock = 0;     ///< 下位 30bit のクロック (Hz)
};

/// @brief YM2608 を VGM のデータブロック (ADPCM の ROM) つきで包む Device
/// @details VgmPlayer と同じ混ぜ方 (SSG は縮めずに左右へ足す) にして、ハードクランプする。
class VgmOpnaDevice final : public ChipDevice
{
public:
	explicit VgmOpnaDevice(std::uint32_t clock)
		: m_chip(m_interface)
		, m_rate(m_chip.sample_rate(clock))
	{
		m_chip.reset();
		m_chip.write_address(0x29); // 6 チャンネルモード
		m_chip.write_data(0x9F);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "YM2608";
	}

	[[nodiscard]] std::uint32_t nativeRate() const noexcept override
	{
		return m_rate;
	}

	void reset() override
	{
		m_chip.reset();
	}

	void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) override
	{
		if (port == 0)
		{
			m_chip.write_address(static_cast<std::uint8_t>(reg));
			m_chip.write_data(static_cast<std::uint8_t>(value));
		}
		else
		{
			m_chip.write_address_hi(static_cast<std::uint8_t>(reg));
			m_chip.write_data_hi(static_cast<std::uint8_t>(value));
		}
	}

	void generate(float* out, std::uint32_t frames) override
	{
		std::array<ymfm::ym2608::output_data, 512> raw;
		while (frames > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(frames, 512);
			m_chip.generate(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				const std::int32_t left = std::clamp(raw[i].data[0] + raw[i].data[2], -32768, 32767);
				const std::int32_t right = std::clamp(raw[i].data[1] + raw[i].data[2], -32768, 32767);
				out[i * 2] = static_cast<float>(left) / 32768.0f;
				out[i * 2 + 1] = static_cast<float>(right) / 32768.0f;
			}
			out += static_cast<std::size_t>(n) * 2;
			frames -= n;
		}
	}

	/// @brief ADPCM-A (type 0x82) と ADPCM-B (type 0x81) の ROM を置く
	void setRom(bool adpcmB, std::uint32_t startAddress, const std::uint8_t* data, std::size_t size)
	{
		auto& rom = adpcmB ? m_interface.m_adpcmB : m_interface.m_adpcmA;
		const std::size_t end = static_cast<std::size_t>(startAddress) + size;
		if (rom.size() < end)
		{
			rom.resize(end, 0);
		}
		std::memcpy(rom.data() + startAddress, data, size);
	}

private:
	VgmOpnaInterface m_interface;
	ymfm::ym2608 m_chip;
	std::uint32_t m_rate;
};

/// @brief 複数チップの VGM プレイヤー
class VgmMultiPlayer
{
public:
	/// @brief 出力のサンプルレート (VGM の時間の単位と同じ)
	static constexpr std::uint32_t OUTPUT_RATE = 44100;
	/// @brief 描く長さの上限 (秒)。ループが終わらない VGM で止まらなくなるのを防ぐ
	static constexpr float MAX_RENDER_SECONDS = 300.0f;
	/// @brief これより小さいクロックのチップは無いものとして扱う (Device のレートが 0 になるのを防ぐ)
	static constexpr std::uint32_t MIN_CLOCK = 100000;

	/// @brief 描画の設定
	struct Options
	{
		float masterGain = 0.8f; ///< チップを足し合わせた後にかける係数
		bool softClip = true;    ///< 足し合わせた後に三次のソフトクリップをかける
	};

	/// @brief メモリから読む (VGM。gzip 圧縮の VGZ も受ける)
	bool loadMemory(const std::uint8_t* data, std::size_t size)
	{
		if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B)
		{
			std::vector<std::uint8_t> compressed(data, data + size);
			std::vector<std::uint8_t> plain;
			if (!decompressGzip(compressed, plain))
			{
				return false;
			}
			m_data = std::move(plain);
		}
		else
		{
			m_data.assign(data, data + size);
		}
		return parseHeader();
	}

	/// @brief ファイルから読む
	bool loadFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs)
		{
			return false;
		}
		const auto size = ifs.tellg();
		if (size < 0x40)
		{
			return false;
		}
		ifs.seekg(0);
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
		ifs.read(reinterpret_cast<char*>(bytes.data()), size);
		return loadMemory(bytes.data(), bytes.size());
	}

	/// @brief ヘッダーで見つけたチップ
	[[nodiscard]] const std::vector<VgmChipUse>& chips() const noexcept
	{
		return m_chips;
	}

	/// @brief ヘッダーの総サンプル数 (44100 Hz)
	[[nodiscard]] std::uint32_t totalSamples() const noexcept
	{
		return m_totalSamples;
	}

	/// @brief 全体を描いて float ステレオ (44100 Hz) で返す
	/// @param loopCount 曲を何回流すか。2 以上はループ位置があるときだけ意味がある
	/// @param maxSeconds 描く長さの上限 (秒)。0 以下なら MAX_RENDER_SECONDS
	[[nodiscard]] StereoBuffer render(int loopCount = 1, float maxSeconds = 0.0f, const Options& options = Options{}) const
	{
		StereoBuffer result;
		result.sampleRate = OUTPUT_RATE;
		if (m_dataOffset == 0 || m_chips.empty())
		{
			return result;
		}
		if (maxSeconds <= 0.0f)
		{
			maxSeconds = MAX_RENDER_SECONDS;
		}
		const auto maxSamples = static_cast<std::uint64_t>(maxSeconds * static_cast<float>(OUTPUT_RATE));

		std::vector<Write> writes;
		std::vector<Rom> roms;
		const std::uint64_t total = scan(std::max(1, loopCount), maxSamples, writes, roms);

		std::vector<Stream> streams;
		streams.reserve(m_chips.size());
		for (const auto& use : m_chips)
		{
			streams.push_back(makeStream(use, roms));
		}

		for (const auto& w : writes)
		{
			Stream& s = streams[w.slot];
			advance(s, w.time);
			s.device->write(w.port, w.reg, w.value);
		}
		for (auto& s : streams)
		{
			// 区間の端数で出力が 1 サンプル足りなくなるので、1 サンプル多く生成してから長さをそろえる
			advance(s, total + 1);
			s.out.resize(static_cast<std::size_t>(total) * 2, 0.0f);
		}

		for (const auto& s : streams)
		{
			result.growTo(s.out.size() / 2);
			for (std::size_t i = 0; i < s.out.size(); ++i)
			{
				result.samples[i] += s.out[i];
			}
		}
		for (float& v : result.samples)
		{
			float x = v * options.masterGain;
			if (options.softClip)
			{
				x = std::clamp(x, -1.0f, 1.0f);
				x = 1.5f * x - 0.5f * x * x * x;
			}
			v = x;
		}
		return result;
	}

private:
	/// @brief チップへの書き込み 1 件
	struct Write
	{
		std::uint64_t time;  ///< 44100 Hz のサンプル数
		std::uint32_t slot;  ///< m_chips の番号
		std::uint32_t port;
		std::uint32_t reg;
		std::uint32_t value;
	};

	/// @brief データブロックで送られた ROM (YM2608 の ADPCM)
	struct Rom
	{
		std::uint32_t slot;
		bool adpcmB;
		std::uint32_t start;
		std::size_t offset; ///< m_data 内の位置
		std::size_t size;
	};

	/// @brief チップ 1 個ぶんの描画の状態
	struct Stream
	{
		std::unique_ptr<ChipDevice> device;
		std::uint32_t native = 0;
		std::uint64_t position = 0; ///< これまでに生成したネイティブのフレーム数
		BoxDecimatorStereo decimator{1, 1};
		std::vector<float> out;
		std::vector<float> block;
	};

	/// @brief 44100 Hz の時刻 time まで、ネイティブのフレームを生成する
	static void advance(Stream& s, std::uint64_t time)
	{
		const std::uint64_t target = time * s.native / OUTPUT_RATE;
		while (s.position < target)
		{
			const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(target - s.position, 4096));
			s.block.resize(static_cast<std::size_t>(n) * 2);
			s.device->generate(s.block.data(), n);
			s.decimator.push(s.block.data(), n, s.out);
			s.position += n;
		}
	}

	[[nodiscard]] Stream makeStream(const VgmChipUse& use, const std::vector<Rom>& roms) const
	{
		Stream s;
		switch (use.type)
		{
		case VgmChipType::Sn76489: s.device = std::make_unique<Sn76489Device>(use.clock); break;
		case VgmChipType::Ym2151: s.device = std::make_unique<OpmDevice>(use.clock); break;
		case VgmChipType::Ym3812: s.device = std::make_unique<OplDevice>(OplVariant::Opl2, use.clock); break;
		case VgmChipType::Ym3526: s.device = std::make_unique<OplDevice>(OplVariant::Opl, use.clock); break;
		case VgmChipType::Ymf262: s.device = std::make_unique<OplDevice>(OplVariant::Opl3, use.clock); break;
		case VgmChipType::Ay8910: s.device = std::make_unique<Ay8910Device>(use.clock); break;
		case VgmChipType::NesApu: s.device = std::make_unique<NesApuDevice>(use.clock); break;
		case VgmChipType::Ym2608:
		{
			auto opna = std::make_unique<VgmOpnaDevice>(use.clock);
			const auto slot = static_cast<std::uint32_t>(&use - m_chips.data());
			for (const auto& rom : roms)
			{
				if (rom.slot == slot)
				{
					opna->setRom(rom.adpcmB, rom.start, m_data.data() + rom.offset, rom.size);
				}
			}
			s.device = std::move(opna);
			break;
		}
		}
		s.native = s.device->nativeRate();
		s.decimator = BoxDecimatorStereo(s.native, OUTPUT_RATE);
		return s;
	}

	[[nodiscard]] std::uint32_t readUint32(std::size_t offset) const noexcept
	{
		if (offset + 3 >= m_data.size())
		{
			return 0;
		}
		return static_cast<std::uint32_t>(m_data[offset])
			| (static_cast<std::uint32_t>(m_data[offset + 1]) << 8)
			| (static_cast<std::uint32_t>(m_data[offset + 2]) << 16)
			| (static_cast<std::uint32_t>(m_data[offset + 3]) << 24);
	}

	bool parseHeader()
	{
		m_chips.clear();
		m_dataOffset = 0;
		if (m_data.size() < 0x40 || std::memcmp(m_data.data(), "Vgm ", 4) != 0)
		{
			return false;
		}
		m_totalSamples = readUint32(0x18);
		const std::uint32_t loopOff = readUint32(0x1C);
		m_loopOffset = (loopOff > 0) ? (0x1C + loopOff) : 0;
		const std::uint32_t version = readUint32(0x08);
		const std::uint32_t dataOff = readUint32(0x34);
		m_dataOffset = (version < 0x150 || dataOff == 0) ? 0x40 : (0x34 + dataOff);
		if (m_dataOffset >= m_data.size())
		{
			m_dataOffset = 0;
			return false;
		}

		struct HeaderField
		{
			VgmChipType type;
			std::size_t offset;
		};
		static constexpr HeaderField FIELDS[] = {
			{VgmChipType::Sn76489, 0x0C}, {VgmChipType::Ym2151, 0x30}, {VgmChipType::Ym2608, 0x48},
			{VgmChipType::Ym3812, 0x50}, {VgmChipType::Ym3526, 0x54}, {VgmChipType::Ymf262, 0x5C},
			{VgmChipType::Ay8910, 0x74}, {VgmChipType::NesApu, 0x84},
		};
		for (const auto& field : FIELDS)
		{
			if (field.offset + 4 > m_dataOffset)
			{
				continue; // このバージョンのヘッダーには無い項目
			}
			const std::uint32_t raw = readUint32(field.offset);
			const std::uint32_t clock = raw & 0x3FFFFFFFu;
			// 実在するチップは数十 kHz 以上で動く。極端に小さい値はヘッダーの破損として読み飛ばす
			if (clock < MIN_CLOCK)
			{
				continue;
			}
			m_chips.push_back({field.type, 0, clock});
			if ((raw & 0x40000000u) != 0)
			{
				m_chips.push_back({field.type, 1, clock});
			}
		}
		return !m_chips.empty();
	}

	/// @brief (種類, 個目) のチップの番号。無ければ -1
	[[nodiscard]] int slotOf(VgmChipType type, int instance) const noexcept
	{
		for (std::size_t i = 0; i < m_chips.size(); ++i)
		{
			if (m_chips[i].type == type && m_chips[i].instance == instance)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	/// @brief コマンドを読み進めて、書き込みの一覧と総サンプル数を作る
	/// @return 曲の長さ (44100 Hz のサンプル数)
	[[nodiscard]] std::uint64_t scan(int loopCount, std::uint64_t maxSamples, std::vector<Write>& writes,
		std::vector<Rom>& roms) const
	{
		std::uint64_t time = 0;
		std::size_t pos = m_dataOffset;
		int loopsRemaining = loopCount - 1;
		std::size_t guard = 0;
		constexpr std::size_t MAX_COMMANDS = 50000000;

		const auto emit = [&](VgmChipType type, int instance, std::uint32_t port, std::uint32_t reg, std::uint32_t value)
		{
			const int slot = slotOf(type, instance);
			if (slot >= 0)
			{
				writes.push_back({time, static_cast<std::uint32_t>(slot), port, reg, value});
			}
		};

		while (pos < m_data.size() && time < maxSamples && guard++ < MAX_COMMANDS)
		{
			const std::uint8_t cmd = m_data[pos];
			const auto arg = [&](std::size_t i) -> std::uint32_t
			{
				return (pos + i < m_data.size()) ? m_data[pos + i] : 0u;
			};

			if (cmd == 0x50) { emit(VgmChipType::Sn76489, 0, 0, 0, arg(1)); pos += 2; }
			else if (cmd == 0x30) { emit(VgmChipType::Sn76489, 1, 0, 0, arg(1)); pos += 2; }
			else if (cmd == 0x4F) { emit(VgmChipType::Sn76489, 0, 1, 0, arg(1)); pos += 2; }
			else if (cmd == 0x3F) { emit(VgmChipType::Sn76489, 1, 1, 0, arg(1)); pos += 2; }
			else if (cmd == 0x54) { emit(VgmChipType::Ym2151, 0, 0, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0xA4) { emit(VgmChipType::Ym2151, 1, 0, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0x56 || cmd == 0x57) { emit(VgmChipType::Ym2608, 0, cmd - 0x56u, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0xA6 || cmd == 0xA7) { emit(VgmChipType::Ym2608, 1, cmd - 0xA6u, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0x5A) { emit(VgmChipType::Ym3812, 0, 0, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0xAA) { emit(VgmChipType::Ym3812, 1, 0, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0x5B) { emit(VgmChipType::Ym3526, 0, 0, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0xAB) { emit(VgmChipType::Ym3526, 1, 0, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0x5E || cmd == 0x5F) { emit(VgmChipType::Ymf262, 0, cmd - 0x5Eu, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0xAE || cmd == 0xAF) { emit(VgmChipType::Ymf262, 1, cmd - 0xAEu, arg(1), arg(2)); pos += 3; }
			else if (cmd == 0xA0) { emit(VgmChipType::Ay8910, static_cast<int>(arg(1) >> 7), 0, arg(1) & 0x7Fu, arg(2)); pos += 3; }
			else if (cmd == 0xB4) { emit(VgmChipType::NesApu, static_cast<int>(arg(1) >> 7), 0, arg(1) & 0x7Fu, arg(2)); pos += 3; }
			else if (cmd == 0x61) { time += arg(1) | (arg(2) << 8); pos += 3; }
			else if (cmd == 0x62) { time += 735; pos += 1; }
			else if (cmd == 0x63) { time += 882; pos += 1; }
			else if (cmd >= 0x70 && cmd <= 0x7F) { time += (cmd & 0x0Fu) + 1u; pos += 1; }
			else if (cmd >= 0x80 && cmd <= 0x8F) { time += cmd & 0x0Fu; pos += 1; } // YM2612 の DAC 書き込みは鳴らさず、待ちだけ数える
			else if (cmd == 0x66)
			{
				if (m_loopOffset > 0 && loopsRemaining > 0)
				{
					pos = m_loopOffset;
					--loopsRemaining;
				}
				else
				{
					break;
				}
			}
			else if (cmd == 0x67)
			{
				// 0x67 0x66 type size(4) data...
				if (pos + 7 > m_data.size())
				{
					break;
				}
				const std::uint32_t type = arg(2);
				const std::uint32_t size = readUint32(pos + 3);
				const std::size_t dataPos = pos + 7;
				if ((type == 0x81 || type == 0x82) && size > 8 && dataPos + size <= m_data.size())
				{
					const int slot = slotOf(VgmChipType::Ym2608, 0);
					if (slot >= 0)
					{
						roms.push_back({static_cast<std::uint32_t>(slot), type == 0x81, readUint32(dataPos + 4),
							dataPos + 8, size - 8});
					}
				}
				pos = dataPos + size;
			}
			else if (cmd == 0x68) { pos += 12; }
			else if (cmd == 0x90 || cmd == 0x91 || cmd == 0x95) { pos += 5; }
			else if (cmd == 0x92) { pos += 6; }
			else if (cmd == 0x93) { pos += 11; }
			else if (cmd == 0x94) { pos += 2; }
			else if ((cmd >= 0x30 && cmd <= 0x3F) || cmd == 0x4F || cmd == 0x50) { pos += 2; }
			else if ((cmd >= 0x40 && cmd <= 0x4E) || (cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) { pos += 3; }
			else if (cmd >= 0xC0 && cmd <= 0xDF) { pos += 4; }
			else if (cmd >= 0xE0) { pos += 5; }
			else { pos += 1; }
		}
		return std::min(time, maxSamples);
	}

	std::vector<std::uint8_t> m_data;
	std::size_t m_dataOffset = 0;
	std::size_t m_loopOffset = 0;
	std::uint32_t m_totalSamples = 0;
	std::vector<VgmChipUse> m_chips;
};

} // namespace mitiru_mml
