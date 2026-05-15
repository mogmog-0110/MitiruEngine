#pragma once

/// @file IAudioOutput.hpp
/// @brief PCMオーディオ出力の抽象インターフェース
/// @details プラットフォーム固有のオーディオ出力バックエンドを抽象化する。
///          IAudioEngineがゲームレベルの音声制御を提供するのに対し、
///          IAudioOutputは低レベルのPCMサンプル書き込みを担当する。

#include <cstddef>
#include <cstdint>

namespace mitiru::audio
{

/// @brief オーディオ出力の設定パラメータ
struct AudioOutputParams
{
	int sampleRate = 44100;     ///< サンプルレート (Hz)
	int channels = 2;           ///< チャンネル数
	int bufferSize = 4096;      ///< バッファサイズ（サンプル数）
};

/// @brief PCMオーディオ出力の抽象インターフェース
/// @details PulseAudio、WASAPI、CoreAudio等のバックエンドがこのインターフェースを実装する。
///          float PCMサンプル [-1.0, 1.0] を受け付けてオーディオデバイスに出力する。
///
/// @code
/// auto output = mitiru::audio::createAudioOutput();
/// mitiru::audio::AudioOutputParams params;
/// params.sampleRate = 44100;
/// params.channels = 2;
/// params.bufferSize = 4096;
///
/// if (output->initialize(params.sampleRate, params.channels, params.bufferSize))
/// {
///     std::vector<float> samples(4096, 0.0f);
///     output->write(samples.data(), samples.size());
/// }
/// output->shutdown();
/// @endcode
class IAudioOutput
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAudioOutput() = default;

	/// @brief オーディオ出力を初期化する
	/// @param sampleRate サンプルレート (Hz)
	/// @param channels チャンネル数
	/// @param bufferSize バッファサイズ（サンプル数）
	/// @return 成功した場合 true
	virtual bool initialize(int sampleRate, int channels, int bufferSize) = 0;

	/// @brief PCMサンプルを書き込む
	/// @param samples float PCMサンプル配列 [-1.0, 1.0]
	/// @param count サンプル数（全チャンネル合計）
	/// @return 成功した場合 true
	virtual bool write(const float* samples, std::size_t count) = 0;

	/// @brief 再生中かどうかを判定する
	/// @return 再生中なら true
	[[nodiscard]] virtual bool isPlaying() const = 0;

	/// @brief オーディオ出力をシャットダウンする
	virtual void shutdown() = 0;

	/// @brief 初期化済みかどうかを判定する
	/// @return 初期化済みなら true
	[[nodiscard]] virtual bool isInitialized() const = 0;

	/// @brief 現在のサンプルレートを取得する
	/// @return サンプルレート (Hz)
	[[nodiscard]] virtual int sampleRate() const = 0;

	/// @brief 現在のチャンネル数を取得する
	/// @return チャンネル数
	[[nodiscard]] virtual int channels() const = 0;

	/// @brief 現在のバッファサイズを取得する
	/// @return バッファサイズ（サンプル数）
	[[nodiscard]] virtual int bufferSize() const = 0;

	/// @brief バックエンド名を取得する
	/// @return バックエンド識別文字列
	[[nodiscard]] virtual const char* backendName() const = 0;
};

} // namespace mitiru::audio
