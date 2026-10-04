#pragma once

/// @file SideState.hpp
/// @brief GameMemory の外に持つ状態を host へ預ける窓口の、game DLL 側の書き方 (ADR 0054)。
/// @details 物理エンジンの world のように flat POD に置けない状態は、保存 (bytes へ書く) と復元
/// (bytes から戻す) の 2 関数を持つ物として申告する。host はそれを GameMemory と同じ時点で記録し、
/// 巻き戻し・分岐・セーブ・ホットリロードで GameMemory と一緒に戻す。
///
/// 預ける物は saveState と restoreState を持つ。saveState は dst へ書いて必要な byte 数を返し、
/// cap が足りなければ書かずに必要量だけ返す。restoreState は戻せたら true を返す。どちらも先頭に
/// GameMemory の pointer を取る形でもよい。stateHash() があれば replay の照合はそれを使う。
/// 物を static に置き、ファイルスコープで MITIRU_SIDE_STATE("名前", 形の番号, 物) と書く。

#include <concepts>
#include <cstdint>
#include <cstring>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module
{

namespace detail
{

/// @brief この DLL が申告した窓口の表。`MITIRU_SIDE_STATE` が DLL の読み込み時に積み、
///        host は `mitiru_module_side_states` で写し取る。
struct SideStateRegistry
{
	SideStateChannel channels[kMaxSideStateChannels]{};
	std::int32_t     count = 0;
};

inline SideStateRegistry& sideStateRegistry() noexcept
{
	static SideStateRegistry registry;
	return registry;
}

inline bool sameSideStateName(const char* a, const char* b) noexcept
{
	return std::strncmp(a, b, sizeof(SideStateChannel::name)) == 0;
}

/// @return 積めたら true。同名は申告の誤りなので積まずに知らせる (後勝ちにすると片方が黙って消える)。
inline bool registerSideStateChannel(const SideStateChannel& channel) noexcept
{
	SideStateRegistry& r = sideStateRegistry();
	for (std::int32_t i = 0; i < r.count; ++i)
	{
		if (sameSideStateName(r.channels[i].name, channel.name))
		{
			console::noticef("MITIRU_SIDE_STATE の \"%s\" が 2 回書かれているので、2 回目は使いません。"
			                 "名前が重ならないようにしてください。", channel.name);
			return false;
		}
	}
	if (r.count >= kMaxSideStateChannels)
	{
		console::noticef("MITIRU_SIDE_STATE は %d 個までなので、\"%s\" は使いません。", kMaxSideStateChannels, channel.name);
		return false;
	}
	r.channels[r.count++] = channel;
	return true;
}

/// @brief 申告済みの窓口を out へ最大 cap 個写し、全件数を返す (`mitiru_module_side_states` の中身)。
inline std::int32_t copySideStates(SideStateChannel* out, std::int32_t cap) noexcept
{
	const SideStateRegistry& r = sideStateRegistry();
	if (out == nullptr || cap <= 0) { return r.count; }
	const std::int32_t n = (r.count < cap) ? r.count : cap;
	for (std::int32_t i = 0; i < n; ++i) { out[i] = r.channels[i]; }
	return r.count;
}

template<class C>
std::uint64_t sideSaveTrampoline(void* ctx, const void* memory, void* dst, std::uint64_t cap) noexcept
{
	// 例外を DLL 境界の外へ出さない。0 は「何も書けない」で、host は記録できないことを知らせる。
	try
	{
		C& c = *static_cast<C*>(ctx);
		if constexpr (requires { c.saveState(memory, dst, cap); }) { return c.saveState(memory, dst, cap); }
		else { (void)memory; return c.saveState(dst, cap); }
	}
	catch (...) { return 0; }
}

template<class C>
std::int32_t sideRestoreTrampoline(void* ctx, void* memory, const void* src, std::uint64_t size) noexcept
{
	try
	{
		C& c = *static_cast<C*>(ctx);
		bool ok = false;
		if constexpr (requires { c.restoreState(memory, src, size); }) { ok = c.restoreState(memory, src, size); }
		else { (void)memory; ok = c.restoreState(src, size); }
		return ok ? 1 : 0;
	}
	catch (...) { return 0; }
}

template<class C>
std::uint64_t sideHashTrampoline(void* ctx, const void* /*memory*/) noexcept
{
	try { return static_cast<const C*>(ctx)->stateHash(); }
	catch (...) { return 0; }
}

/// @brief ファイルスコープの static として置き、DLL の読み込み時に窓口を積む。
struct SideStateRegistrar
{
	explicit SideStateRegistrar(const SideStateChannel& channel) noexcept { (void)registerSideStateChannel(channel); }
};

}  // namespace detail

/// @brief C++ の物 obj を、保存と復元の C 関数の組に包む。obj は DLL が終わるまで生きていること。
template<class C>
[[nodiscard]] SideStateChannel makeSideStateChannel(const char* name, std::uint32_t version, C* obj,
                                                    std::uint32_t flags = 0) noexcept
{
	static_assert(requires(C& c, void* d, std::uint64_t n) { c.saveState(d, n); }
	           || requires(C& c, const void* m, void* d, std::uint64_t n) { c.saveState(m, d, n); },
		"MITIRU_SIDE_STATE: std::uint64_t saveState(void* dst, std::uint64_t cap) が必要です");
	static_assert(requires(C& c, const void* s, std::uint64_t n) { c.restoreState(s, n); }
	           || requires(C& c, void* m, const void* s, std::uint64_t n) { c.restoreState(m, s, n); },
		"MITIRU_SIDE_STATE: bool restoreState(const void* src, std::uint64_t size) が必要です");

	SideStateChannel ch{};
	if (name != nullptr) { std::strncpy(ch.name, name, sizeof(ch.name) - 1); }
	ch.version = version;
	ch.flags   = flags;
	ch.ctx     = obj;
	ch.save    = &detail::sideSaveTrampoline<C>;
	ch.restore = &detail::sideRestoreTrampoline<C>;
	if constexpr (requires(const C& c) { { c.stateHash() } -> std::convertible_to<std::uint64_t>; })
	{
		ch.hash = &detail::sideHashTrampoline<C>;
	}
	return ch;
}

}  // namespace mitiru::module

#if defined(_WIN32)
#  define MITIRU_SIDE_STATE_EXPORT __declspec(dllexport)
#else
#  define MITIRU_SIDE_STATE_EXPORT __attribute__((visibility("default"), used))
#endif

// host が GetProcAddress で引く窓口の表。inline なので、この header を読む翻訳単位が幾つあっても
// DLL に 1 つだけ残る。窓口を 1 つも申告しない DLL では 0 を返す。
extern "C" MITIRU_SIDE_STATE_EXPORT inline std::int32_t mitiru_module_side_states(
	::mitiru::module::SideStateChannel* out, std::int32_t cap) noexcept
{
	return ::mitiru::module::detail::copySideStates(out, cap);
}

#define MITIRU_SIDE_STATE_CAT_(a, b) a##b
#define MITIRU_SIDE_STATE_CAT(a, b)  MITIRU_SIDE_STATE_CAT_(a, b)

/// GameMemory の外に持つ状態 object を、名前 name・形の番号 version で host へ預ける。
/// ファイルスコープに書く。object は static (DLL が終わるまで生きる物) にする。
#define MITIRU_SIDE_STATE(name, version, object)                                                  \
	static const ::mitiru::module::detail::SideStateRegistrar                                       \
		MITIRU_SIDE_STATE_CAT(_mitiruSideState_, __LINE__){                                          \
			::mitiru::module::makeSideStateChannel((name), (version), &(object))}
