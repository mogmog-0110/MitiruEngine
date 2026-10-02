#pragma once

/// @file Invariant.hpp
/// @brief ゲーム側が GameMemory に対する不変条件を宣言する DSL (N1)。
/// `MITIRU_REWIND_BUDGET` と同じ「DLL が export する関数を host が GetProcAddress で
/// 探す」方式を踏襲する。ModuleApi 自体の ABI (POD レイアウト) は変えず別 export symbol
/// (mitiru_module_invariants) を追加するだけなので、宣言していない既存 game には
/// 一切影響しない (ModuleHost::invariantsFn() が nullptr を返すだけ)。
///
/// MITIRU_INVARIANT(GameType, 名前, 式) をグローバル scope・MITIRU_GAME と同じ .cpp に
/// 並べ、ファイル末尾で MITIRU_INVARIANTS_EXPORT() を 1 回だけ呼ぶ形で使う。式の中では
/// g (const GameType&) で GameMemory を参照でき、戻り値は「不変条件が成立しているか」
/// (true=成立、false=違反) を表す真偽値にする。host は Engine_Frame.hpp の update 後
/// フックで毎フレーム全件評価し、違反を mitiru::observe::OracleEvent として記録する。

#include <cstdint>
#include <type_traits>
#include <vector>

#include <mitiru/module/Reflection.hpp>  // detail::copyTag を流用する

namespace mitiru::module
{

/// @brief 1 件の不変条件記述子 (POD、DLL 境界を渡る)。
struct InvariantDescriptor
{
	char name[32];                    ///< 宣言時の名前、null 終端
	bool (*check)(const void* mem);   ///< true = 成立、false = 違反。mem は GameMemory 先頭
};

static_assert(std::is_trivially_copyable_v<InvariantDescriptor>,
	"InvariantDescriptor は POD (DLL 境界)");

namespace detail
{

/// @brief DLL-local な不変条件登録簿。宣言マクロが static init 時に push する。
inline std::vector<InvariantDescriptor>& invariantRegistry()
{
	static std::vector<InvariantDescriptor> registry;
	return registry;
}

inline void registerInvariant(const char* name, bool (*fn)(const void*))
{
	InvariantDescriptor d{};
	copyTag(d.name, sizeof(d.name), name);
	d.check = fn;
	invariantRegistry().push_back(d);
}

}  // namespace detail
}  // namespace mitiru::module

#if defined(_WIN32)
#  define MITIRU_INVARIANT_EXPORT __declspec(dllexport)
#else
#  define MITIRU_INVARIANT_EXPORT __attribute__((visibility("default")))
#endif

#define MITIRU_INVARIANT_CONCAT_IMPL(a, b) a##b
#define MITIRU_INVARIANT_CONCAT(a, b) MITIRU_INVARIANT_CONCAT_IMPL(a, b)

/// @brief GameType の GameMemory に対する不変条件を 1 件宣言する (optional、MITIRU_GAME と併記)。
/// グローバル scope・完全修飾名で書くこと。行番号で一意な static 登録オブジェクトを作る。
#define MITIRU_INVARIANT(GameType, name, expr)                                       \
	namespace                                                                          \
	{                                                                                   \
	struct MITIRU_INVARIANT_CONCAT(MitiruInvariantInit_, __LINE__)                     \
	{                                                                                   \
		static bool check(const void* mem) noexcept                                    \
		{                                                                               \
			const GameType& g = *static_cast<const GameType*>(mem);                    \
			return static_cast<bool>(expr);                                            \
		}                                                                               \
		MITIRU_INVARIANT_CONCAT(MitiruInvariantInit_, __LINE__)()                      \
		{                                                                               \
			::mitiru::module::detail::registerInvariant(name, &check);                 \
		}                                                                               \
	} MITIRU_INVARIANT_CONCAT(g_mitiruInvariantInit_, __LINE__);                       \
	}

namespace mitiru::module::detail
{

/// @brief DLL-local な MITIRU_REACHABLE の到達状態リセット登録簿。
inline std::vector<void (*)()>& invariantResetRegistry()
{
	static std::vector<void (*)()> registry;
	return registry;
}

inline void registerInvariantReset(void (*fn)())
{
	invariantResetRegistry().push_back(fn);
}

}  // namespace mitiru::module::detail

/// @brief GameType の GameMemory が指定フレーム数以内に一度でも expr を満たしたかを見張る (N7)。
/// 到達済みなら以後ずっと成立扱い (INVARIANT と同じ true=成立/false=違反の check シグネチャに
/// 乗せるため、既存の MITIRU_INVARIANT 登録・export・host 側 (Oracle.hpp checkInvariantsOracle)
/// をそのまま再利用でき、DLL 境界 (InvariantDescriptor の ABI) も host loader も一切変えずに済む。
/// maxFrames 到達前に一度も expr が真にならなければ、以後 check は false (違反) を返し続ける。
/// 到達状態は `elapsedCounter()`/`reachedFlag()` 経由で保持し、`reset()` を
/// `registerInvariantReset()` に登録して host がまとめて 0 に戻せるようにする。
#define MITIRU_REACHABLE(GameType, name, expr, maxFrames)                            \
	namespace                                                                          \
	{                                                                                   \
	struct MITIRU_INVARIANT_CONCAT(MitiruReachableInit_, __LINE__)                     \
	{                                                                                   \
		static std::uint32_t& elapsedCounter() noexcept                                \
		{                                                                               \
			static std::uint32_t s_elapsed = 0;                                        \
			return s_elapsed;                                                          \
		}                                                                               \
		static bool& reachedFlag() noexcept                                            \
		{                                                                               \
			static bool s_reached = false;                                            \
			return s_reached;                                                          \
		}                                                                               \
		static bool check(const void* mem) noexcept                                    \
		{                                                                               \
			const GameType& g = *static_cast<const GameType*>(mem);                    \
			if (!reachedFlag() && static_cast<bool>(expr)) { reachedFlag() = true; }    \
			if (reachedFlag()) { return true; }                                        \
			++elapsedCounter();                                                        \
			return elapsedCounter() <= static_cast<std::uint32_t>(maxFrames);          \
		}                                                                               \
		static void reset() noexcept                                                   \
		{                                                                               \
			elapsedCounter() = 0;                                                      \
			reachedFlag()    = false;                                                  \
		}                                                                               \
		MITIRU_INVARIANT_CONCAT(MitiruReachableInit_, __LINE__)()                      \
		{                                                                               \
			::mitiru::module::detail::registerInvariant(name, &check);                 \
			::mitiru::module::detail::registerInvariantReset(&reset);                  \
		}                                                                               \
	} MITIRU_INVARIANT_CONCAT(g_mitiruReachableInit_, __LINE__);                       \
	}

/// @brief 登録済みの全 MITIRU_INVARIANT を host が GetProcAddress で読める形に export する。
/// MITIRU_GAME と同じ .cpp の末尾に 1 回だけ書く (複数回書くと export の再定義エラーになる)。
/// 呼ばれるのは host の load 完了後 (GetProcAddress 経由) なので、静的初期化順には依存しない。
/// @brief MITIRU_REACHABLE の到達状態を全件 0 に戻す (optional export)。restart intent
/// (`hud.requestRestart()`) 適用時に host が GameMemory の再構築と併せて呼ぶ。
#define MITIRU_INVARIANTS_EXPORT()                                                    \
	extern "C" MITIRU_INVARIANT_EXPORT                                                 \
	const ::mitiru::module::InvariantDescriptor* mitiru_module_invariants(             \
		std::int32_t* outCount)                                                        \
	{                                                                                   \
		const auto& reg = ::mitiru::module::detail::invariantRegistry();               \
		outCount[0] = static_cast<std::int32_t>(reg.size());                           \
		return reg.data();                                                             \
	}                                                                                   \
	extern "C" MITIRU_INVARIANT_EXPORT void mitiru_module_invariants_reset()           \
	{                                                                                   \
		for (auto* fn : ::mitiru::module::detail::invariantResetRegistry())            \
		{                                                                               \
			fn();                                                                      \
		}                                                                               \
	}
