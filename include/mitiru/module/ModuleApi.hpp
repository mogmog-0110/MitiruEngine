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
#include <type_traits>

#include <mitiru/module/Reflection.hpp>  // FieldDescriptor / ReflectSchema (ADR 0018)

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
///   - v5: InputSnapshot に gamepad (XInput 主コントローラ) 状態を追加。
///     ボタンビットマスク (down/justPressed/justReleased) + axes[6] + connected。
///     POD 末尾への追記なので既存 field offset 不変。host が古い module に渡しても
///     古い module は新 field を読まないだけ (後方安全)。
///   - v6: SoundIntent 末尾に pitchScale / fadeInSec / fadeOutSec を追加。SE 個別
///     ピッチ変更と BGM crossfade/stopfade に対応 (ADR 0008 拡張)。末尾追記で後方安全。
///   - v7: FrameIntents 末尾に visualIntents[8] + visualIntentCount を追加。Tint 等の
///     一発演出を intent で要求できる。末尾追記 + zero-init で v≤6 module は後方安全。
///   - v8: InputSnapshot 末尾に rngSeed を追加 (ADR 0012)。host が決定論 seed を供給し、
///     RNG 駆動 game も replay で bit-exact 再現できる。末尾追記で v≤7 module は後方安全。
///   - v9: ModuleApi 末尾に memorySize を追加 (ADR 0013)。DLL が GameMemory のバイト数を
///     申告し、host が単一 state channel (replay state slot) に GameMemory を記録できる。
///     ②time-travel / ④replay-as-test が real DLL game で構造保証に。v≤8 module は後方安全。
///   - v10: FrameIntents 末尾に toolRequests[] を追加 (ADR 0014)。DLL が「この独立ウィンドウの
///     ツール (inspector 等) を開いて」と host に頼み、host が別 exe を spawn する。必要なときだけ
///     コードから tool 窓を配置できる (pulled UI)。末尾追記で v≤9 module は後方安全。
///   - v11: ModuleApi 末尾に seriesProbes[] を追加 (ADR 0017)。DLL が「GameMemory から
///     このスカラーを引く純関数」を申告し、host が GameMemory ring に適用して time-travel の
///     観測系列 (HP 履歴など) を自動生成する。手動 Snapshot push を廃し、観測も rewind も
///     replay も単一の GameMemory 源に統一する。末尾追記 + zero-init で v≤10 module は後方安全。
///   - v12: ModuleApi 末尾に reflectFields[] + reflectSchemas[] を追加 (ADR 0018)。DLL が
///     GameMemory の全フィールドの名前・型・オフセットを申告し、host が GameMemory バイト列
///     (現フレーム + ring の過去) を構造化 JSON 化して AI に全状態を開放する (probe の拡張)。
///     末尾追記 + zero-init で v≤11 module は後方安全 (reflectFieldCount=0 = 非対応)。
///   - v14: Screen 末尾に AI 観測用 draw log メンバを追加 (/api/ai/frame)。Screen* は
///     gameDraw で DLL 境界を渡るため layout 拡張 = ABI break。末尾追加なので
///     旧 module (v≤13) は新 host 上で安全 (旧 offset 不変、draw log に載らないだけ)。
constexpr std::uint32_t kCurrentApiVersion = 14;  // v14: Screen 末尾 draw log (AI フレーム観測)

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

	// ── 決定論 RNG seed (ABI v8 で追記、ADR 0012) ─────────────────────
	// 末尾追記なので既存 field の offset は不変。v≤7 module は読まないだけ (0)。
	// host が EngineConfig::randomSeed を毎フレーム供給。replay 時は記録値が再投入され
	// bit-exact に復元される。DLL は `mitiru::Random rng(input->rngSeed)` で seed する。
	std::uint64_t rngSeed;                     ///< session 固定の決定論 seed (0 = 未供給)

	// ── 音声クロック (ABI v13 で追記) ─────────────────────────────────
	// host の audio backend が再生したサンプル位置 (秒)。リズムゲーム等がフレーム dt
	// 積算ではなく音声クロック基準で判定するために使う。0 = 非対応 backend (Null/headless
	// 等) → game は dt 積算へフォールバック。replay 時は末尾の moduleInputOverride が
	// snapshot 全体を記録値で置換するので、再生でも同じ値が流れ bit-exact 性が保たれる。
	double audioTimeSec;
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
/// @brief 「この画面演出をやって」という DLL → host の intent (#33、v7 追加)。
/// @details kind = 0:None / 1:Tint (色フラッシュ)。将来 shake / hitstop を追加可。
///          tint は host が `Screen::pushTint({r,g,b,a}, durSec)` に直接渡す。
struct VisualIntent
{
	std::uint8_t kind;       ///< 0 = none、1 = Tint
	std::uint8_t _pad[3];
	float        r, g, b, a; ///< Tint 色 (a は初期 alpha、時間で fade)
	float        durSec;     ///< 効果尺 (秒)
	float        _reserved;  ///< 8byte 境界 padding + 将来用
};

constexpr std::uint8_t kVisualIntentNone = 0;
constexpr std::uint8_t kVisualIntentTint = 1;

struct SoundIntent
{
	char         id[64];      ///< 論理サウンド id (例: "hit")。null 終端。
	std::uint8_t category;    ///< 0=SE, 1=BGM, 2=Voice
	std::uint8_t loop;        ///< 1 = ループ再生
	std::uint8_t stop;        ///< 1 = 鳴らすのでなく停止
	std::uint8_t _pad;
	float        volume;      ///< 0.0–1.0
	// v6 で末尾に追加 (#19/#20、後方安全: 既存 offset 不変、host が zero-init)。
	float        pitchScale;  ///< 0=未指定→1.0、0.5..2.0 程度。SE 用 (BGM は無視可)。
	float        fadeInSec;   ///< > 0 で再生開始時に 0→volume へ fade-in (BGM/SE 両用)。
	float        fadeOutSec;  ///< stop=1 のとき > 0 で volume→0 fade-out してから停止。
};

/// @brief 「このツール窓を開いて」という DLL → host の intent (ADR 0014、v10 追加)。
/// @details game は Engine* を持てない (ADR 0005) ので、独立ウィンドウのツール
///          (inspector / input monitor / time-travel など) を自分では開けない。代わりに
///          tool 名を書いて「開いて」と頼み、host が別 exe (mitiru_<tool>.exe) を spawn する。
///          必要なときだけ呼ぶ — 既定では何も開かない (pulled UI、アトミックツール哲学)。
struct RequestToolWindow
{
	char tool[64];   ///< ツール名 (例: "inspector")。host が mitiru_<tool>.exe を探す。null 終端。
	char args[128];  ///< 追加 CLI 引数 (例: "--inspectable input")。null 終端。
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

	/// @brief このフレームの画面演出要求 (#33、v7 追加)。SoundIntents と同じ pattern。
	///        末尾追加なので既存 offset 不変、v≤6 module は無視されるだけ (後方安全)。
	std::int32_t visualIntentCount;
	VisualIntent visualIntents[8];

	/// @brief このフレームのツール窓 spawn 要求 (ADR 0014、v10 追加)。末尾追加で v≤9 後方安全。
	std::int32_t      toolRequestCount;
	RequestToolWindow toolRequests[4];

	// ── 便利メソッド (game 作者向け) ──────────────────────────────────────
	// HUD へ値を送る / 音を鳴らす、を 1 行で書くためのヘルパ。中の固定長スロット詰め
	// (空き探し・上限チェック・null 終端) はここに隠す。これが無いと game 側が毎回
	// memset / strncpy で手書きする羽目になり、初心者には厳しい。
	//
	// これらは inline メソッドで、呼んだ game DLL 側にだけ展開される。struct の
	// メモリ配置 (= DLL 境界の wire format) は一切変えない (下の static_assert で保証)。
	//
	// 使い方:
	//   intents->pushInt("view.hud.score", score);   // scene.html の data-m-text へ
	//   intents->playSound("brick", 0.6f);           // assets/audio/brick.wav を再生

	/// HUD に int を送る (scene.html の data-m-text="view.hud.xxx" が受け取る)。
	void pushInt(const char* key, int value) noexcept
	{
		if (StatePushItem* s = nextStatePush()) { s->kind = 1; s->intVal = value; setKey(s, key); }
	}
	/// HUD に float を送る。
	void pushFloat(const char* key, float value) noexcept
	{
		if (StatePushItem* s = nextStatePush()) { s->kind = 2; s->floatVal = value; setKey(s, key); }
	}
	/// HUD に bool を送る (data-m-show / data-m-class の条件に使える)。
	void pushBool(const char* key, bool value) noexcept
	{
		if (StatePushItem* s = nextStatePush()) { s->kind = 3; s->intVal = value ? 1 : 0; setKey(s, key); }
	}
	/// HUD に文字列を送る (勝敗テキスト等)。
	void pushString(const char* key, const char* value) noexcept
	{
		if (StatePushItem* s = nextStatePush()) { s->kind = 4; setKey(s, key); copyStr(s->strVal, value, sizeof(s->strVal)); }
	}
	/// 効果音を鳴らす。host が assets/audio/<id>.wav (.ogg/.mp3) を再生する。
	void playSound(const char* id, float volume = 1.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 0; s.volume = volume; s.pitchScale = 1.0f;
	}
	/// 効果音をピッチ指定で鳴らす (pitch 0.5..2.0 程度、1.0=原音)。1 つの SE を音階で鳴らす
	/// リズムゲーム等で使う。pitch は再生レート変更 (= 音程と長さが同時に変わる)。
	void playSound(const char* id, float volume, float pitch) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 0; s.volume = volume; s.pitchScale = (pitch > 0.0f) ? pitch : 1.0f;
	}

	/// 画面を一瞬色フラッシュさせる (被弾演出など)。host が Screen::pushTint に渡す。
	/// BGM を再生する (category=1)。host が assets/audio/<id>.wav をストリーム再生する。
	/// loop=true でループ。連続トラックなので 1 回呼べばよい (毎フレーム呼ばない)。
	void playMusic(const char* id, float volume = 1.0f, bool loop = true) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 1; s.volume = volume; s.loop = loop ? 1 : 0; s.pitchScale = 1.0f;
	}
	/// 再生中の BGM を停止する (fadeOutSec > 0 でフェードアウト)。
	void stopMusic(float fadeOutSec = 0.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		s.category = 1; s.stop = 1; s.fadeOutSec = fadeOutSec;
	}

	/// 画面を一瞬色フラッシュさせる (被弾演出など)。host が Screen::pushTint に渡す。
	void pushTint(float r, float g, float b, float a, float durationSec) noexcept
	{
		const int cap = static_cast<int>(sizeof(visualIntents) / sizeof(visualIntents[0]));
		if (visualIntentCount >= cap) { return; }
		VisualIntent& v = visualIntents[visualIntentCount++];
		v = VisualIntent{};
		v.kind = kVisualIntentTint;
		v.r = r; v.g = g; v.b = b; v.a = a; v.durSec = durationSec;
	}

	/// このフレームの PNG 保存を要求する。
	void requestScreenshotNow() noexcept { requestScreenshot = 1; }

	/// inspector (別窓のデバッグツール) に観察データ (JSON 文字列) を送る。
	/// 必要なときだけ呼べばよい — inspector が開いている時にだけ映る (pulled UI)。
	void pushInspectable(const char* name, const char* title, const char* json) noexcept
	{
		const int cap = static_cast<int>(sizeof(exportedInspectables) / sizeof(exportedInspectables[0]));
		if (exportedInspectableCount >= cap) { return; }
		InspectableExport& e = exportedInspectables[exportedInspectableCount++];
		e = InspectableExport{};
		copyStr(e.name,  name,  sizeof(e.name));
		copyStr(e.title, title, sizeof(e.title));
		std::size_t i = 0;
		const std::size_t cap2 = sizeof(e.json);
		if (json != nullptr) { for (; json[i] != '\0' && i + 1 < cap2; ++i) { e.json[i] = json[i]; } }
		e.json[i] = '\0';
		e.jsonLen = static_cast<std::int32_t>(i);
	}

	/// 独立ウィンドウのツール (inspector 等) を開くよう host に頼む。必要なときだけ呼ぶ。
	/// host は mitiru_<tool>.exe を別窓で spawn する (ADR 0014)。
	void requestToolWindow(const char* tool, const char* args = "") noexcept
	{
		const int cap = static_cast<int>(sizeof(toolRequests) / sizeof(toolRequests[0]));
		if (toolRequestCount >= cap) { return; }
		RequestToolWindow& r = toolRequests[toolRequestCount++];
		r = RequestToolWindow{};
		copyStr(r.tool, tool, sizeof(r.tool));
		copyStr(r.args, args ? args : "", sizeof(r.args));
	}

	/// 生 JavaScript を CEF に実行させる (escape hatch)。HUD は data-m-* で足りるので、
	/// data-m-* で表せない one-shot な DOM 操作 (例: hot-reload の location.reload) だけに使う。
	void runJs(const char* code) noexcept
	{
		const int cap = static_cast<int>(sizeof(jsToExecute) / sizeof(jsToExecute[0]));
		int i = 0;
		if (code != nullptr) { for (; code[i] != '\0' && i + 1 < cap; ++i) { jsToExecute[i] = code[i]; } }
		jsToExecute[i] = '\0';
		jsToExecuteLen = i;
	}

private:
	/// 空き state-push スロットを 1 つ確保して key を書く。満杯なら nullptr。
	StatePushItem* nextStatePush() noexcept
	{
		const int cap = static_cast<int>(sizeof(statePushes) / sizeof(statePushes[0]));
		if (statePushCount >= cap) { return nullptr; }
		StatePushItem& s = statePushes[statePushCount++];
		s = StatePushItem{};
		return &s;
	}
	static void setKey(StatePushItem* s, const char* key) noexcept { copyStr(s->key, key, sizeof(s->key)); }
	/// 固定長バッファへの null 終端コピー (src が長ければ切り詰める)。
	static void copyStr(char* dst, const char* src, std::size_t cap) noexcept
	{
		if (cap == 0) { return; }
		std::size_t i = 0;
		if (src != nullptr) { for (; src[i] != '\0' && i + 1 < cap; ++i) { dst[i] = src[i]; } }
		dst[i] = '\0';
	}
};

// 便利メソッドを足しても DLL 境界の wire format (= メモリ配置) は不変であることを
// 構造で保証する。これが崩れたら host と game で解釈がズレる。
static_assert(std::is_trivially_copyable_v<FrameIntents>,
              "FrameIntents は DLL 境界を memcpy で渡るので trivially copyable を保つこと");
static_assert(std::is_standard_layout_v<FrameIntents>,
              "FrameIntents は C ABI wire format なので standard layout を保つこと");
static_assert(std::is_trivially_copyable_v<InputSnapshot>,
              "InputSnapshot も同上 (host → game の POD push)");

// ── 観測 probe (ABI v11、ADR 0017) ───────────────────────────────────────

/// @brief GameMemory から追跡スカラーを引く純関数 (C 生関数ポインタ = POD = ADR 0005 安全)
/// @details host が GameMemory bytes へのポインタを渡して呼ぶ。読むのは GameMemory のみ
///          (static / 外部状態を読むと replay 非再現になる)。capture を持たない lambda は
///          暗黙変換可、capture ありは変換不可でコンパイル拒否される (footgun 防止)。
using SeriesProbeFn = double (*)(const void* gameMemory);

/// @brief 「GameMemory のこの値を time-travel graph で追って」という DLL → host の宣言 (v11)。
/// @details DLL が load 時に申告する (毎フレーム不要)。host が GameMemoryRing の各フレームに
///          accessor を適用して系列を作り、SeriesMarkers で節目を抽出して inspector に出す。
struct SeriesProbe
{
	char          name[32];      ///< 系列 id (例: "hp")、null 終端
	char          title[48];     ///< 人間向けラベル (例: "HP")、null 終端
	SeriesProbeFn accessor;      ///< GameMemory → double。null = 無効スロット
	double        threshold;     ///< hasThreshold=1 のとき danger ライン跨ぎを marker に
	std::uint8_t  hasThreshold;  ///< 1 = threshold 跨ぎ判定を行う
	std::uint8_t  _pad[7];
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

	/// @brief GameMemory (DLL が *memory にセットした state) のバイト数 (ABI v9、ADR 0013)。
	/// @details DLL は `api->memorySize = sizeof(自分の GameMemory)` を申告する。0 = 未申告で、
	///          host は GameMemory を記録せず観測 view.* にフォールバックする。host は replay
	///          記録時にこのサイズだけ opaque に memcpy する (中身は parse しない = ADR 0005)。
	std::uint32_t memorySize;

	/// @brief 観測 probe テーブル (ABI v11、ADR 0017)。末尾追記なので v≤10 module は後方安全
	///        (zero-init で seriesProbeCount=0 = 観測なし)。`MITIRU_GAME_SERIES` が埋める。
	std::int32_t seriesProbeCount;
	SeriesProbe  seriesProbes[8];

	/// @brief GameMemory リフレクション記述表 (ABI v12、ADR 0018)。末尾追記で v≤11 後方安全
	///        (zero-init で reflectFieldCount=0 = 非対応)。`MITIRU_REFLECT` が埋める。host が
	///        GameMemory バイト列を構造化 JSON 化して AI に全状態を開放する。
	std::int32_t  reflectFieldCount;
	FieldDescriptor reflectFields[64];   ///< トップ GameMemory のフィールド
	std::int32_t  reflectSchemaCount;
	ReflectSchema reflectSchemas[8];     ///< FixedVec<struct,N> の要素型スキーマ (1 段ネスト)
};

/// @brief DLL が export すべき load 関数のシグネチャ
using ModuleLoadFn = void (*)(ModuleApi* api, void** memory);

/// @brief DLL が export すべき unload 関数のシグネチャ (optional)
using ModuleUnloadFn = void (*)(void* memory);

}  // namespace mitiru::module
