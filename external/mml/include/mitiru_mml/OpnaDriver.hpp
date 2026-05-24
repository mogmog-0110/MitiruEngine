#pragma once
/// @file OpnaDriver.hpp
/// @brief YM2608 (OPNA) ハードウェアエミュレーションドライバー
/// @details ymfmライブラリを使用してYM2608チップをエミュレートし、
///          FM音源6ch・SSG音源3ch・リズム音源をレジスタレベルで制御する。
///
/// @code
/// mitiru_mml::OpnaDriver driver;
/// driver.setFmVoice(0, mitiru_mml::opna_presets::PIANO);
/// driver.fmNoteOn(0, 60);  // C4
/// auto pcm = driver.renderSamples(22050);  // 0.5秒分
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <ymfm_opn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mitiru_mml
{

/// @brief ymfmインターフェース実装（OPNA用）
/// @details ymfm::ymfm_interfaceを継承し、外部メモリ読み書きコールバックを提供する。
///          ADPCM-A/Bリズムサンプルは静音値（0x80）を返す。
class OpnaInterface : public ymfm::ymfm_interface
{
public:
	/// @brief 外部メモリ読み出しコールバック
	/// @param type アクセス種別（ADPCM_A, ADPCM_B, PCM）
	/// @param address 読み出しアドレス
	/// @return 読み出しデータ（リズムROMが無いため静音値を返す）
	uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
	{
		static_cast<void>(address);
		// ADPCM-A/Bリズムサンプルは静音値（0x80 = ADPCMの中心値）を返す
		// 実際のリズム音色にはROMデータが必要だが、ここでは省略
		if (type == ymfm::ACCESS_ADPCM_A)
		{
			return 0x80;
		}
		if (type == ymfm::ACCESS_ADPCM_B)
		{
			return 0x80;
		}
		return 0;
	}

	/// @brief 外部メモリ書き込みコールバック（何もしない）
	void ymfm_external_write(ymfm::access_class type, uint32_t address, uint8_t data) override
	{
		static_cast<void>(type);
		static_cast<void>(address);
		static_cast<void>(data);
	}
};

/// @brief YM2608 (OPNA) ドライバー
/// @details ymfmライブラリを使ってYM2608チップをエミュレートする。
///          FM6ch + SSG3ch + リズム音源を直接レジスタ操作で制御し、
///          PCMサンプルを生成する。
class OpnaDriver
{
public:
	/// @brief PC-98標準OPNAクロック（7.9872MHz）
	static constexpr uint32_t CLOCK = 7987200;
	/// @brief FM音源チャンネル数
	static constexpr int FM_CHANNELS = 6;
	/// @brief SSG音源チャンネル数
	static constexpr int SSG_CHANNELS = 3;

	/// @brief FMオペレータパラメータ（1オペレータ分）
	struct OpParams
	{
		uint8_t detune = 0;       ///< デチューン (0-7)
		uint8_t multiple = 1;     ///< 周波数倍率 (0-15, 0=0.5倍)
		uint8_t totalLevel = 0;   ///< 出力レベル (0=最大, 127=無音)
		uint8_t keyScale = 0;     ///< キースケーリング (0-3)
		uint8_t attackRate = 31;  ///< アタックレート (0-31)
		uint8_t decayRate = 0;    ///< ディケイレート (0-31)
		uint8_t sustainRate = 0;  ///< サステインレート (0-31)
		uint8_t sustainLevel = 0; ///< サステインレベル (0-15, 0=最大)
		uint8_t releaseRate = 7;  ///< リリースレート (0-15)
	};

	/// @brief FMボイス定義（4オペレータ + アルゴリズム + フィードバック）
	struct FmVoice
	{
		uint8_t algorithm = 4;  ///< アルゴリズム (0-7)
		uint8_t feedback = 5;   ///< フィードバック (0-7)
		OpParams ops[4];        ///< 4オペレータ
	};

	/// @brief F-Number + ブロック（周波数設定値）
	struct FnumBlock
	{
		uint16_t fnum;   ///< F-Number (0-2047)
		uint8_t block;   ///< ブロック = オクターブ (0-7)
	};

	/// @brief コンストラクタ（チップを初期化する）
	OpnaDriver()
	{
		m_chip = std::make_unique<ymfm::ym2608>(m_interface);
		m_chip->reset();
		m_sampleRate = m_chip->sample_rate(CLOCK);
		// 6チャンネルモードを有効にする（レジスタ0x29のbit7）
		// デフォルトでは3チャンネルモード(ch0-2のみ)のため、
		// ch3-5を使用するにはこの設定が必須
		writeReg(0x29, 0x9F); // bit7=1(6ch), bit0-4=0x1F(IRQ mask)
	}

	/// @brief ポート0へのレジスタ書き込み（FM ch0-2, SSG, リズム）
	/// @param addr レジスタアドレス
	/// @param data 書き込みデータ
	void writeReg(uint8_t addr, uint8_t data)
	{
		m_chip->write_address(addr);
		m_chip->write_data(data);
	}

	/// @brief ポート1へのレジスタ書き込み（FM ch3-5）
	/// @param addr レジスタアドレス
	/// @param data 書き込みデータ
	void writeRegHi(uint8_t addr, uint8_t data)
	{
		m_chip->write_address_hi(addr);
		m_chip->write_data_hi(data);
	}

	/// @brief MIDIノート番号からOPNA F-Number + Blockに変換する
	/// @param midiNote MIDIノート番号（60=C4）
	/// @return F-Number + ブロック値
	[[nodiscard]] static FnumBlock noteToFnumBlock(int midiNote) noexcept
	{
		const float freq = 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);

		// OPNA F-Number計算: F-Number = (144 * freq * 2^20) / (masterClock * 2^(block-1))
		int block = 4;
		float fnumF = freq * 144.0f * static_cast<float>(1 << 20) / static_cast<float>(CLOCK);

		// 適切なブロック（オクターブ）を探す
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
			static_cast<uint16_t>(std::clamp(fnumF, 0.0f, 2047.0f)),
			static_cast<uint8_t>(block)
		};
	}

	/// @brief FMチャンネルにボイス（音色）を設定する
	/// @param channel チャンネル番号 (0-5)
	/// @param voice FMボイスデータ
	void setFmVoice(int channel, const FmVoice& voice)
	{
		const bool hi = channel >= 3;
		const int ch = channel % 3;

		// アルゴリズム + フィードバック: レジスタ0xB0+ch
		const uint8_t algFb = static_cast<uint8_t>((voice.feedback << 3) | voice.algorithm);
		if (hi)
		{
			writeRegHi(static_cast<uint8_t>(0xB0 + ch), algFb);
		}
		else
		{
			writeReg(static_cast<uint8_t>(0xB0 + ch), algFb);
		}

		// LR パン設定: 両チャンネルON（これが無いとFM出力が無音になる）
		if (hi)
		{
			writeRegHi(static_cast<uint8_t>(0xB4 + ch), 0xC0); // L=1, R=1
		}
		else
		{
			writeReg(static_cast<uint8_t>(0xB4 + ch), 0xC0); // L=1, R=1
		}

		// OPNAのオペレータレジスタ配置:
		// スロット = ch + op * 4（op=0,1,2,3）
		// レジスタ0x30+slot: DT/MUL
		// レジスタ0x40+slot: TL
		// レジスタ0x50+slot: KS/AR
		// レジスタ0x60+slot: DR (AM/DR)
		// レジスタ0x70+slot: SR (D2R)
		// レジスタ0x80+slot: SL/RR
		for (int op = 0; op < 4; ++op)
		{
			const auto& p = voice.ops[op];
			const int slot = ch + op * 4;

			auto wr = [&](uint8_t reg, uint8_t val)
			{
				if (hi)
				{
					writeRegHi(reg, val);
				}
				else
				{
					writeReg(reg, val);
				}
			};

			wr(static_cast<uint8_t>(0x30 + slot),
				static_cast<uint8_t>((p.detune << 4) | p.multiple));
			wr(static_cast<uint8_t>(0x40 + slot), p.totalLevel);
			wr(static_cast<uint8_t>(0x50 + slot),
				static_cast<uint8_t>((p.keyScale << 6) | p.attackRate));
			wr(static_cast<uint8_t>(0x60 + slot), p.decayRate);
			wr(static_cast<uint8_t>(0x70 + slot), p.sustainRate);
			wr(static_cast<uint8_t>(0x80 + slot),
				static_cast<uint8_t>((p.sustainLevel << 4) | p.releaseRate));
		}
	}

	/// @brief FMチャンネルのキーオン（発音開始）
	/// @param channel チャンネル番号 (0-5)
	/// @param midiNote MIDIノート番号（60=C4）
	/// @param velocity ベロシティ（現在未使用）
	void fmNoteOn(int channel, int midiNote, float velocity = 1.0f)
	{
		static_cast<void>(velocity);

		const bool hi = channel >= 3;
		const int ch = channel % 3;

		const auto [fnum, block] = noteToFnumBlock(midiNote);

		auto wr = [&](uint8_t reg, uint8_t val)
		{
			if (hi)
			{
				writeRegHi(reg, val);
			}
			else
			{
				writeReg(reg, val);
			}
		};

		// 周波数設定（上位バイトを先に書く）
		wr(static_cast<uint8_t>(0xA4 + ch),
			static_cast<uint8_t>((block << 3) | ((fnum >> 8) & 0x07)));
		wr(static_cast<uint8_t>(0xA0 + ch),
			static_cast<uint8_t>(fnum & 0xFF));

		// キーオン: 全4オペレータ有効
		const uint8_t chBits = hi ? static_cast<uint8_t>(ch + 4) : static_cast<uint8_t>(ch);
		writeReg(0x28, static_cast<uint8_t>(0xF0 | chBits));
	}

	/// @brief FMチャンネルのキーオフ（消音）
	/// @param channel チャンネル番号 (0-5)
	void fmNoteOff(int channel)
	{
		const bool hi = channel >= 3;
		const int ch = channel % 3;
		const uint8_t chBits = hi ? static_cast<uint8_t>(ch + 4) : static_cast<uint8_t>(ch);
		writeReg(0x28, static_cast<uint8_t>(0x00 | chBits));
	}

	/// @brief FMチャンネルの音量を設定する（TLを調整）
	/// @param channel チャンネル番号 (0-5)
	/// @param volume 音量 (0-127, 0=最大, 127=無音)
	void setFmVolume(int channel, uint8_t volume)
	{
		const bool hi = channel >= 3;
		const int ch = channel % 3;

		// キャリアオペレータのTLのみ調整する
		// アルゴリズムによってキャリアは異なるが、簡易的にOP4(slot=ch+12)を設定
		const int slot = ch + 12;
		if (hi)
		{
			writeRegHi(static_cast<uint8_t>(0x40 + slot), volume);
		}
		else
		{
			writeReg(static_cast<uint8_t>(0x40 + slot), volume);
		}
	}

	/// @brief SSGチャンネルの音程と音量を設定する
	/// @param channel SSGチャンネル番号 (0-2)
	/// @param freq 周波数(Hz)
	/// @param volume 音量 (0-15)
	void setSsgTone(int channel, float freq, uint8_t volume)
	{
		if (channel < 0 || channel >= SSG_CHANNELS) return;
		if (freq <= 0.0f) return;

		// SSG周期 = 実効クロック / (16 * freq)
		// 実効クロック = CLOCK / プリスケール（標準は4）
		const uint32_t effClock = CLOCK / 4;
		uint16_t period = static_cast<uint16_t>(
			static_cast<float>(effClock) / (16.0f * freq));
		if (period == 0) period = 1;

		writeReg(static_cast<uint8_t>(channel * 2), static_cast<uint8_t>(period & 0xFF));
		writeReg(static_cast<uint8_t>(channel * 2 + 1), static_cast<uint8_t>((period >> 8) & 0x0F));
		writeReg(static_cast<uint8_t>(0x08 + channel), static_cast<uint8_t>(volume & 0x0F));
	}

	/// @brief SSGトーン有効/無効を設定する
	/// @param toneA チャンネルAのトーン有効
	/// @param toneB チャンネルBのトーン有効
	/// @param toneC チャンネルCのトーン有効
	void setSsgEnable(bool toneA, bool toneB, bool toneC)
	{
		// SSGイネーブルレジスタ（アクティブLow）
		uint8_t val = 0x38; // ノイズは全チャンネルオフ
		if (!toneA) val |= 0x01;
		if (!toneB) val |= 0x02;
		if (!toneC) val |= 0x04;
		writeReg(0x07, val);
	}

	/// @brief SSGハードウェアエンベロープを設定する
	/// @param channel SSGチャンネル番号 (0-2)
	/// @param shape エンベロープ形状 (0x00-0x0F)
	/// @param period エンベロープ周期 (0-65535)
	/// @details 形状: 0x09=減衰停止, 0x0B=減衰サステイン, 0x0D=アタック繰返し
	void setSsgEnvelope(int channel, uint8_t shape, uint16_t period)
	{
		if (channel < 0 || channel >= SSG_CHANNELS) return;
		writeReg(0x0B, static_cast<uint8_t>(period & 0xFF));
		writeReg(0x0C, static_cast<uint8_t>((period >> 8) & 0xFF));
		writeReg(0x0D, shape);
		// チャンネルをエンベロープモードに設定する（bit4=1）
		writeReg(static_cast<uint8_t>(0x08 + channel), 0x10);
	}

	/// @brief SSGチャンネルのキーオフ（音量ゼロ）
	/// @param channel SSGチャンネル番号 (0-2)
	void ssgNoteOff(int channel)
	{
		if (channel < 0 || channel >= SSG_CHANNELS) return;
		writeReg(static_cast<uint8_t>(0x08 + channel), 0);
	}

	/// @brief リズム音源のキーオン
	/// @param instruments 楽器ビットマスク (bit0=BD, bit1=SD, bit2=TOP, bit3=HH, bit4=TOM, bit5=RIM)
	/// @param totalLevel リズム全体の音量 (0x00-0x3F)
	void triggerRhythm(uint8_t instruments, uint8_t totalLevel = 0x30)
	{
		writeReg(0x11, totalLevel);
		writeReg(0x10, instruments);
	}

	/// @brief FMチャンネルでキックドラムを合成する（ADPCM ROM不要）
	/// @param channel 使用FMチャンネル (0-5)
	/// @details ALG=4(2キャリア), FB=7(最大フィードバック)で低音パンチを再現する
	void synthKick(int channel = 3)
	{
		FmVoice kick = {4, 7, {
			{3, 1, 30, 0, 31, 20, 0, 2, 8},  // mod1: 歪み生成
			{0, 1,  5, 0, 31, 18, 0, 1, 8},  // carrier1: 低音ボディ
			{3, 2, 35, 0, 31, 22, 0, 3, 9},  // mod2: アタックノイズ
			{0, 0,  3, 0, 31, 16, 0, 1, 7},  // carrier2: サブベース (MUL=0=半周波数)
		}};
		setFmVoice(channel, kick);
		fmNoteOn(channel, 36); // C2 = 低いキック
	}

	/// @brief FMチャンネルでスネアドラムを合成する
	/// @param channel 使用FMチャンネル (0-5)
	/// @details ALG=4(2キャリア), FB=7 — ノイジーな中音域でスネアを再現する
	void synthSnare(int channel = 4)
	{
		FmVoice snare = {4, 7, {
			{7, 3, 20, 0, 31, 14, 0, 2, 10}, // mod1: 高倍率ノイズ
			{0, 1,  5, 0, 31, 16, 0, 2, 9},  // carrier1: スネアボディ
			{3, 7, 25, 0, 31, 12, 0, 3, 11}, // mod2: 金属的なノイズ
			{0, 2,  3, 0, 31, 14, 0, 2, 10}, // carrier2: スネアスナッピー
		}};
		setFmVoice(channel, snare);
		fmNoteOn(channel, 50); // D3
	}

	/// @brief FMチャンネルでハイハットを合成する
	/// @param channel 使用FMチャンネル (0-5)
	/// @details ALG=4(2キャリア), FB=7 — 超高域ノイズでハイハットを再現する
	void synthHihat(int channel = 5)
	{
		FmVoice hihat = {4, 7, {
			{7, 14, 15, 0, 31, 10, 0, 2, 12}, // mod1: 超高域ノイズ
			{0,  1,  5, 0, 31, 22, 0, 4, 12}, // carrier1: ハイハットボディ
			{5, 13, 20, 0, 31, 12, 0, 3, 13}, // mod2: 金属共鳴
			{0,  3,  5, 0, 31, 24, 0, 5, 13}, // carrier2: チック音
		}};
		setFmVoice(channel, hihat);
		fmNoteOn(channel, 72); // C5 — より高いピッチ
	}

	/// @brief ハードウェアLFOを設定する
	/// @param enable LFO有効/無効
	/// @param rate LFO速度 (0-7, 0=3.98Hz ... 7=72.2Hz)
	void setLfo(bool enable, uint8_t rate = 3)
	{
		writeReg(0x22, static_cast<uint8_t>((enable ? 0x08 : 0x00) | (rate & 0x07)));
	}

	/// @brief チャンネルのLFO感度を設定する
	/// @param channel FMチャンネル (0-5)
	/// @param pmDepth ピッチモジュレーション深さ (0-7)
	/// @param amDepth 振幅モジュレーション深さ (0-3)
	void setChannelLfoSensitivity(int channel, uint8_t pmDepth, uint8_t amDepth)
	{
		const bool hi = channel >= 3;
		const int ch = channel % 3;
		// レジスタ0xB4: L(7) R(6) AMS(5-4) PMS(2-0)
		// L/R両方ON (0xC0) + AMS + PMS
		const uint8_t val = static_cast<uint8_t>(
			0xC0 | ((amDepth & 0x03) << 4) | (pmDepth & 0x07));
		if (hi)
		{
			writeRegHi(static_cast<uint8_t>(0xB4 + ch), val);
		}
		else
		{
			writeReg(static_cast<uint8_t>(0xB4 + ch), val);
		}
	}

	/// @brief FMチャンネルのパン定位を設定する
	/// @param channel FMチャンネル (0-5)
	/// @param pan パン値 (0=左, 1=中央, 2=右)
	void setFmPan(int channel, int pan)
	{
		const bool hi = channel >= 3;
		const int ch = channel % 3;
		uint8_t lr = 0xC0; // デフォルト: 両方ON
		switch (pan)
		{
		case 0: lr = 0x80; break; // 左のみ
		case 1: lr = 0xC0; break; // 中央（両方）
		case 2: lr = 0x40; break; // 右のみ
		}
		if (hi)
		{
			writeRegHi(static_cast<uint8_t>(0xB4 + ch), lr);
		}
		else
		{
			writeReg(static_cast<uint8_t>(0xB4 + ch), lr);
		}
	}

	/// @brief 指定サンプル数のPCMデータを生成する
	/// @param numSamples 生成サンプル数
	/// @return 16bitモノラルPCMバッファ
	[[nodiscard]] PcmBuffer renderSamples(uint32_t numSamples)
	{
		PcmBuffer buf(numSamples);
		ymfm::ym2608::output_data output;

		for (uint32_t i = 0; i < numSamples; ++i)
		{
			m_chip->generate(&output);
			// output.data[0] = FM Left
			// output.data[1] = FM Right
			// output.data[2] = SSG
			// 浮動小数点正規化 → ソフトクリップ → int16変換
			const float fmF = static_cast<float>(output.data[0] + output.data[1]) / 65536.0f;
			const float ssgF = static_cast<float>(output.data[2]) / 32768.0f * 0.3f;
			const float mixed = softClip((fmF + ssgF) * 0.8f);
			buf[i] = static_cast<int16_t>(mixed * 32767.0f);
		}

		return buf;
	}

	/// @brief チップのサンプルレートを取得する
	/// @return サンプルレート(Hz)
	[[nodiscard]] uint32_t sampleRate() const noexcept
	{
		return m_sampleRate;
	}

	/// @brief チップをリセットする
	void reset()
	{
		m_chip->reset();
		// リセット後も6チャンネルモードを再設定する
		writeReg(0x29, 0x9F);
	}

private:
	/// @brief 三次関数によるソフトクリッピング
	/// @param x 入力値
	/// @return クリップされた値（-1.0〜1.0）
	static float softClip(float x) noexcept
	{
		if (x > 1.0f) return 1.0f;
		if (x < -1.0f) return -1.0f;
		return 1.5f * x - 0.5f * x * x * x;
	}

	OpnaInterface m_interface;                    ///< ymfmインターフェース
	std::unique_ptr<ymfm::ym2608> m_chip;         ///< YM2608チップインスタンス
	uint32_t m_sampleRate = 0;                    ///< 出力サンプルレート
};

} // namespace mitiru_mml
