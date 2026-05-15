#pragma once

/// @file TracyIntegration.hpp
/// @brief Tracy Profilerブリッジ
/// @details Tracyが利用可能な場合はリアルプロファイリングマクロに展開し、
///          利用不可の場合はno-opに展開する。エンジン全体の計測ポイントを統一的に定義。
///
/// @par 計測ポイント（エンジンへの組込み箇所）
/// - Engine::run メインループ末尾 → MITIRU_PROFILE_FRAME()
/// - Screen::present → MITIRU_PROFILE_SCOPE("Render")
/// - PhysicsWorld::update → MITIRU_PROFILE_SCOPE("Physics")
/// - AudioMixer::update → MITIRU_PROFILE_SCOPE("Audio")
///
/// @code
/// void Engine::run(Game& game, const EngineConfig& config) {
///     // ... メインループ内 ...
///     game.update(dt);
///     MITIRU_PROFILE_SCOPE("Update");
///     // ... 描画 ...
///     MITIRU_PROFILE_FRAME();
/// }
/// @endcode

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// ─────────────────────────────────────────────────────────────
// マクロ定義: Tracy有効時はリアルマクロ、無効時はno-op
// ─────────────────────────────────────────────────────────────

#ifdef MITIRU_HAS_TRACY

#include <tracy/Tracy.hpp>

/// @brief フレーム境界マーク（メインループ末尾で呼ぶ）
#define MITIRU_PROFILE_FRAME()              FrameMark

/// @brief 名前付きスコープ計測
#define MITIRU_PROFILE_SCOPE(name)          ZoneScopedN(name)

/// @brief 関数全体のスコープ計測（関数名が自動付与）
#define MITIRU_PROFILE_FUNCTION()           ZoneScoped

/// @brief GPU計測ゾーン
#define MITIRU_PROFILE_GPU(name)            TracyGpuZone(name)

/// @brief メモリ確保トラッキング
#define MITIRU_PROFILE_MEMORY(ptr, size)    TracyAlloc(ptr, size)

/// @brief メモリ解放トラッキング
#define MITIRU_PROFILE_FREE(ptr)            TracyFree(ptr)

/// @brief 現在のゾーンにテキストを付与する
/// @note textはnull終端のconst char*でなければならない（strlen使用のため）
#define MITIRU_PROFILE_TEXT(text)            ZoneText(text, strlen(text))

/// @brief カスタムプロット値を送信する
#define MITIRU_PROFILE_VALUE(name, v)       TracyPlot(name, v)

/// @brief スレッド名を設定する
#define MITIRU_PROFILE_THREAD(name)         tracy::SetThreadName(name)

/// @brief メッセージをタイムラインに記録する
/// @note textはnull終端のconst char*でなければならない（strlen使用のため）
#define MITIRU_PROFILE_MESSAGE(text)        TracyMessage(text, strlen(text))

#else // !MITIRU_HAS_TRACY

#define MITIRU_PROFILE_FRAME()              ((void)0)
#define MITIRU_PROFILE_SCOPE(name)          ((void)0)
#define MITIRU_PROFILE_FUNCTION()           ((void)0)
#define MITIRU_PROFILE_GPU(name)            ((void)0)
#define MITIRU_PROFILE_MEMORY(ptr, size)    ((void)0)
#define MITIRU_PROFILE_FREE(ptr)            ((void)0)
#define MITIRU_PROFILE_TEXT(text)            ((void)0)
#define MITIRU_PROFILE_VALUE(name, v)       ((void)0)
#define MITIRU_PROFILE_THREAD(name)         ((void)0)
#define MITIRU_PROFILE_MESSAGE(text)        ((void)0)

#endif // MITIRU_HAS_TRACY

namespace mitiru::debug
{

/// @brief Tracyプロファイラのヘルパーユーティリティ
/// @details マクロを直接使わず、C++関数経由でプロファイラ操作を行うための薄いラッパー。
///          Tracy無効時はすべてno-opとして動作する。
class TracyHelper
{
public:
	/// @brief Tracyが利用可能かどうかを返す
	/// @return MITIRU_HAS_TRACY定義時はtrue、それ以外はfalse
	[[nodiscard]] static constexpr bool isAvailable() noexcept
	{
#ifdef MITIRU_HAS_TRACY
		return true;
#else
		return false;
#endif
	}

	/// @brief フレーム境界をマークする（メインループ末尾で呼ぶ）
	static void markFrame() noexcept
	{
		MITIRU_PROFILE_FRAME();
	}

	/// @brief カスタムプロット値を送信する
	/// @param name プロット名（Tracy上での表示名）
	/// @param value 値
	static void plotValue([[maybe_unused]] const char* name,
	                      [[maybe_unused]] double value) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyPlot(name, value);
#endif
	}

	/// @brief int64_t値のプロット
	/// @param name プロット名
	/// @param value 値
	static void plotValue([[maybe_unused]] const char* name,
	                      [[maybe_unused]] std::int64_t value) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyPlot(name, value);
#endif
	}

	/// @brief メッセージをTracyタイムラインに記録する
	/// @param text メッセージ文字列
	static void message([[maybe_unused]] std::string_view text) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyMessage(text.data(), text.size());
#endif
	}

	/// @brief 色付きメッセージをTracyタイムラインに記録する
	/// @param text メッセージ文字列
	/// @param color 色（0xRRGGBB）
	static void messageColor([[maybe_unused]] std::string_view text,
	                         [[maybe_unused]] std::uint32_t color) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyMessageC(text.data(), text.size(), color);
#endif
	}

	/// @brief スレッド名を設定する
	/// @param name スレッド名
	static void setThreadName([[maybe_unused]] const char* name) noexcept
	{
		MITIRU_PROFILE_THREAD(name);
	}

	/// @brief メモリ確保をトラッキングする
	/// @param ptr 確保したポインタ
	/// @param size サイズ（バイト）
	static void trackAlloc([[maybe_unused]] const void* ptr,
	                       [[maybe_unused]] std::size_t size) noexcept
	{
		// const_cast: TracyAlloc/TracyFreeはvoid*を要求するが、
		// トラッキング目的のみでポインタ先を変更しないため安全
		MITIRU_PROFILE_MEMORY(const_cast<void*>(ptr), size);
	}

	/// @brief メモリ解放をトラッキングする
	/// @param ptr 解放するポインタ
	static void trackFree([[maybe_unused]] const void* ptr) noexcept
	{
		// const_cast: TracyFreeはvoid*を要求するが、アドレス記録のみで変更なし
		MITIRU_PROFILE_FREE(const_cast<void*>(ptr));
	}
};

} // namespace mitiru::debug
