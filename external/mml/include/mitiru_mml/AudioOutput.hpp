#pragma once
/// @file AudioOutput.hpp
/// @brief プラットフォームオーディオ出力（waveOut多チャンネル対応）
/// @details BGMとSEを同時再生可能。waveOut APIでPCMデータを直接送信する。

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/WavWriter.hpp>
#include <vector>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace mitiru_mml
{

/// @brief オーディオ出力（waveOut APIによる非同期再生）
/// @details BGMとSEを同時再生可能。waveOutでPCMデータを直接送信する。
class AudioOutput
{
public:
	/// @brief PCMバッファを非同期再生する（BGMチャンネル）
	/// @param pcm PCMサンプルデータ
	/// @param sampleRate サンプルレート
	/// @return 再生開始に成功したらtrue
	static bool play(const PcmBuffer& pcm, std::uint32_t sampleRate = 44100)
	{
		if (pcm.empty()) return false;
#ifdef _WIN32
		stopChannel(0);
		return playOnChannel(0, pcm, sampleRate);
#else
		static_cast<void>(pcm);
		static_cast<void>(sampleRate);
		return false;
#endif
	}

	/// @brief SE用PCMを再生する（SEチャンネル、BGMと同時再生可能）
	/// @param pcm PCMサンプルデータ
	/// @param sampleRate サンプルレート
	/// @return 再生開始に成功したらtrue
	static bool playSe(const PcmBuffer& pcm, std::uint32_t sampleRate = 44100)
	{
		if (pcm.empty()) return false;
#ifdef _WIN32
		stopChannel(1);
		return playOnChannel(1, pcm, sampleRate);
#else
		static_cast<void>(pcm);
		static_cast<void>(sampleRate);
		return false;
#endif
	}

	/// @brief BGMを停止する
	static void stop()
	{
#ifdef _WIN32
		stopChannel(0);
#endif
	}

	/// @brief 全チャンネルを停止する
	static void stopAll()
	{
#ifdef _WIN32
		stopChannel(0);
		stopChannel(1);
#endif
	}

	/// @brief ループ再生する（曲終了時に自動で最初から再生）
	/// @param pcm PCMサンプルデータ
	/// @param sampleRate サンプルレート
	/// @return 再生開始に成功したらtrue
	static bool playLooped(const PcmBuffer& pcm, std::uint32_t sampleRate = 44100)
	{
		if (pcm.empty()) return false;
#ifdef _WIN32
		stopChannel(0);
		auto& ch = getChannel(0);
		ch.loopData = WavWriter::toWav(pcm, sampleRate);
		ch.looping = true;
		ch.loopSampleRate = sampleRate;
		return startLoopPlayback(ch);
#else
		static_cast<void>(pcm);
		static_cast<void>(sampleRate);
		return false;
#endif
	}

private:
#ifdef _WIN32
	/// @brief 再生チャンネル情報
	struct Channel
	{
		HWAVEOUT hWaveOut = nullptr;
		WAVEHDR waveHdr = {};
		std::vector<std::uint8_t> wavData;
		bool active = false;
		bool looping = false;                     ///< ループ再生フラグ
		std::uint32_t loopSampleRate = 44100;     ///< ループ時サンプルレート
		std::vector<std::uint8_t> loopData;       ///< ループ用WAVデータ
	};

	/// @brief チャンネル配列を取得する（0=BGM, 1=SE）
	/// @param idx チャンネルインデックス
	/// @return チャンネル参照
	static Channel& getChannel(int idx)
	{
		static Channel channels[2];
		return channels[idx];
	}

	/// @brief チャンネルを停止する
	/// @param idx チャンネルインデックス
	static void stopChannel(int idx)
	{
		auto& ch = getChannel(idx);
		ch.looping = false;
		if (ch.active && ch.hWaveOut)
		{
			waveOutReset(ch.hWaveOut);
			waveOutUnprepareHeader(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
			waveOutClose(ch.hWaveOut);
			ch.hWaveOut = nullptr;
			ch.active = false;
		}
	}

	/// @brief waveOutコールバック（ループ再生用）
	static void CALLBACK waveOutProc(
		HWAVEOUT /*hwo*/, UINT msg,
		DWORD_PTR inst, DWORD_PTR /*p1*/, DWORD_PTR /*p2*/)
	{
		if (msg == WOM_DONE)
		{
			auto* ch = reinterpret_cast<Channel*>(inst);
			if (ch && ch->looping && ch->hWaveOut)
			{
				waveOutWrite(ch->hWaveOut, &ch->waveHdr, sizeof(WAVEHDR));
			}
		}
	}

	/// @brief ループ再生を開始する
	/// @param ch チャンネル
	/// @return 再生開始に成功したらtrue
	static bool startLoopPlayback(Channel& ch)
	{
		WAVEFORMATEX wfx = {};
		wfx.wFormatTag = WAVE_FORMAT_PCM;
		wfx.nChannels = 1;
		wfx.nSamplesPerSec = ch.loopSampleRate;
		wfx.wBitsPerSample = 16;
		wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
		wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

		MMRESULT result = waveOutOpen(&ch.hWaveOut, WAVE_MAPPER, &wfx,
			reinterpret_cast<DWORD_PTR>(waveOutProc),
			reinterpret_cast<DWORD_PTR>(&ch),
			CALLBACK_FUNCTION);
		if (result != MMSYSERR_NOERROR) return false;

		std::memset(&ch.waveHdr, 0, sizeof(WAVEHDR));
		ch.waveHdr.lpData = reinterpret_cast<LPSTR>(ch.loopData.data() + 44);
		ch.waveHdr.dwBufferLength = static_cast<DWORD>(ch.loopData.size() - 44);

		result = waveOutPrepareHeader(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR)
		{
			waveOutClose(ch.hWaveOut);
			ch.hWaveOut = nullptr;
			return false;
		}

		result = waveOutWrite(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR)
		{
			waveOutUnprepareHeader(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
			waveOutClose(ch.hWaveOut);
			ch.hWaveOut = nullptr;
			return false;
		}

		ch.active = true;
		return true;
	}

	/// @brief チャンネルでPCMを再生する
	/// @param idx チャンネルインデックス
	/// @param pcm PCMサンプルデータ
	/// @param sampleRate サンプルレート
	/// @return 再生開始に成功したらtrue
	static bool playOnChannel(int idx, const PcmBuffer& pcm, std::uint32_t sampleRate)
	{
		auto& ch = getChannel(idx);

		// WAVデータをメモリに生成する
		ch.wavData = WavWriter::toWav(pcm, sampleRate);

		// waveOut設定
		WAVEFORMATEX wfx = {};
		wfx.wFormatTag = WAVE_FORMAT_PCM;
		wfx.nChannels = 1;
		wfx.nSamplesPerSec = sampleRate;
		wfx.wBitsPerSample = 16;
		wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
		wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

		MMRESULT result = waveOutOpen(&ch.hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
		if (result != MMSYSERR_NOERROR)
		{
			return false;
		}

		// WAVヘッダーをスキップしてPCMデータ部分を指す（44バイトヘッダー）
		std::memset(&ch.waveHdr, 0, sizeof(WAVEHDR));
		ch.waveHdr.lpData = reinterpret_cast<LPSTR>(ch.wavData.data() + 44);
		ch.waveHdr.dwBufferLength = static_cast<DWORD>(ch.wavData.size() - 44);

		result = waveOutPrepareHeader(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR)
		{
			waveOutClose(ch.hWaveOut);
			ch.hWaveOut = nullptr;
			return false;
		}

		result = waveOutWrite(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR)
		{
			waveOutUnprepareHeader(ch.hWaveOut, &ch.waveHdr, sizeof(WAVEHDR));
			waveOutClose(ch.hWaveOut);
			ch.hWaveOut = nullptr;
			return false;
		}

		ch.active = true;
		return true;
	}
#endif
};

} // namespace mitiru_mml
