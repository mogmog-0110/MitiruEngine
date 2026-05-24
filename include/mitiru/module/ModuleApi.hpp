#pragma once

/// @file ModuleApi.hpp
/// @brief Engine ↔ Game DLL の C ABI 契約 (v0.2.0 hot reload foundation)
/// @details
/// Game DLL は **純関数** に近い形で実装される (ADR 0005):
///
/// @code
///   (memory_in, input_snapshot, dt) → (memory_out, render, intents)
/// @endcode
///
/// **3 つの不変条件 (ADR 0005)**:
/// 1. Game DLL は host (engine) の object pointer を一切持たない
/// 2. Host が必要データを毎フレーム POD で push する (InputSnapshot)
/// 3. Game の side effect は intent field 経由で「お願い」する (FrameIntents)
///
/// 詳細: `docs/adr/0005-host-game-c-abi-signal-flow.md`
///
/// ## 境界跨ぎ規約
///
/// - 境界では **POD のみ** (`std::string` / `std::function` / C++ class
///   pointer は禁止)
/// - 文字列は `char[N]` 固定長 + null-terminated
/// - 可変長は `int32_t count` + `Item items[MAX]` で bounded
/// - 境界 struct のフィールド追加は ABI break → `kCurrentApiVersion` を bump
///
/// ## DLL 側の最小実装
///
/// @code
///   extern "C" __declspec(dllexport)
///   void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory);
/// @endcode
///
/// engine host (`mitiru_host.exe`) が `LoadLibrary` で DLL を読み、
/// `GetProcAddress("mitiru_module_load")` でこの関数を解決し、
/// 自分の管理する `ModuleApi` 構造体 + `void**` の永続 state slot を渡す。

#include <cstddef>
#include <cstdint>

// Forward declare engine types so the header is light. Concrete definitions
// come from the engine when the DLL links against `Mitiru::mitiru`.
namespace mitiru { class Screen; }

namespace mitiru::module
{

/// @brief 現在の ABI version。breaking change で bump する。
/// @details
///   - v1 (1e53c8d9): on_init / on_update / on_draw / on_shutdown のみ
///   - v2 (47e0a43a): InputSnapshot, FrameIntents 追加。on_update sig 変更
///   - v3 (本 commit): StatePushItem::strVal を 160 → 3968 に拡張
///     (launcher の view.launcher.projects 等、JSON-encoded big state push 対応)
constexpr std::uint32_t kCurrentApiVersion = 3;

/// @brief load 時のエントリ関数名 — host が `GetProcAddress` で探す symbol
constexpr const char* kLoadSymbol = "mitiru_module_load";

/// @brief unload 時のエントリ関数名 (optional — 無くてもよい)
constexpr const char* kUnloadSymbol = "mitiru_module_unload";

// ── POD wire format ──────────────────────────────────────────────────────

/// @brief CEF JS から DLL に届く action event (e.g. button click)
/// @details
///   - JS 側: `window.mitiru.dispatch("game.restart", {})` で発火
///   - Engine が action handler を register、queue に enqueue
///   - 翌フレーム頭の InputSnapshot.actionEvents に詰めて DLL に渡す
struct ActionEvent
{
	char name[64];          ///< action name, null-terminated (e.g. "game.restart")
	char payloadJson[256];  ///< JSON payload string, null-terminated
};

/// @brief 1 フレーム分の input 状態 (host → DLL の push)
/// @details
/// 全 256 VK code 対応。エッジ (just pressed / released) は host が
/// 前フレームとの diff から組み立てる。DLL 側は edge も held も完全な
/// stateless view として受け取り、自分で状態を保持しない
/// (= replay 時の bit-exact 再現が構造保証される)。
struct InputSnapshot
{
	std::uint8_t keysDown[256];           ///< 1 = currently held
	std::uint8_t keysJustPressed[256];    ///< 1 = pressed this frame
	std::uint8_t keysJustReleased[256];   ///< 1 = released this frame
	float        mouseX;                  ///< logical screen coords
	float        mouseY;
	std::uint8_t mouseButtonsDown[3];           ///< L=0, R=1, M=2
	std::uint8_t mouseButtonsJustPressed[3];
	std::uint8_t mouseButtonsJustReleased[3];
	std::uint8_t _pad[1];                 ///< explicit padding (align)

	/// @brief queued action events from CEF JS this frame
	std::int32_t actionEventCount;
	ActionEvent  actionEvents[16];
};

/// @brief state push の 1 件 (DLL → host の intent)
/// @details
///   kind の意味:
///     0 = null      (state を消す)
///     1 = int       (intVal 使う)
///     2 = float     (floatVal 使う)
///     3 = bool      (intVal 使う; 0/1)
///     4 = string    (strVal 使う)
///
/// host が `engine.moduleStateStore()->set(key, value)` を呼ぶ。
/// scene.html 側は `window.mitiru.onStateChange(key, ...)` で受ける。
struct StatePushItem
{
	char         key[96];       ///< state key, null-terminated (e.g. "view.hud.hp")
	std::int32_t kind;          ///< type tag (上記参照)
	std::int32_t intVal;        ///< for kind=1, 3
	float        floatVal;      ///< for kind=2
	char         strVal[3968];  ///< for kind=4, null-terminated. Large enough for
	                            ///< JSON-encoded compound state (e.g. project lists).
	                            ///< v0.2.0 launcher hit truncation at 160 bytes.
};

/// @brief 1 つの inspectable の export (DLL → host の intent)
/// @details
/// 既存 InspectableRegistry の lambda ベース API は DLL-unsafe なので、
/// DLL は毎フレーム自分の inspectables を **pre-serialized JSON** で push する。
/// Engine が SharedSnapshot (%TEMP%) に書き出し、view.palette.items を更新する。
/// JSON が json[] buffer を超える時は **truncate** され `jsonLen` がその旨を示す。
struct InspectableExport
{
	char         name[64];   ///< unique id, null-terminated
	char         title[64];  ///< human label, null-terminated
	std::int32_t jsonLen;    ///< size in bytes (without trailing null); 0 OK
	char         json[3968]; ///< serialized JSON, null-terminated
};

/// @brief 1 フレーム分の DLL → host への要求 (intent)
/// @details
/// 全 field は **毎フレーム host が zero-init してから** on_update に渡す。
/// DLL は必要な field だけ書き込む。次フレームには値は残らない。
struct FrameIntents
{
	std::uint8_t requestStop;       ///< 1 = call engine.requestStop()
	std::uint8_t requestScreenshot; ///< 1 = engine が PNG を保存
	std::uint8_t paletteToggle;     ///< 1 = command palette の visible を toggle
	std::uint8_t _pad0[5];

	/// @brief CEF state push queue (HUD 更新等)
	std::int32_t  statePushCount;
	StatePushItem statePushes[64];

	/// @brief Inspectable registry の per-frame snapshot
	std::int32_t      exportedInspectableCount;
	InspectableExport exportedInspectables[8];

	/// @brief Raw JS execution (e.g. hot reload toast trigger)
	/// @details jsToExecuteLen > 0 のとき、host が
	///          `cefContext.executeJavaScript(jsToExecute)` を呼ぶ。
	std::int32_t jsToExecuteLen;
	char         jsToExecute[2048];
};

// ── ModuleApi callback table ─────────────────────────────────────────────

/// @brief DLL → host へ届ける callback table
/// @details すべて nullable。DLL は自分が使う slot だけ埋める。
struct ModuleApi
{
	/// @brief ABI version。engine が kCurrentApiVersion を初期値で埋めて DLL に
	///        渡す。DLL は必要なら自分の知ってる最大 version に置き換える。
	std::uint32_t version;

	/// @brief 初回 load 直後、メインループが回り始める前に呼ばれる。
	///        memory は user_memory (DLL が *memory にセットしたもの)。
	void (*on_init)(void* memory);

	/// @brief 毎フレーム呼ばれる (固定 timestep)。
	/// @details
	///   - input は host が組み立てた現フレームの input スナップショット (read-only)
	///   - intents は zero-init された buffer。DLL は要求を書き込む。
	///     host が next-tick 頭で intents を drain して該当 engine 操作を実行する。
	void (*on_update)(void* memory, float dt,
	                  const InputSnapshot* input,
	                  FrameIntents* intents);

	/// @brief 毎フレーム描画タイミングで呼ばれる
	/// @details screen は per-frame 引数。DLL は保持しない。
	void (*on_draw)(void* memory, mitiru::Screen* screen);

	/// @brief DLL がもうすぐ unload される直前に呼ばれる (reload 含む)。
	void (*on_shutdown)(void* memory);
};

/// @brief DLL が export すべき load 関数のシグネチャ
using ModuleLoadFn = void (*)(ModuleApi* api, void** memory);

/// @brief DLL が export すべき unload 関数のシグネチャ (optional)
using ModuleUnloadFn = void (*)(void* memory);

}  // namespace mitiru::module
