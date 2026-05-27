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
///   - v3 (47cc3cc1): StatePushItem::strVal を 160 → 3968 に拡張
///     (launcher の view.launcher.projects 等、JSON-encoded big state push 対応)
///   - v4 (47..): FrameIntents に soundIntents 追加 (ADR 0008)。DLL ゲームが
///     既存 audio mixer へ「音を鳴らして」と intent を出せる。host は古い v3
///     module の soundIntents を読まない (= 音無しで動く) ので後方安全。
///   - v5 (本 commit): InputSnapshot に gamepad (XInput 主コントローラ) 状態を追加。
///     ボタンビットマスク (down/justPressed/justReleased) + axes[6] + connected。
///     POD 末尾への追記なので既存 field offset 不変。host が古い module に渡しても
///     古い module は新 field を読まないだけ (後方安全)。
constexpr std::uint32_t kCurrentApiVersion = 5;

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
	char name[64];          ///< action 名、null 終端 (例: "game.restart")
	char payloadJson[256];  ///< JSON payload 文字列、null 終端
};

/// @brief ゲームパッドのボタンビット (InputSnapshot::gamepadButtons* 用)。
/// @details 値は XInput の wButtons と一致。DLL は ModuleApi.hpp だけ include すれば
///          `if (input->gamepadButtonsDown & mitiru::module::gamepad::A) ...` と読める。
namespace gamepad
{
	constexpr std::uint32_t DPadUp    = 0x0001;
	constexpr std::uint32_t DPadDown  = 0x0002;
	constexpr std::uint32_t DPadLeft  = 0x0004;
	constexpr std::uint32_t DPadRight = 0x0008;
	constexpr std::uint32_t Start     = 0x0010;
	constexpr std::uint32_t Back      = 0x0020;
	constexpr std::uint32_t LS        = 0x0040; ///< 左スティック押し込み
	constexpr std::uint32_t RS        = 0x0080; ///< 右スティック押し込み
	constexpr std::uint32_t LB        = 0x0100; ///< 左バンパー
	constexpr std::uint32_t RB        = 0x0200; ///< 右バンパー
	constexpr std::uint32_t A         = 0x1000;
	constexpr std::uint32_t B         = 0x2000;
	constexpr std::uint32_t X         = 0x4000;
	constexpr std::uint32_t Y         = 0x8000;

	/// @brief gamepadAxes[] の添字。stick は [-1,1]、trigger は [0,1]（デッドゾーン適用済）。
	enum Axis : int
	{
		LeftStickX = 0, LeftStickY = 1,
		RightStickX = 2, RightStickY = 3,
		LeftTrigger = 4, RightTrigger = 5,
		AxisCount = 6,
	};
}

/// @brief 1 フレーム分の input 状態 (host → DLL の push)
/// @details
/// 全 256 VK code 対応。エッジ (just pressed / released) は host が
/// 前フレームとの diff から組み立てる。DLL 側は edge も held も完全な
/// stateless view として受け取り、自分で状態を保持しない
/// (= replay 時の bit-exact 再現が構造保証される)。
struct InputSnapshot
{
	std::uint8_t keysDown[256];           ///< 1 = 現在押下中
	std::uint8_t keysJustPressed[256];    ///< 1 = このフレームで押された
	std::uint8_t keysJustReleased[256];   ///< 1 = このフレームで離された
	float        mouseX;                  ///< 論理スクリーン座標
	float        mouseY;
	std::uint8_t mouseButtonsDown[3];           ///< L=0, R=1, M=2
	std::uint8_t mouseButtonsJustPressed[3];
	std::uint8_t mouseButtonsJustReleased[3];
	std::uint8_t _pad[1];                 ///< 明示的 padding (align)

	/// @brief このフレームに溜まった CEF JS からの action event
	std::int32_t actionEventCount;
	ActionEvent  actionEvents[16];

	// ── gamepad (主コントローラ = XInput player 0) — ABI v5 で追記 ──────
	// 末尾追記なので既存 field の offset は不変。非対応 platform / 未接続時は全 0。
	std::int32_t  gamepadConnected;            ///< 1 = 接続中 (0 = 未接続/非対応)
	std::uint32_t gamepadButtonsDown;          ///< gamepad:: ビットマスク (押下中)
	std::uint32_t gamepadButtonsJustPressed;   ///< このフレームで押された
	std::uint32_t gamepadButtonsJustReleased;  ///< このフレームで離された
	float         gamepadAxes[6];              ///< gamepad::Axis 添字。stick [-1,1] / trigger [0,1]
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
	char         key[96];       ///< state key、null 終端 (例: "view.hud.hp")
	std::int32_t kind;          ///< 型タグ (上記参照)
	std::int32_t intVal;        ///< kind=1, 3 用
	float        floatVal;      ///< kind=2 用
	char         strVal[3968];  ///< kind=4 用、null 終端。JSON-encoded な複合 state
	                            ///< (例: project リスト) を収めるのに十分な大きさ。
	                            ///< v0.2.0 launcher は 160 bytes で truncation に当たった。
};

/// @brief 1 つの inspectable の export (DLL → host の intent)
/// @details
/// 既存 InspectableRegistry の lambda ベース API は DLL-unsafe なので、
/// DLL は毎フレーム自分の inspectables を **pre-serialized JSON** で push する。
/// Engine が SharedSnapshot (%TEMP%) に書き出し、view.palette.items を更新する。
/// JSON が json[] buffer を超える時は **truncate** され `jsonLen` がその旨を示す。
struct InspectableExport
{
	char         name[64];   ///< 一意 id、null 終端
	char         title[64];  ///< 人間向けラベル、null 終端
	std::int32_t jsonLen;    ///< バイト数 (末尾 null を除く); 0 でも可
	char         json[3968]; ///< serialize した JSON、null 終端
};

/// @brief 「この音を鳴らして」という DLL → host の intent (ADR 0008)
/// @details ゲームは mixer / AudioEngine pointer を持たず (ADR 0005)、id を
///          書くだけ。host が所有する audio engine が再生する。id は host が
///          assets/audio/ からロードした論理名 (拡張子抜きファイル名)。
struct SoundIntent
{
	char         id[64];      ///< 論理サウンド id (例: "hit")。null 終端。
	std::uint8_t category;    ///< 0=SE, 1=BGM, 2=Voice
	std::uint8_t loop;        ///< 1 = ループ再生
	std::uint8_t stop;        ///< 1 = 鳴らすのでなく停止
	std::uint8_t _pad;
	float        volume;      ///< 0.0–1.0
};

/// @brief 1 フレーム分の DLL → host への要求 (intent)
/// @details
/// 全 field は **毎フレーム host が zero-init してから** on_update に渡す。
/// DLL は必要な field だけ書き込む。次フレームには値は残らない。
struct FrameIntents
{
	std::uint8_t requestStop;       ///< 1 = engine.requestStop() を呼ぶ
	std::uint8_t requestScreenshot; ///< 1 = engine が PNG を保存
	std::uint8_t paletteToggle;     ///< 1 = command palette の visible を toggle
	std::uint8_t _pad0[5];

	/// @brief CEF state push queue (HUD 更新等)
	std::int32_t  statePushCount;
	StatePushItem statePushes[64];

	/// @brief Inspectable registry の per-frame snapshot
	std::int32_t      exportedInspectableCount;
	InspectableExport exportedInspectables[8];

	/// @brief 生 JS の実行 (例: hot reload の toast trigger)
	/// @details jsToExecuteLen > 0 のとき、host が
	///          `cefContext.executeJavaScript(jsToExecute)` を呼ぶ。
	std::int32_t jsToExecuteLen;
	char         jsToExecute[2048];

	/// @brief このフレームの sound 再生要求 (ADR 0008)。末尾に追加したので既存
	///        field の offset は不変。struct size は増えたため kCurrentApiVersion
	///        を 4 に bump した。
	std::int32_t soundIntentCount;
	SoundIntent  soundIntents[8];
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
