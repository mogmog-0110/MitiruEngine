#pragma once

/// @file TracyIntegration.hpp
/// @brief Tracy Profiler ブリッジ
/// @details Tracy が使えるときは実際のプロファイリングマクロに展開し、
///          使えないときは no-op に展開する。エンジン全体の計測ポイントをここでまとめて定義する。
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
#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────
// マクロ定義: Tracy 有効時はリアルマクロ、無効時は no-op
// ─────────────────────────────────────────────────────────────

#ifdef MITIRU_HAS_TRACY

#include <tracy/Tracy.hpp>

/// @brief フレーム境界マーク（メインループ末尾で呼ぶ）
#define MITIRU_PROFILE_FRAME()              FrameMark

/// @brief 名前付きスコープ計測
#define MITIRU_PROFILE_SCOPE(name)          ZoneScopedN(name)

/// @brief 関数全体のスコープ計測（関数名は自動で付く）
#define MITIRU_PROFILE_FUNCTION()           ZoneScoped

/// @brief GPU 計測ゾーン
#define MITIRU_PROFILE_GPU(name)            TracyGpuZone(name)

/// @brief メモリ確保トラッキング
#define MITIRU_PROFILE_MEMORY(ptr, size)    TracyAlloc(ptr, size)

/// @brief メモリ解放トラッキング
#define MITIRU_PROFILE_FREE(ptr)            TracyFree(ptr)

/// @brief 現在のゾーンにテキストを付ける
/// @note text は null 終端の const char* でなければならない（strlen を使うため）
#define MITIRU_PROFILE_TEXT(text)            ZoneText(text, strlen(text))

/// @brief カスタムプロット値を送信する
#define MITIRU_PROFILE_VALUE(name, v)       TracyPlot(name, v)

/// @brief スレッド名を設定する
#define MITIRU_PROFILE_THREAD(name)         tracy::SetThreadName(name)

/// @brief メッセージをタイムラインに記録する
/// @note text は null 終端の const char* でなければならない（strlen を使うため）
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

/// @brief Tracy プロファイラのヘルパーユーティリティ
/// @details マクロを直接使わずに、C++ 関数経由でプロファイラを操作するための薄いラッパー。
///          Tracy 無効時はすべて no-op として動く。
class TracyHelper
{
public:
	/// @brief Tracy が使えるかどうかを返す
	/// @return MITIRU_HAS_TRACY 定義時は true、それ以外は false
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
	/// @param name プロット名（Tracy 上での表示名）
	/// @param value 値
	static void plotValue([[maybe_unused]] const char* name,
	                      [[maybe_unused]] double value) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyPlot(name, value);
#endif
	}

	/// @brief int64_t 値のプロット
	/// @param name プロット名
	/// @param value 値
	static void plotValue([[maybe_unused]] const char* name,
	                      [[maybe_unused]] std::int64_t value) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyPlot(name, value);
#endif
	}

	/// @brief メッセージを Tracy タイムラインに記録する
	/// @param text メッセージ文字列
	static void message([[maybe_unused]] std::string_view text) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyMessage(text.data(), text.size());
#endif
	}

	/// @brief 色付きメッセージを Tracy タイムラインに記録する
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
		// const_cast: TracyAlloc/TracyFree は void* を要求するが、
		// 記録するだけでポインタ先は変更しないので安全
		MITIRU_PROFILE_MEMORY(const_cast<void*>(ptr), size);
	}

	/// @brief メモリ解放をトラッキングする
	/// @param ptr 解放するポインタ
	static void trackFree([[maybe_unused]] const void* ptr) noexcept
	{
		// const_cast: TracyFree は void* を要求するが、アドレスを記録するだけで変更はしない
		MITIRU_PROFILE_FREE(const_cast<void*>(ptr));
	}

	/// @brief 7-1: `.mtrr` のフレーム番号を Tracy のタイムラインへ書き込む。
	/// @details Tracy とリプレイのフレーム ID は別の座標系で、突き合わせる手段が無かった
	///          (`docs/PROFILING_GUIDE.md` 参照)。`replay::Recorder::record()` から
	///          録画中だけ呼ばれる想定。Plot は数値グラフとして時系列に残り、Message は
	///          その瞬間のタイムラインへ 1 行のテキストとして記録される (どちらも Tracy 接続時
	///          だけ意味を持つ)。Tracy 無効ビルドでは no-op。
	static void tagMtrrFrame([[maybe_unused]] std::uint64_t mtrrFrameIdx) noexcept
	{
#ifdef MITIRU_HAS_TRACY
		TracyPlot("mtrr.frame", static_cast<std::int64_t>(mtrrFrameIdx));
		const std::string text = "mtrr=" + std::to_string(mtrrFrameIdx);
		TracyMessage(text.c_str(), text.size());
#endif
	}
};

} // namespace mitiru::debug
