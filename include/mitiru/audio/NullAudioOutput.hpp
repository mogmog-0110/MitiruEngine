#pragma once

/// @file NullAudioOutput.hpp
/// @brief ヌルオーディオ出力バックエンド
/// @details ヘッドレスモードやテスト時に使用する、何も出力しないIAudioOutput実装。
///          全操作がノーオペレーションで、安全にどのプラットフォームでも使用可能。

#include <mitiru/audio/IAudioOutput.hpp>

#include <algorithm>
#include <cstddef>

namespace mitiru::audio
{

/// @brief ヌルオーディオ出力バックエンド
/// @details 実際のオーディオ出力は行わないが、状態を正確に追跡する。
///          PulseAudioが利用不可な環境やユニットテストで使用する。
///
/// @code
/// mitiru::audio::NullAudioOutput output;
/// output.initialize(44100, 2, 4096);
/// assert(output.isInitialized());
/// assert(!output.isPlaying());
///
/// std::vector<float> silence(4096, 0.0f);
/// output.write(silence.data(), silence.size()); // 何も出力しない
/// @endcode
class NullAudioOutput : public IAudioOutput
{
public:
	/// @brief デフォルトコンストラクタ
	NullAudioOutput() noexcept = default;

	/// @brief デストラクタ（自動シャットダウン）
	~NullAudioOutput() override
	{
		shutdown();
	}

	/// @brief コピー禁止
	NullAudioOutput(const NullAudioOutput&) = delete;
	/// @brief コピー代入禁止
	NullAudioOutput& operator=(const NullAudioOutput&) = delete;
	/// @brief ムーブコンストラクタ
	NullAudioOutput(NullAudioOutput&&) noexcept = default;
	/// @brief ムーブ代入演算子
	NullAudioOutput& operator=(NullAudioOutput&&) noexcept = default;

	/// @brief オーディオ出力を初期化する（状態のみ更新）
	/// @param sr サンプルレート (Hz)
	/// @param ch チャンネル数
	/// @param bs バッファサイズ（サンプル数）
	/// @return 有効なパラメータなら true
	bool initialize(int sr, int ch, int bs) override
	{
		if (m_initialized) return false;
		if (sr <= 0 || ch <= 0 || bs <= 0) return false;

		m_sampleRate = sr;
		m_channels = ch;
		m_bufferSize = bs;
		m_initialized = true;
		return true;
	}

	/// @brief PCMサンプルを書き込む（ノーオペレーション）
	/// @param samples サンプル配列（無視される）
	/// @param count サンプル数
	/// @return 初期化済みかつ有効なパラメータなら true
	bool write(const float* samples, std::size_t count) override
	{
		if (!m_initialized) return false;
		if (samples == nullptr || count == 0) return false;

		m_totalSamplesWritten += count;
		m_writeCallCount++;
		return true;
	}

	/// @brief 再生中かどうか（常に false）
	/// @return 常に false（実際の出力なし）
	[[nodiscard]] bool isPlaying() const override
	{
		return false;
	}

	/// @brief シャットダウンする
	void shutdown() override
	{
		m_initialized = false;
	}

	/// @brief 初期化済みかどうか
	/// @return 初期化済みなら true
	[[nodiscard]] bool isInitialized() const override
	{
		return m_initialized;
	}

	/// @brief サンプルレートを取得する
	/// @return サンプルレート (Hz)
	[[nodiscard]] int sampleRate() const override
	{
		return m_sampleRate;
	}

	/// @brief チャンネル数を取得する
	/// @return チャンネル数
	[[nodiscard]] int channels() const override
	{
		return m_channels;
	}

	/// @brief バッファサイズを取得する
	/// @return バッファサイズ
	[[nodiscard]] int bufferSize() const override
	{
		return m_bufferSize;
	}

	/// @brief バックエンド名を取得する
	/// @return "Null"
	[[nodiscard]] const char* backendName() const override
	{
		return "Null";
	}

	// ── テスト・デバッグ用クエリ ──────────────────────────

	/// @brief 書き込まれた合計サンプル数を取得する
	/// @return 合計サンプル数
	[[nodiscard]] std::size_t totalSamplesWritten() const noexcept
	{
		return m_totalSamplesWritten;
	}

	/// @brief write()の呼び出し回数を取得する
	/// @return 呼び出し回数
	[[nodiscard]] std::size_t writeCallCount() const noexcept
	{
		return m_writeCallCount;
	}

	/// @brief カウンタをリセットする
	void resetCounters() noexcept
	{
		m_totalSamplesWritten = 0;
		m_writeCallCount = 0;
	}

private:
	int m_sampleRate = 0;                       ///< サンプルレート
	int m_channels = 0;                         ///< チャンネル数
	int m_bufferSize = 0;                       ///< バッファサイズ
	bool m_initialized = false;                 ///< 初期化済みフラグ
	std::size_t m_totalSamplesWritten = 0;      ///< 書き込み合計サンプル数
	std::size_t m_writeCallCount = 0;           ///< write呼び出し回数
};

} // namespace mitiru::audio
