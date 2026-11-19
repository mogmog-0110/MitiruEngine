#pragma once
/// @file OpnaDriver.hpp
/// @brief YM2608 (OPNA) の Driver (層 2): 音符や音色をレジスタ書き込みに変える
/// @details FM 6ch・SSG 3ch・リズム音源をレジスタレベルで制御する。レジスタの読み書きとサンプル生成は
///          OpnaDevice (層 1) が持つ。ChipDriver を実装しているので、複数チップのシーケンサーから
///          他のチップと同じ形で使える。従来の fmNoteOn や setFmVoice などの呼び出しも残してある。
///
/// @code
/// mitiru_mml::OpnaDriver driver;
/// driver.setFmVoice(0, mitiru_mml::opna_presets::PIANO);
/// driver.fmNoteOn(0, 60);  // C4
/// auto pcm = driver.renderSamples(22050);  // ネイティブレートで 22050 サンプル
/// @endcode

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/OpnaDevice.hpp>
#include <mitiru_mml/OpnaPresets.hpp>
#include <mitiru_mml/OpnaVoice.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief YM2608 (OPNA) ドライバー
/// @details チャンネル番号 (ChipDriver 側): FM は 0〜5、SSG は 6〜8、FM で合成するドラムは 9〜14
///          (BD、SD、HH、タム、シンバル、リムショットの順)。
class OpnaDriver final : public ChipDriver
{
public:
	/// @brief PC-98 標準 OPNA クロック (7.9872MHz)
	static constexpr std::uint32_t CLOCK = OpnaDevice::CLOCK;
	/// @brief FM 音源チャンネル数
	static constexpr int FM_CHANNELS = 6;
	/// @brief SSG 音源チャンネル数
	static constexpr int SSG_CHANNELS = 3;
	/// @brief ドラムのチャンネル数
	static constexpr int DRUM_CHANNELS = 6;

	/// @brief ChipDriver のチャンネル番号の始まり
	static constexpr int FM_CHANNEL_BASE = 0;
	static constexpr int SSG_CHANNEL_BASE = FM_CHANNELS;
	static constexpr int DRUM_CHANNEL_BASE = FM_CHANNELS + SSG_CHANNELS;
	static constexpr int CHANNEL_COUNT = DRUM_CHANNEL_BASE + DRUM_CHANNELS;

	/// @brief setParam の id (従来の挙動の再現用)。
	/// @details 従来の OpnaSequencer は、SSG トラックの V と P を、同じ番号の FM チャンネルへの
	///          書き込みとして実行していた。その再現のため、FM チャンネル番号 channel をそのまま渡す。
	static constexpr int PARAM_LEGACY_FM_VOLUME = chip_param::FIRST_CHIP_SPECIFIC;
	static constexpr int PARAM_LEGACY_FM_PAN = chip_param::FIRST_CHIP_SPECIFIC + 1;

	using OpParams = OpnaOpParams;
	using FmVoice = OpnaFmVoice;

	/// @brief F-Number + ブロック (周波数設定値)
	struct FnumBlock
	{
		std::uint16_t fnum;   ///< F-Number (0-2047)
		std::uint8_t block;   ///< ブロック = オクターブ (0-7)
	};

	/// @brief コンストラクタ (チップを初期化する)
	OpnaDriver()
	{
		// 6 チャンネルモードにする (レジスタ 0x29 の bit7)。既定は 3 チャンネルで ch3-5 が鳴らない
		writeReg(0x29, 0x9F); // bit7=1(6ch), bit0-4=0x1F(IRQ mask)
	}

	// ------------------------------------------------------------------
	// 層 1 (Device) へのアクセス
	// ------------------------------------------------------------------

	/// @brief ポート 0 へのレジスタ書き込み (FM ch0-2, SSG, リズム)
	void writeReg(std::uint8_t addr, std::uint8_t data)
	{
		m_device.write(0, addr, data);
	}

	/// @brief ポート 1 へのレジスタ書き込み (FM ch3-5)
	void writeRegHi(std::uint8_t addr, std::uint8_t data)
	{
		m_device.write(1, addr, data);
	}

	/// @brief 操作している Device (OPNA 専用の機能が要るとき)
	[[nodiscard]] OpnaDevice& opnaDevice() noexcept
	{
		return m_device;
	}

	/// @brief チップのネイティブレート (Hz)
	[[nodiscard]] std::uint32_t sampleRate() const noexcept
	{
		return m_device.nativeRate();
	}

	/// @brief 指定サンプル数の PCM をネイティブレートで生成する
	/// @return 16bit モノラル。FM は左右の平均、SSG は 0.3 倍して足し、0.8 倍してソフトクリップする
	[[nodiscard]] PcmBuffer renderSamples(std::uint32_t numSamples)
	{
		PcmBuffer buf(numSamples);
		renderInto(buf.data(), numSamples);
		return buf;
	}

	/// @brief renderSamples と同じ内容を、呼び出し側が用意した領域に書く
	/// @details ymfm を塊で呼び、FM は 18 サンプル、SSG は 4 サンプルのあいだ同じ値が続くので、
	///          入力 3 要素が前と同じならミックス結果を使い回す。結果は 1 サンプルずつ計算した場合と同じ。
	void renderInto(std::int16_t* dst, std::uint32_t numSamples)
	{
		std::array<OpnaDevice::RawFrame, RENDER_BLOCK> raw;
		while (numSamples > 0)
		{
			const std::uint32_t n = std::min<std::uint32_t>(numSamples, RENDER_BLOCK);
			m_device.generateRaw(raw.data(), n);
			for (std::uint32_t i = 0; i < n; ++i)
			{
				const auto& f = raw[i];
				if (!m_haveLast || f.data[0] != m_lastRaw.data[0] || f.data[1] != m_lastRaw.data[1]
					|| f.data[2] != m_lastRaw.data[2])
				{
					m_lastRaw = f;
					m_lastMono = OpnaDevice::legacyMonoSample(f);
					m_haveLast = true;
				}
				dst[i] = m_lastMono;
			}
			dst += n;
			numSamples -= n;
		}
	}

	/// @brief チップをリセットする
	void reset() override
	{
		m_device.reset();
		m_lrAmsPms.fill(0xC0);
		m_ssgToneMask = 0;
		m_ssgSounding.fill(false);
		m_haveLast = false;
		// リセット後も 6 チャンネルモードを再設定する
		writeReg(0x29, 0x9F);
	}

	// ------------------------------------------------------------------
	// ChipDriver
	// ------------------------------------------------------------------

	[[nodiscard]] ChipDevice& device() noexcept override
	{
		return m_device;
	}

	[[nodiscard]] int channelCount() const noexcept override
	{
		return CHANNEL_COUNT;
	}

	[[nodiscard]] ChannelCaps channelCaps(int channel) const noexcept override
	{
		ChannelCaps caps;
		if (channel >= FM_CHANNEL_BASE && channel < SSG_CHANNEL_BASE)
		{
			caps.valid = true;
			caps.pitchSlide = true;
		}
		else if (channel >= SSG_CHANNEL_BASE && channel < DRUM_CHANNEL_BASE)
		{
			caps.valid = true;
			caps.hardwareEnvelope = true;
		}
		else if (channel >= DRUM_CHANNEL_BASE && channel < CHANNEL_COUNT)
		{
			caps.valid = true;
		}
		return caps;
	}

	void noteOn(int channel, int key, int velocity) override
	{
		if (isFm(channel))
		{
			fmNoteOn(channel, key);
		}
		else if (isSsg(channel))
		{
			enableSsgTone(channel - SSG_CHANNEL_BASE);
			setSsgTone(channel - SSG_CHANNEL_BASE, keyToFrequency(key),
				static_cast<std::uint8_t>(std::clamp(velocity, 0, 15)));
			m_ssgSounding[channel - SSG_CHANNEL_BASE] = true;
		}
		else if (isDrum(channel))
		{
			switch (channel - DRUM_CHANNEL_BASE)
			{
			case 0: case 3: synthKick(3); break;   // BD、タム
			case 1: case 5: synthSnare(4); break;  // SD、リムショット
			case 2: case 4: synthHihat(5); break;  // HH、シンバル
			default: break;
			}
		}
	}

	void noteOff(int channel) override
	{
		if (isFm(channel))
		{
			fmNoteOff(channel);
		}
		else if (isSsg(channel))
		{
			ssgNoteOff(channel - SSG_CHANNEL_BASE);
			m_ssgSounding[channel - SSG_CHANNEL_BASE] = false;
		}
		else if (isDrum(channel))
		{
			switch (channel - DRUM_CHANNEL_BASE)
			{
			case 0: case 3: fmNoteOff(3); break;
			case 1: case 5: fmNoteOff(4); break;
			case 2: case 4: fmNoteOff(5); break;
			default: break;
			}
		}
	}

	void setVoice(int channel, int voice) override
	{
		if (isFm(channel))
		{
			setFmVoice(channel, opna_presets::getPreset(voice));
		}
	}

	void setVolume(int channel, int volume) override
	{
		if (isFm(channel))
		{
			setFmVolume(channel, volumeToTotalLevel(volume));
		}
		else if (isSsg(channel))
		{
			// 発音中だけ音量レジスタへ反映する。次の noteOn は velocity で音量を渡してくる
			const int ch = channel - SSG_CHANNEL_BASE;
			if (m_ssgSounding[ch])
			{
				writeReg(static_cast<std::uint8_t>(0x08 + ch),
					static_cast<std::uint8_t>(std::clamp(volume, 0, 15)));
			}
		}
	}

	void setPan(int channel, int pan) override
	{
		if (isFm(channel))
		{
			setFmPan(channel, pan);
		}
	}

	void setPitch(int channel, int key) override
	{
		if (isFm(channel))
		{
			writeFmFrequency(channel, key);
		}
	}

	void setParam(int channel, int id, int value, int value2) override
	{
		if (id == chip_param::ENVELOPE)
		{
			if (isSsg(channel))
			{
				setSsgEnvelope(channel - SSG_CHANNEL_BASE,
					static_cast<std::uint8_t>(value), static_cast<std::uint16_t>(value2));
			}
		}
		else if (id == PARAM_LEGACY_FM_VOLUME)
		{
			setFmVolume(channel, volumeToTotalLevel(value));
		}
		else if (id == PARAM_LEGACY_FM_PAN)
		{
			setFmPan(channel, value);
		}
	}

	// ------------------------------------------------------------------
	// OPNA 固有の操作 (従来から公開している物)
	// ------------------------------------------------------------------

	/// @brief MIDI ノート番号から OPNA F-Number + Block に変換する
	/// @param midiNote MIDI ノート番号 (60=C4)
	[[nodiscard]] static FnumBlock noteToFnumBlock(int midiNote) noexcept
	{
		const float freq = 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);

		// F-Number = (144 * freq * 2^20) / (masterClock * 2^(block-1))
		// 最初の block=4 に対する値を求め、[1024, 2048) に収まるようブロックを動かす。
		// 1024 以上に保つのは、F-Number の丸め誤差 (最大 0.5/1024) を 1 セント未満にするため。
		int block = 4;
		float fnumF = freq * 144.0f * static_cast<float>(1 << 20)
			/ (static_cast<float>(CLOCK) * static_cast<float>(1 << (block - 1)));

		while (fnumF >= 2048.0f && block < 7)
		{
			fnumF /= 2.0f;
			++block;
		}
		while (fnumF < 1024.0f && block > 0)
		{
			fnumF *= 2.0f;
			--block;
		}

		return {
			static_cast<std::uint16_t>(std::clamp(std::lround(fnumF), 0L, 2047L)),
			static_cast<std::uint8_t>(block)
		};
	}

	/// @brief FM チャンネルにボイス (音色) を設定する
	/// @param channel チャンネル番号 (0-5)
	void setFmVoice(int channel, const FmVoice& voice)
	{
		const int ch = channel % 3;

		// アルゴリズム + フィードバック: レジスタ 0xB0+ch
		const std::uint8_t algFb = static_cast<std::uint8_t>((voice.feedback << 3) | voice.algorithm);
		writeFm(channel, static_cast<std::uint8_t>(0xB0 + ch), algFb);

		// LR パン: 両チャンネル ON (これが無いと FM 出力が無音になる)
		setLrAmsPms(channel, 0xC0);

		// スロット = ch + op * 4。0x30:DT/MUL 0x40:TL 0x50:KS/AR 0x60:DR 0x70:SR 0x80:SL/RR
		for (int op = 0; op < 4; ++op)
		{
			const auto& p = voice.ops[op];
			const int slot = ch + op * 4;
			writeFm(channel, static_cast<std::uint8_t>(0x30 + slot),
				static_cast<std::uint8_t>((p.detune << 4) | p.multiple));
			writeFm(channel, static_cast<std::uint8_t>(0x40 + slot), p.totalLevel);
			writeFm(channel, static_cast<std::uint8_t>(0x50 + slot),
				static_cast<std::uint8_t>((p.keyScale << 6) | p.attackRate));
			writeFm(channel, static_cast<std::uint8_t>(0x60 + slot), p.decayRate);
			writeFm(channel, static_cast<std::uint8_t>(0x70 + slot), p.sustainRate);
			writeFm(channel, static_cast<std::uint8_t>(0x80 + slot),
				static_cast<std::uint8_t>((p.sustainLevel << 4) | p.releaseRate));
		}
	}

	/// @brief FM チャンネルのキーオン (発音開始)
	/// @param channel チャンネル番号 (0-5)
	/// @param midiNote MIDI ノート番号 (60=C4)
	/// @param velocity ベロシティ (現在未使用)
	void fmNoteOn(int channel, int midiNote, float velocity = 1.0f)
	{
		static_cast<void>(velocity);
		writeFmFrequency(channel, midiNote);
		writeReg(0x28, static_cast<std::uint8_t>(0xF0 | keyOnBits(channel))); // 全 4 オペレータ有効
	}

	/// @brief FM チャンネルのキーオフ (消音)
	void fmNoteOff(int channel)
	{
		writeReg(0x28, static_cast<std::uint8_t>(0x00 | keyOnBits(channel)));
	}

	/// @brief FM チャンネルの音量を設定する (TL を調整)
	/// @param volume 音量 (0-127, 0=最大, 127=無音)
	void setFmVolume(int channel, std::uint8_t volume)
	{
		// キャリアの TL だけ調整する。アルゴリズムによってキャリアは違うが、簡易的に OP4 (slot=ch+12) とする
		const int slot = channel % 3 + 12;
		writeFm(channel, static_cast<std::uint8_t>(0x40 + slot), volume);
	}

	/// @brief SSG チャンネルの音程と音量を設定する
	/// @param channel SSG チャンネル番号 (0-2)
	/// @param freq 周波数 (Hz)
	/// @param volume 音量 (0-15)
	void setSsgTone(int channel, float freq, std::uint8_t volume)
	{
		if (channel < 0 || channel >= SSG_CHANNELS) return;
		if (freq <= 0.0f) return;

		writeSsgPeriod(channel, freq);
		writeReg(static_cast<std::uint8_t>(0x08 + channel), static_cast<std::uint8_t>(volume & 0x0F));
	}

	/// @brief SSG トーンの有効/無効を設定する
	void setSsgEnable(bool toneA, bool toneB, bool toneC)
	{
		// イネーブルレジスタはアクティブ Low。ノイズは全チャンネル無効のまま
		std::uint8_t val = 0x38;
		if (!toneA) val |= 0x01;
		if (!toneB) val |= 0x02;
		if (!toneC) val |= 0x04;
		writeReg(0x07, val);
		m_ssgToneMask = static_cast<std::uint8_t>((toneA ? 1 : 0) | (toneB ? 2 : 0) | (toneC ? 4 : 0));
	}

	/// @brief SSG ハードウェアエンベロープを設定する
	/// @param shape 形状 (0x00-0x0F)。0x09=減衰停止, 0x0B=減衰サステイン, 0x0D=アタック繰返し
	/// @param period 周期 (0-65535)
	void setSsgEnvelope(int channel, std::uint8_t shape, std::uint16_t period)
	{
		if (channel < 0 || channel >= SSG_CHANNELS) return;
		writeReg(0x0B, static_cast<std::uint8_t>(period & 0xFF));
		writeReg(0x0C, static_cast<std::uint8_t>((period >> 8) & 0xFF));
		writeReg(0x0D, shape);
		// チャンネルをエンベロープモードにする (bit4=1)
		writeReg(static_cast<std::uint8_t>(0x08 + channel), 0x10);
	}

	/// @brief SSG チャンネルのキーオフ (音量ゼロ)
	void ssgNoteOff(int channel)
	{
		if (channel < 0 || channel >= SSG_CHANNELS) return;
		writeReg(static_cast<std::uint8_t>(0x08 + channel), 0);
	}

	/// @brief リズム音源のキーオン
	/// @param instruments 楽器ビットマスク (bit0=BD, bit1=SD, bit2=TOP, bit3=HH, bit4=TOM, bit5=RIM)
	/// @param totalLevel リズム全体の音量 (0x00-0x3F)
	void triggerRhythm(std::uint8_t instruments, std::uint8_t totalLevel = 0x30)
	{
		writeReg(0x11, totalLevel);
		writeReg(0x10, instruments);
	}

	/// @brief FM チャンネルでキックドラムを合成する (ADPCM ROM 不要)
	/// @details ALG=4 (2 キャリア)、FB=7 (最大フィードバック) で低音のパンチを出す
	void synthKick(int channel = 3)
	{
		FmVoice kick = {4, 7, {
			{3, 1, 30, 0, 31, 20, 0, 2, 8},  // mod1: 歪み生成
			{0, 1,  5, 0, 31, 18, 0, 1, 8},  // carrier1: 低音ボディ
			{3, 2, 35, 0, 31, 22, 0, 3, 9},  // mod2: アタックノイズ
			{0, 0,  3, 0, 31, 16, 0, 1, 7},  // carrier2: サブベース (MUL=0 で半周波数)
		}};
		setFmVoice(channel, kick);
		fmNoteOn(channel, 36); // C2
	}

	/// @brief FM チャンネルでスネアドラムを合成する
	void synthSnare(int channel = 4)
	{
		FmVoice snare = {4, 7, {
			{7, 3, 20, 0, 31, 14, 0, 2, 10}, // mod1: 高倍率ノイズ
			{0, 1,  5, 0, 31, 16, 0, 2, 9},  // carrier1: スネアボディ
			{3, 7, 25, 0, 31, 12, 0, 3, 11}, // mod2: 金属的なノイズ
			{0, 2,  3, 0, 31, 14, 0, 2, 10}, // carrier2: スナッピー
		}};
		setFmVoice(channel, snare);
		fmNoteOn(channel, 50); // D3
	}

	/// @brief FM チャンネルでハイハットを合成する
	void synthHihat(int channel = 5)
	{
		FmVoice hihat = {4, 7, {
			{7, 14, 15, 0, 31, 10, 0, 2, 12}, // mod1: 超高域ノイズ
			{0,  1,  5, 0, 31, 22, 0, 4, 12}, // carrier1: ハイハットのボディ
			{5, 13, 20, 0, 31, 12, 0, 3, 13}, // mod2: 金属共鳴
			{0,  3,  5, 0, 31, 24, 0, 5, 13}, // carrier2: チック音
		}};
		setFmVoice(channel, hihat);
		fmNoteOn(channel, 72); // C5
	}

	/// @brief ハードウェア LFO を設定する
	/// @param rate LFO 速度 (0-7, 0=3.98Hz ... 7=72.2Hz)
	void setLfo(bool enable, std::uint8_t rate = 3)
	{
		writeReg(0x22, static_cast<std::uint8_t>((enable ? 0x08 : 0x00) | (rate & 0x07)));
	}

	/// @brief チャンネルの LFO 感度を設定する (パンは変えない)
	/// @param pmDepth ピッチモジュレーションの深さ (0-7)
	/// @param amDepth 振幅モジュレーションの深さ (0-3)
	void setChannelLfoSensitivity(int channel, std::uint8_t pmDepth, std::uint8_t amDepth)
	{
		const std::uint8_t lr = static_cast<std::uint8_t>(m_lrAmsPms[shadowIndex(channel)] & 0xC0);
		setLrAmsPms(channel, static_cast<std::uint8_t>(
			lr | ((amDepth & 0x03) << 4) | (pmDepth & 0x07)));
	}

	/// @brief FM チャンネルのパン定位を設定する (LFO 感度は変えない)
	/// @param pan 0=左, 1=中央, 2=右
	void setFmPan(int channel, int pan)
	{
		std::uint8_t lr = 0xC0; // 既定: 両方 ON
		switch (pan)
		{
		case 0: lr = 0x80; break; // 左のみ
		case 1: lr = 0xC0; break; // 中央 (両方)
		case 2: lr = 0x40; break; // 右のみ
		}
		const std::uint8_t sensitivity = static_cast<std::uint8_t>(m_lrAmsPms[shadowIndex(channel)] & 0x37);
		setLrAmsPms(channel, static_cast<std::uint8_t>(lr | sensitivity));
	}

private:
	static constexpr std::uint32_t RENDER_BLOCK = 512;

	[[nodiscard]] static bool isFm(int channel) noexcept
	{
		return channel >= FM_CHANNEL_BASE && channel < SSG_CHANNEL_BASE;
	}

	[[nodiscard]] static bool isSsg(int channel) noexcept
	{
		return channel >= SSG_CHANNEL_BASE && channel < DRUM_CHANNEL_BASE;
	}

	[[nodiscard]] static bool isDrum(int channel) noexcept
	{
		return channel >= DRUM_CHANNEL_BASE && channel < CHANNEL_COUNT;
	}

	/// @brief MML の音量 (0〜15) を FM のキャリア TL (0 が最大、127 が無音) に変える
	[[nodiscard]] static std::uint8_t volumeToTotalLevel(int volume) noexcept
	{
		return static_cast<std::uint8_t>(127 - (std::clamp(volume, 0, 15) * 127 / 15));
	}

	[[nodiscard]] static float keyToFrequency(int key) noexcept
	{
		return 440.0f * std::pow(2.0f, (static_cast<float>(key) - 69.0f) / 12.0f);
	}

	/// @brief 0xB4 の影の添字。channel の範囲外でも落ちないよう 6 で折り返す (従来は %3 と >=3 で hi を決めていた)
	[[nodiscard]] static std::size_t shadowIndex(int channel) noexcept
	{
		const int ch = channel % 3;
		const int hi = channel >= 3 ? 1 : 0;
		return static_cast<std::size_t>(hi * 3 + ch);
	}

	/// @brief channel が 3 以上ならポート 1 (ch3-5) へ、そうでなければポート 0 へ書く
	void writeFm(int channel, std::uint8_t reg, std::uint8_t value)
	{
		if (channel >= 3)
		{
			writeRegHi(reg, value);
		}
		else
		{
			writeReg(reg, value);
		}
	}

	/// @brief 0xB4 (L/R、AMS、PMS) を書き、影の値も更新する
	void setLrAmsPms(int channel, std::uint8_t value)
	{
		m_lrAmsPms[shadowIndex(channel)] = value;
		writeFm(channel, static_cast<std::uint8_t>(0xB4 + channel % 3), value);
	}

	/// @brief レジスタ 0x28 のチャンネル指定ビット (ch3-5 は +4)
	[[nodiscard]] static std::uint8_t keyOnBits(int channel) noexcept
	{
		const int ch = channel % 3;
		return channel >= 3 ? static_cast<std::uint8_t>(ch + 4) : static_cast<std::uint8_t>(ch);
	}

	/// @brief 周波数 (F-Number と Block) だけを書く。キーオンはしない
	void writeFmFrequency(int channel, int midiNote)
	{
		const int ch = channel % 3;
		const auto [fnum, block] = noteToFnumBlock(midiNote);
		// 上位バイトを先に書く
		writeFm(channel, static_cast<std::uint8_t>(0xA4 + ch),
			static_cast<std::uint8_t>((block << 3) | ((fnum >> 8) & 0x07)));
		writeFm(channel, static_cast<std::uint8_t>(0xA0 + ch), static_cast<std::uint8_t>(fnum & 0xFF));
	}

	/// @brief SSG の周期レジスタを書く
	void writeSsgPeriod(int channel, float freq)
	{
		// SSG 周期 = 実効クロック / (16 * freq)。実効クロックは CLOCK / プリスケール (標準は 4)
		const std::uint32_t effClock = CLOCK / 4;
		std::uint16_t period = static_cast<std::uint16_t>(static_cast<float>(effClock) / (16.0f * freq));
		if (period == 0) period = 1;
		writeReg(static_cast<std::uint8_t>(channel * 2), static_cast<std::uint8_t>(period & 0xFF));
		writeReg(static_cast<std::uint8_t>(channel * 2 + 1), static_cast<std::uint8_t>((period >> 8) & 0x0F));
	}

	/// @brief ChipDriver 経由の発音で、トーンが無効のチャンネルを有効にする
	void enableSsgTone(int ch)
	{
		const auto bit = static_cast<std::uint8_t>(1 << ch);
		if ((m_ssgToneMask & bit) == 0)
		{
			setSsgEnable((m_ssgToneMask & 1) != 0 || ch == 0,
				(m_ssgToneMask & 2) != 0 || ch == 1,
				(m_ssgToneMask & 4) != 0 || ch == 2);
		}
	}

	OpnaDevice m_device;
	std::array<std::uint8_t, 6> m_lrAmsPms = {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0}; ///< 0xB4 の影 (パンと LFO 感度が互いを消さないため)
	std::uint8_t m_ssgToneMask = 0;                  ///< setSsgEnable で有効にしたトーンのビット
	std::array<bool, SSG_CHANNELS> m_ssgSounding{};  ///< SSG が発音中か
	bool m_haveLast = false;                         ///< renderInto の結果の使い回しが有効か
	OpnaDevice::RawFrame m_lastRaw{};
	std::int16_t m_lastMono = 0;
};

} // namespace mitiru_mml
