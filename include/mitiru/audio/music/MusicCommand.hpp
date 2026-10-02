#pragma once

/// @file MusicCommand.hpp
/// @brief MusicDirector が決めた再生、停止、音量変更を MusicRenderer へ渡す形
/// @details 時刻は director の時計で、再生開始からのサンプル数を表す。RingBuffer で音声スレッドへ渡すため、trivially copyable に保つ。
///          音源の読み手である IStemReader は、director ではなく MiniaudioMusicPlayer やテストが StartInstance に設定する。

#include <array>
#include <cstddef>
#include <cstdint>

namespace mitiru::audio::music
{

/// @brief 音源 1 本を先頭から読み、renderer のチャンネル数でインターリーブした float を書いて、書けたフレーム数を返す。
/// @details 戻り値が frames より小さいときは、そこで音源が終わったとみなす。
class IStemReader
{
public:
	virtual ~IStemReader() = default;
	virtual std::uint64_t read(float* out, std::uint64_t frames) = 0;
};

enum class MusicCommandKind : std::uint8_t
{
	StartInstance,  ///< 区間 (かスティンガー) を at から鳴らす。length > 0 なら 0 から gain へフェードイン
	StopInstance,   ///< instance を at から length かけて消す (0 なら at で切る)
	LayerGain,      ///< 層 layer の音量を at から length かけて gain へ
	MasterGain,     ///< 曲全体の音量を at から length かけて gain へ
};

struct MusicStemVoice
{
	IStemReader* reader = nullptr;
	std::int16_t layer = -1;   ///< -1 = 層に属さない (層の音量を掛けない)
	float        gain = 1.0f;
};

constexpr std::size_t kMaxStemsPerInstance = 8;

struct MusicCommand
{
	std::int64_t     at = 0;
	std::int64_t     length = 0;
	std::uint32_t    instance = 0;
	std::int16_t     segment = -1;  ///< StartInstance: 区間の番号 (スティンガーなら -1)
	std::int16_t     stinger = -1;  ///< StartInstance: スティンガーの番号 (区間なら -1)
	std::int16_t     layer = -1;    ///< LayerGain の層
	MusicCommandKind kind = MusicCommandKind::StartInstance;
	float            gain = 1.0f;
	std::uint8_t     stemCount = 0;
	std::array<MusicStemVoice, kMaxStemsPerInstance> stems{};
};

/// @brief 1 回の MusicDirector::advance で出す命令を保持する。メモリを確保しないよう固定長にする。
struct MusicCommandBuffer
{
	static constexpr std::size_t kCapacity = 64;

	std::array<MusicCommand, kCapacity> items{};
	std::size_t count = 0;
	bool        overflowed = false;  ///< 入り切らずに捨てた命令があった

	void clear() noexcept { count = 0; overflowed = false; }
	void push(const MusicCommand& c) noexcept
	{
		if (count < kCapacity) { items[count++] = c; }
		else                   { overflowed = true; }
	}
	[[nodiscard]] const MusicCommand* begin() const noexcept { return items.data(); }
	[[nodiscard]] const MusicCommand* end() const noexcept { return items.data() + count; }
};

}  // namespace mitiru::audio::music
