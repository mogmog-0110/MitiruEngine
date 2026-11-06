#pragma once

/// @file HostVideoExport.hpp
/// @brief --export-video: 毎フレームの PNG と音を途中のファイルに撮らせ、走り終えてから AVI にまとめる
/// @details 撮るのは --capture-dir / --audio-capture と同じ道で、まとめるのは mitiru_avi と同じ
///          video/CaptureAviExport.hpp。ゲームを走らせる側には何も足さない (読み取り専用の後処理)。
///          途中のファイルは成功したら消し、失敗したら原因を調べられるよう残す。

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/video/CaptureAviExport.hpp>

namespace mitiru::host
{

class HostVideoExport
{
public:
	/// 音の書き出しは 1 フレーム = 1/60 秒に固定されている (HostAudioCapture)
	static constexpr std::uint32_t kAudioStepsPerSecond = 60;
	static constexpr std::uint64_t kAudioStepSamples = 800;  // 48 kHz / 60

	explicit HostVideoExport(std::string outPath) : m_out(std::move(outPath)) {}

	[[nodiscard]] std::string framesDir() const { return m_out + ".frames"; }
	[[nodiscard]] std::string wavPath() const { return m_out + ".audio.wav"; }

	/// @brief 前回の失敗で残った途中のファイルを消す。残っていると、短い録画の後ろに前回の画像が連番で続いてしまう
	void clearLeftovers() const
	{
		std::error_code ec;
		std::filesystem::remove_all(framesDir(), ec);
		std::filesystem::remove(wavPath(), ec);
	}

	/// @brief PNG と WAV が書き終わってから呼ぶ。成功したら途中のファイルを消す
	/// @return 0 = 成功、5 = AVI を書けなかった
	[[nodiscard]] int finish(std::uint32_t fps)
	{
		namespace fs = std::filesystem;
		video::CaptureAviOptions opt;
		opt.framesDir = framesDir();
		opt.out = m_out;
		opt.fps = fps;
		// 音の区間は 1/60 秒単位で切ってあるので、別のフレームレートの画像とは並べられない
		const bool audioFits = fps == kAudioStepsPerSecond;
		if (audioFits) { opt.audioWav = wavPath(); }
		else { mitiru::console::noticef("フレームレートが %u なので、音は入れずに映像だけの AVI にします", fps); }

		const video::CaptureAviResult r = video::exportCaptureAvi(opt);
		if (!r.ok)
		{
			mitiru::console::noticef("AVI を書けませんでした (%s)、撮った画像と音は %s と %s に残しました",
			                         r.error.c_str(), framesDir().c_str(), wavPath().c_str());
			return 5;
		}
		mitiru::console::noticef("%s に %llu フレーム (%.2f 秒、%ux%u)、%llu byte を書きました",
		                         m_out.c_str(), static_cast<unsigned long long>(r.frames),
		                         static_cast<double>(r.frames) / fps, r.width, r.height,
		                         static_cast<unsigned long long>(r.bytes));
		// 最後の update は音だけ回るので 1 ステップ (800 サンプル) 長いのが普通。それ以上ずれたら知らせる
		if (r.audioPaddedSamples > 0 || r.audioDroppedSamples > kAudioStepSamples)
		{
			mitiru::console::noticef("音と画像の長さが合いません (無音で埋めた %llu、捨てた %llu サンプル)",
			                         static_cast<unsigned long long>(r.audioPaddedSamples),
			                         static_cast<unsigned long long>(r.audioDroppedSamples));
		}
		clearLeftovers();
		std::error_code ec;
		fs::remove(fs::path(wavPath()).replace_extension(".events.jsonl"), ec);
		return 0;
	}

private:
	std::string m_out;
};

}  // namespace mitiru::host
