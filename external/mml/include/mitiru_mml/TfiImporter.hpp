#pragma once
/// @file TfiImporter.hpp
/// @brief TFI (Furnace/TFM) FM音色ファイルインポーター
/// @details 42バイトのTFIデータからOpnaDriver::FmVoiceを構築する。
///          TFIフォーマット: ALG(1) + FB(1) + 4 ops * (MUL,DT,TL,RS,AR,DR,SR,RR,SL,SSG)(10) = 42 bytes

#include <mitiru_mml/OpnaDriver.hpp>
#include <array>
#include <cstdint>

namespace mitiru_mml
{

/// @brief TFI FM音色ファイルインポーター
class TfiImporter
{
public:
	/// @brief TFIデータの固定サイズ
	static constexpr std::size_t TFI_SIZE = 42;

	/// @brief 42バイトのTFIデータからFmVoiceを構築する
	/// @param data TFIバイト配列（42バイト）
	/// @return FMボイスデータ
	[[nodiscard]] static OpnaDriver::FmVoice fromTfi(
		const std::array<uint8_t, TFI_SIZE>& data)
	{
		OpnaDriver::FmVoice voice;
		voice.algorithm = data[0];
		voice.feedback = data[1];

		for (int op = 0; op < 4; ++op)
		{
			int off = 2 + op * 10;
			auto& p = voice.ops[op];
			// TFI: MUL, DT, TL, RS, AR, DR, SR, RR, SL, SSG
			// FmVoice OpParams: DT, MUL, TL, KS, AR, DR, SR, SL, RR
			p.multiple    = data[off + 0];
			p.detune      = data[off + 1];
			p.totalLevel  = data[off + 2];
			p.keyScale    = data[off + 3];
			p.attackRate  = data[off + 4];
			p.decayRate   = data[off + 5];
			p.sustainRate = data[off + 6];
			p.releaseRate = data[off + 7];
			p.sustainLevel = data[off + 8];
			// SSG = data[off + 9]; // 現在未使用
		}

		return voice;
	}
};

} // namespace mitiru_mml
