#pragma once

/// @file TracyZones.hpp
/// @brief Tracy Profilerゾーンマクロ
/// @details カテゴリ別の色付きゾーンマクロを提供する。
///          MITIRU_HAS_TRACY定義時はTracyゾーンに展開し、
///          未定義時はno-opに展開する。
///
/// @par TracyIntegration.hppとの違い
/// TracyIntegration.hppはエンジン計測ポイント（MITIRU_PROFILE_*）を定義する。
/// TracyZones.hppはカテゴリ色付きの名前付きゾーンとフレームマーカーを定義する。
///
/// @par カテゴリ色
/// - Render:  青    (0x4444FF)
/// - Physics: 緑    (0x44FF44)
/// - Audio:   黄    (0xFFFF44)
/// - Script:  紫    (0xAA44FF)
/// - UI:      橙    (0xFF8844)

#include <cstdint>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// カテゴリ色定数
// ─────────────────────────────────────────────────────────────

namespace mitiru::debug
{

/// @brief Tracyゾーン用のカテゴリ色
struct ZoneColors
{
    static constexpr std::uint32_t Render  = 0x4444FF;
    static constexpr std::uint32_t Physics = 0x44FF44;
    static constexpr std::uint32_t Audio   = 0xFFFF44;
    static constexpr std::uint32_t Script  = 0xAA44FF;
    static constexpr std::uint32_t UI      = 0xFF8844;
    static constexpr std::uint32_t General = 0xCCCCCC;
};

} // namespace mitiru::debug

// ─────────────────────────────────────────────────────────────
// マクロ定義: Tracy有効時はリアルマクロ、無効時はno-op
// ─────────────────────────────────────────────────────────────

#ifdef MITIRU_HAS_TRACY

#include <tracy/Tracy.hpp>

/// @brief 匿名ゾーン（関数名を自動取得）
#define MITIRU_ZONE                         ZoneScoped

/// @brief 名前付きゾーン
/// @param name const char* リテラル
#define MITIRU_ZONE_NAMED(name)             ZoneScopedN(name)

/// @brief 関数全体のスコープゾーン
#define MITIRU_ZONE_SCOPED                  ZoneScoped

/// @brief 色付き名前付きゾーン
/// @param name const char* リテラル
/// @param color 0xRRGGBB色値
#define MITIRU_ZONE_COLOR(name, color)      ZoneScopedNC(name, color)

/// @brief フレーム境界マーカー
#define MITIRU_FRAME_MARK                   FrameMark

/// @brief 数値プロット
/// @param name プロット名
/// @param value 数値
#define MITIRU_PLOT(name, value)            TracyPlot(name, value)

/// @brief メッセージ記録
/// @param text const char* メッセージ文字列
#define MITIRU_MESSAGE(text)                TracyMessage(text, strlen(text))

// ── カテゴリ別ショートカット ────────────────────────────────

/// @brief Render用ゾーン（青）
#define MITIRU_ZONE_RENDER(name) \
    ZoneScopedNC(name, ::mitiru::debug::ZoneColors::Render)

/// @brief Physics用ゾーン（緑）
#define MITIRU_ZONE_PHYSICS(name) \
    ZoneScopedNC(name, ::mitiru::debug::ZoneColors::Physics)

/// @brief Audio用ゾーン（黄）
#define MITIRU_ZONE_AUDIO(name) \
    ZoneScopedNC(name, ::mitiru::debug::ZoneColors::Audio)

/// @brief Script用ゾーン（紫）
#define MITIRU_ZONE_SCRIPT(name) \
    ZoneScopedNC(name, ::mitiru::debug::ZoneColors::Script)

/// @brief UI用ゾーン（橙）
#define MITIRU_ZONE_UI(name) \
    ZoneScopedNC(name, ::mitiru::debug::ZoneColors::UI)

#else // !MITIRU_HAS_TRACY

#define MITIRU_ZONE                         ((void)0)
#define MITIRU_ZONE_NAMED(name)             ((void)0)
#define MITIRU_ZONE_SCOPED                  ((void)0)
#define MITIRU_ZONE_COLOR(name, color)      ((void)0)
#define MITIRU_FRAME_MARK                   ((void)0)
#define MITIRU_PLOT(name, value)            ((void)0)
#define MITIRU_MESSAGE(text)                ((void)0)

#define MITIRU_ZONE_RENDER(name)            ((void)0)
#define MITIRU_ZONE_PHYSICS(name)           ((void)0)
#define MITIRU_ZONE_AUDIO(name)             ((void)0)
#define MITIRU_ZONE_SCRIPT(name)            ((void)0)
#define MITIRU_ZONE_UI(name)                ((void)0)

#endif // MITIRU_HAS_TRACY
