#pragma once

/// @file BoundaryTypes.hpp
/// @brief ABI v48 / v49 で境界に足した POD (パッドの拡張、セーブスロットの一覧、曲の拍、実績、ツール窓に見せる資産)。ModuleApi.hpp が include する。
/// @details 配置は ModuleApi.hpp の InputSnapshot / FrameIntents と同じ規約 (固定長、明示の詰め物、sizeof を固定)。
///          意味と配置の一覧は docs/adr/0056-abi-v48-boundary.md。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::module
{

/// @brief GamepadExt::caps の bit (そのパッドが持つ機能)。
inline constexpr std::uint8_t kPadCapGyro             = 1u << 0;
inline constexpr std::uint8_t kPadCapAccel            = 1u << 1;
inline constexpr std::uint8_t kPadCapTouchpad         = 1u << 2;
inline constexpr std::uint8_t kPadCapLightbar         = 1u << 3;
inline constexpr std::uint8_t kPadCapRumble           = 1u << 4;
inline constexpr std::uint8_t kPadCapTriggerRumble    = 1u << 5;
inline constexpr std::uint8_t kPadCapAdaptiveTriggers = 1u << 6;

/// @brief GamepadExt::extraDown などの bit (gamepad:: の 16 bit に入らないボタン)。
inline constexpr std::uint8_t kPadExtraTouchpad = 1u << 0;

/// @brief タッチパッドの指 1 本 (x, y は 0..1、左上が 0)。
struct PadTouch
{
	std::uint8_t down;
	std::uint8_t _pad[3];
	float        x;
	float        y;
	float        pressure;
};

/// @brief パッド 1 台の拡張 (InputSnapshot::gamepadsExt、v48)。枠は gamepads[] と同じ。未接続の枠は全 0。
/// @details kind / power は input::PadKind / PadPower の値。motion は FrameIntents::padOut で有効にした台だけ入る
///          (SDL がセンサーを止めている間は 0)。単位はジャイロ rad/s、加速度 m/s^2。
struct GamepadExt
{
	std::uint8_t kind;
	std::uint8_t power;
	std::int8_t  battery;        ///< 0..100。分からなければ -1
	std::uint8_t caps;           ///< kPadCap*
	std::uint8_t extraDown;      ///< kPadExtra*
	std::uint8_t extraPressed;
	std::uint8_t extraReleased;
	std::uint8_t motionActive;   ///< 1 = gyro / accel が今の値
	float        gyro[3];
	float        accel[3];
	PadTouch     touch[2];
};

/// @brief PadOutIntent::set の bit。立てた欄だけ host が取り込み、次に立つまで保つ (振動は秒数ぶん)。
inline constexpr std::uint8_t kPadOutMotion        = 1u << 0;
inline constexpr std::uint8_t kPadOutLight         = 1u << 1;
inline constexpr std::uint8_t kPadOutTriggerLeft   = 1u << 2;
inline constexpr std::uint8_t kPadOutTriggerRight  = 1u << 3;
inline constexpr std::uint8_t kPadOutRumble        = 1u << 4;
inline constexpr std::uint8_t kPadOutTriggerRumble = 1u << 5;

/// @brief アダプティブトリガーの効き (input::TriggerEffect::Mode と同じ番号。0 = 切る)。
inline constexpr std::uint8_t kPadTriggerOff        = 0;
inline constexpr std::uint8_t kPadTriggerResistance = 1;
inline constexpr std::uint8_t kPadTriggerVibration  = 2;

struct PadTriggerOut
{
	std::uint8_t mode;       ///< kPadTrigger*
	std::uint8_t start;      ///< 効き始める押し込み位置 0..255
	std::uint8_t strength;   ///< 抵抗の強さ / 振幅 0..255
	std::uint8_t frequency;  ///< Vibration の周波数 (Hz)
};

/// @brief パッド 1 台への出力 (FrameIntents::padOut、v48)。録画には乗らない (出力だけの演出)。
struct PadOutIntent
{
	std::uint8_t  set;            ///< kPadOut*
	std::uint8_t  motionEnabled;  ///< 1 = ジャイロと加速度を読む (電池を使う)
	std::uint8_t  light[3];       ///< ライトバーの RGB
	std::uint8_t  _pad[3];
	PadTriggerOut trigger[2];     ///< 0 = 左、1 = 右
	float         rumbleLow;      ///< 左の重いモーター 0..1
	float         rumbleHigh;     ///< 右の軽いモーター 0..1
	float         triggerRumble[2];
	float         rumbleSec;      ///< 振動の長さ (秒)。kPadOutRumble / kPadOutTriggerRumble に共通
	std::uint8_t  _tail[4];
};

/// @brief セーブスロット 1 個の要約 (InputSnapshot::slots、v48)。新しい順。
/// @details status は save::SlotStatus の値 (0 = 読める、1/2 = 予備から戻した、4 = 壊れている 等)。
struct SlotSummary
{
	char          slot[28];
	std::uint8_t  status;
	std::uint8_t  hasThumbnail;
	std::uint8_t  _pad[2];
	std::int64_t  savedAtUnixSec;
	double        playtimeSec;
	char          chapter[64];   ///< hud.saveSlot(slot, chapter) で付けた名前 (UTF-8)
};

inline constexpr int kMaxSlotSummaries = 8;

/// @brief 曲の拍 (InputSnapshot::music、v48)。曲の定義 (music.json) で鳴らしていない間は playing = 0。
struct MusicClock
{
	std::uint8_t playing;
	std::uint8_t beatInBar;      ///< 0 始まり
	std::uint8_t beatsPerBar;
	std::uint8_t _pad;
	std::int32_t segment;        ///< music.json の区間の添字。-1 = 無し
	std::int32_t bar;            ///< 区間の頭からの小節 (0 始まり)
	float        beatPhase;      ///< 拍の中の位置 0..1
	double       sec;            ///< 区間の頭からの秒
};

/// @brief AchievementIntent::kind の値。
inline constexpr std::uint8_t kAchievementUnlock        = 1;
inline constexpr std::uint8_t kAchievementClear         = 2;
inline constexpr std::uint8_t kAchievementStatInt       = 3;
inline constexpr std::uint8_t kAchievementStatFloat     = 4;
inline constexpr std::uint8_t kAchievementStore         = 5;   ///< 統計と実績をサーバーへ送る
inline constexpr std::uint8_t kAchievementPresence      = 6;   ///< id = key、text = 値
inline constexpr std::uint8_t kAchievementClearPresence = 7;

/// @brief 実績・統計・Rich Presence の依頼 (FrameIntents::achievements、v48)。Steam が無い host では何もしない。
struct AchievementIntent
{
	std::uint8_t kind;
	std::uint8_t _pad[3];
	std::int32_t intValue;
	float        floatValue;
	char         id[64];
	char         text[64];
};

inline constexpr int kMaxAchievementIntents = 4;
inline constexpr int kMaxFrameMarks = 8;
inline constexpr int kFrameMarkLen = 24;

/// @brief 入力のアクション (manifest の i 番目 = bit i)。manifest はゲーム DLL が export する JSON。
inline constexpr int kMaxModuleActions = 64;

/// @brief アクションの表を返す export 名 (optional、v48)。`MITIRU_ACTIONS(json)` が出す。
constexpr const char* kActionManifestSymbol = "mitiru_module_action_manifest";
using ModuleActionManifestFn = const char* (*)();

/// @brief セーブの読み直しで GameMemory の形が変わっていた時に呼ぶ export 名 (optional、v48)。
constexpr const char* kMigrateSymbol = "mitiru_module_migrate";
/// @brief 古い bytes (oldLayoutHash の形) から newMemory (newSize、今の GameMemory の写しが入っている) を作る。1 = 移せた / 0 = 移せない。
using ModuleMigrateFn = std::int32_t (*)(const void* oldBytes, std::uint64_t oldSize, std::uint64_t oldLayoutHash,
                                         void* newMemory, std::uint64_t newSize);

/// @brief ツール窓に見せる DLL の資産 1 件 (v49)。GameMemory に無い形のデータ (ビヘイビアツリー、ナビメッシュ) を渡す
/// @details kind は "bt_tree" (BehaviorTreeJson と同じ JSON の文字列) か "navmesh" (NavMesh / DynamicNavMesh の焼いた
///          bytes)。data は DLL の static か読み込んだデータで、DLL を手放すまで変えない。host は読み込みの時に 1 度だけ写す。
struct InspectAsset
{
	char          kind[16];
	char          name[32];
	const void*   data;
	std::uint64_t size;
};

inline constexpr int kMaxInspectAssets = 16;

/// @brief 資産の一覧を out[0..cap) に書き、書いた数を返す export 名 (optional、v49)。`MITIRU_INSPECT_ASSETS(fn)` が出す。
constexpr const char* kInspectAssetsSymbol = "mitiru_module_inspect_assets";
using ModuleInspectAssetsFn = std::int32_t (*)(InspectAsset* out, std::int32_t cap);

static_assert(sizeof(InspectAsset) == 64 && offsetof(InspectAsset, data) == 48, "InspectAsset wire size 固定 (v49)");
static_assert(std::is_trivially_copyable_v<InspectAsset> && std::is_standard_layout_v<InspectAsset>);

static_assert(sizeof(PadTouch) == 16, "PadTouch wire size 固定 (v48)");
static_assert(sizeof(GamepadExt) == 64, "GamepadExt wire size 固定 (v48)");
static_assert(offsetof(GamepadExt, gyro) == 8 && offsetof(GamepadExt, touch) == 32, "GamepadExt layout (v48)");
static_assert(sizeof(PadTriggerOut) == 4, "PadTriggerOut wire size 固定 (v48)");
static_assert(sizeof(PadOutIntent) == 40, "PadOutIntent wire size 固定 (v48)");
static_assert(offsetof(PadOutIntent, trigger) == 8 && offsetof(PadOutIntent, rumbleLow) == 16, "PadOutIntent layout (v48)");
static_assert(sizeof(SlotSummary) == 112, "SlotSummary wire size 固定 (v48)");
static_assert(offsetof(SlotSummary, savedAtUnixSec) == 32 && offsetof(SlotSummary, chapter) == 48, "SlotSummary layout (v48)");
static_assert(sizeof(MusicClock) == 24 && offsetof(MusicClock, sec) == 16, "MusicClock wire size 固定 (v48)");
static_assert(sizeof(AchievementIntent) == 140 && offsetof(AchievementIntent, id) == 12, "AchievementIntent wire size 固定 (v48)");
static_assert(std::is_trivially_copyable_v<GamepadExt> && std::is_trivially_copyable_v<PadOutIntent> &&
              std::is_trivially_copyable_v<SlotSummary> && std::is_trivially_copyable_v<AchievementIntent>);

}  // namespace mitiru::module
