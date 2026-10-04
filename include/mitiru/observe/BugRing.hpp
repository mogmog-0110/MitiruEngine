#pragma once

/// @file BugRing.hpp
/// @brief 常時バグリング (P1)。直近の入力を毎フレーム積み、F11 や GPU 喪失のときに bug_<unix ms>.mtrr へ書き出す。
/// @details 状態は kBugRingKeyframeEvery フレームごとの keyframe だけを持つ。keyframe は GameMemory の後ろに
/// GameMemory の外に持つ状態の image (ADR 0054、observe/SideStateImage.hpp の形) を続けた bytes で、
/// .msav の末尾や replay の state blob と同じ並びにしてある。書き出すときは入力の窓に残る一番古い keyframe から
/// 始めるので、窓の頭の最大 kBugRingKeyframeEvery - 1 フレームは書き出さない。
/// 巻き戻し・ロード・ホットリロードで live の状態が飛んだときは restartBugRing で積んだ分を捨てる。飛ぶ前の入力を
/// 飛んだ後の状態へ続けて再生すると、最初のフレームから食い違うため。

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/observe/GameMemoryRing.hpp>
#include <mitiru/replay/Recorder.hpp>

namespace mitiru::observe
{

inline constexpr std::uint64_t kBugRingKeyframeEvery = 60;

struct BugRingKeyframe
{
	std::uint64_t             seq = 0;       ///< 何番目に積んだフレームの on_update の後か
	bool                      valid = false; ///< 窓口の image まで取れたか
	std::vector<std::uint8_t> state;         ///< GameMemory + 窓口の image
};

struct BugRingState
{
	GameMemoryRing               inputRing;
	std::vector<BugRingKeyframe> keyframes;   ///< 古いものから上書きする
	std::uint64_t                pushed = 0;  ///< restartBugRing から数えて積んだフレームの数
	std::uint32_t                memSize = 0;
};

namespace detail
{

inline std::unordered_map<const void*, BugRingState>& bugRingRegistry()
{
	static std::unordered_map<const void*, BugRingState> reg;
	return reg;
}

/// @brief 入力の窓 (古い方から pushed - size) に残る一番古い有効な keyframe。無ければ nullptr。
[[nodiscard]] inline const BugRingKeyframe* oldestBugRingKeyframe(const BugRingState& st) noexcept
{
	const std::uint64_t oldest = st.pushed - st.inputRing.size();
	const BugRingKeyframe* best = nullptr;
	for (const BugRingKeyframe& k : st.keyframes)
	{
		if (!k.valid || k.seq < oldest || k.seq >= st.pushed) { continue; }
		if (best == nullptr || k.seq < best->seq) { best = &k; }
	}
	return best;
}

}  // namespace detail

/// @brief engineKey には Engine* を void* にした値を使う。
[[nodiscard]] inline BugRingState& bugRingFor(const void* engineKey)
{
	return detail::bugRingRegistry()[engineKey];
}

/// @brief Engine を壊すときに呼ぶ。入力の窓 (既定 30 秒で約 15 MB) を手放す。
inline void dropBugRing(const void* engineKey) noexcept
{
	detail::bugRingRegistry().erase(engineKey);
}

/// @brief 積んだ入力と keyframe を捨て、次に積むフレームを keyframe にする。
inline void restartBugRing(const void* engineKey) noexcept
{
	auto it = detail::bugRingRegistry().find(engineKey);
	if (it == detail::bugRingRegistry().end()) { return; }
	it->second.inputRing.clear();
	for (BugRingKeyframe& k : it->second.keyframes) { k.valid = false; }
	it->second.pushed = 0;
}

/// @brief on_update を 1 回終えたフレームを積む。keyframe にするフレームだけ appendSide(state) を呼び、
///        GameMemory の後ろへ窓口の image を足させる。appendSide が false を返した keyframe は使わない。
/// @param frames 入力を持つフレーム数 (bugRingSeconds を 60fps でフレーム数にした値)
template <class AppendSide>
inline void pushBugRingFrame(const void* engineKey, const void* mem, std::uint32_t memSize,
	const void* inputBytes, std::uint32_t inputSize, std::uint32_t frames, AppendSide&& appendSide)
{
	if (frames == 0 || mem == nullptr || memSize == 0 || inputBytes == nullptr || inputSize == 0) { return; }
	BugRingState& st = bugRingFor(engineKey);
	if (st.memSize != memSize || st.inputRing.frameSize() != inputSize || st.inputRing.capacity() != frames)
	{
		st.inputRing.configure(inputSize, frames);
		st.keyframes.assign(static_cast<std::size_t>(frames / kBugRingKeyframeEvery) + 2, BugRingKeyframe{});
		st.memSize = memSize;
		st.pushed = 0;
	}
	if (st.pushed % kBugRingKeyframeEvery == 0)
	{
		BugRingKeyframe& k = st.keyframes[static_cast<std::size_t>(st.pushed / kBugRingKeyframeEvery) % st.keyframes.size()];
		const auto* p = static_cast<const std::uint8_t*>(mem);
		k.seq = st.pushed;
		k.state.assign(p, p + memSize);
		k.valid = appendSide(k.state);
	}
	st.inputRing.push(inputBytes, inputSize);
	++st.pushed;
}

/// @brief GameMemory だけの game 用 (窓口の image を足さない)。
inline void pushBugRingFrame(const void* engineKey, const void* mem, std::uint32_t memSize,
	const void* inputBytes, std::uint32_t inputSize, std::uint32_t frames)
{
	pushBugRingFrame(engineKey, mem, memSize, inputBytes, inputSize, frames,
		[](std::vector<std::uint8_t>&) { return true; });
}

/// @brief リングを `<pathPrefix><unix ms>.mtrr` に保存する。savedPath があれば書いたファイル名を入れる
/// @details frame 0 の state blob に keyframe を入れる。keyframe はそのフレームの入力を適用した後の状態なので、
/// apps/mitiru_host/main.cpp は keyframe を書き戻し、frame 0 の入力は使わずに frame 1 から再生する。
[[nodiscard]] inline bool saveBugRing(const void* engineKey, const std::string& pathPrefix = "bug_",
	std::string* savedPath = nullptr)
{
	auto it = detail::bugRingRegistry().find(engineKey);
	if (it == detail::bugRingRegistry().end()) { return false; }
	const BugRingState& st = it->second;
	if (st.inputRing.size() == 0 || st.inputRing.frameSize() != sizeof(module::InputSnapshot)) { return false; }
	const BugRingKeyframe* key = detail::oldestBugRingKeyframe(st);
	if (key == nullptr) { return false; }

	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const std::string path = pathPrefix + std::to_string(nowMs) + ".mtrr";
	replay::Recorder rec;
	if (!rec.open(path)) { return false; }
	// frame 0 が post-update のキーフレームであることを host が見分ける印 (ファイル名に依らない)
	(void)rec.writeEnvTag("bugring|");

	const std::uint64_t n = st.pushed - key->seq;
	for (std::uint64_t i = 0; i < n; ++i)
	{
		const std::uint8_t* snap = st.inputRing.at(static_cast<std::size_t>(n - 1 - i));  // 古い順に書き出す
		if (snap == nullptr) { continue; }
		module::InputSnapshot input{};
		std::memcpy(&input, snap, sizeof(module::InputSnapshot));
		const bool first = (i == 0);
		rec.record(static_cast<std::uint32_t>(i), input, first ? key->state.data() : nullptr,
			first ? static_cast<std::uint32_t>(key->state.size()) : 0u);
	}
	rec.close();
	if (savedPath != nullptr) { *savedPath = path; }
	return true;
}

}  // namespace mitiru::observe
