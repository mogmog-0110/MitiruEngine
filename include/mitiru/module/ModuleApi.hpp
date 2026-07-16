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

#if defined(_MSC_VER)
#  include <yvals.h>  // _ITERATOR_DEBUG_LEVEL を確定させる (build fingerprint 用)
#endif

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
///   - v15: Screen 末尾に SW ラスタライズ gating フラグを追加 (#53)。headless で
///     capture が読まないフレームの CPU ラスタライズを省く。末尾追加なので
///     旧 module (v≤14) は新 host 上で安全 (旧挙動 = 毎フレームラスタライズのまま)。
///   - v16: Screen 末尾に sprite resolver (sprite(id) 1 行描画)。host が id→Texture の
///     resolver (C 関数ポインタ + ctx) を注入し、game は `s.sprite("hero", x, y)` だけで
///     assets/sprites/<id>.png を描ける。末尾追加なので旧 module (v≤15) は新 host 上で
///     安全 (resolver メンバに触らないだけ)。
///   - v13: InputSnapshot 末尾に audioTimeSec を追加。host audio backend の再生サンプル位置
///     (秒) を毎フレーム供給し、リズムゲームが dt 積算でなく音声クロック基準で判定できる。
///   - v17: FrameIntents 末尾に save/load intent を追加 (ADR 0020)。hud.save("slot0") で
///     host が GameMemory bytes をファイルへ memcpy、load で復元 (rewind と同一機構)。
///     replay 中の load は記録済み state blob で代用され bit-exact を保つ。末尾追記で後方安全。
///   - v18: Screen 末尾に 3D facade メンバ (s.camera3D / s.drawMesh)。Screen* は on_draw で
///     DLL 境界を渡るため layout 拡張 = ABI break (v14/v15/v16 と同種の Screen 拡張)。
///   - v19: audio engine v2 (oscar-rythm リズム同期の根治)。3 点を末尾追記:
///     (1) InputSnapshot 末尾に audioLatencySec — host audio backend の出力レイテンシ (秒)。
///         masterTime はデバイスへ送った位置 = 耳より先行。判定を耳基準に補正できるよう
///         「バッファ→耳」の遅延を供給する。earTime = audioTime - audioLatency。
///     (2) SoundIntent 末尾に transport/seekSec — BGM の pause/resume/seek (会話チュートリアルで
///         BGM を止めて間を取る等)。host が ma_sound_stop/start/seek へ写像する。
///     (3) SoundIntent 末尾に scheduleSec — 「この音をこの音声クロック時刻で鳴らす」サンプル精度
///         予約 (miniaudio set_start_time)。毎フレーム clock>=t 発火のフレーム量子化 (~16ms) を消す。
///     いずれも POD 末尾追記 + host zero-init。
///   - v20: FrameIntents 末尾に つむぎ タイムライン織り intent (weaveBegin/Spin/Commit) を追加。
///     ※ weave は v21 内で撤去 (v21 未リリースの窓で ABI から除去、ADR 0024 撤去節)。
///   - v21: InputSnapshot 末尾に 2 点を追記 (ADR 0024):
///     (1) effectiveDt / paused — pause・hitStop の dt gating は simulation 入力なのに
///         記録系の外にあった (H-3)。host が実際に on_update へ渡す dt を snapshot に書き、
///         replay / resim / branch は記録値を再投入する (GUI 録画 → headless 再生の false-red 根治)。
///     (2) logicalW / logicalH — 論理解像度 (Screen logical size) の毎フレーム供給。
///         サンプルの kScreenW/kScreenH 自前 constexpr を置き換える基盤 (§8-5)。
///     あわせて暗黙 padding 2 カ所 (InputSnapshot 786→788 / SoundIntent seekSec→scheduleSec 間)
///     を明示 pad に置換し、主要 wire struct の sizeof / offset を static_assert でピン留めした。
///     ※ weave (v20 intent 群 + WeaveInbox) は v21 内で撤去 — consumer ゼロの
///        ゲーム固有機構は engine に持たず game 側 (flat POD の 3-way merge) で実装する。
///
/// @note **host は version の完全一致を要求する** (Engine_Module_Loader、D1)。上の各 version の
///       「後方安全」記述は *struct layout の追記方針* の説明であって、**古い DLL を runtime で
///       受理する意味ではない**。配列要素 (SoundIntent 等) が後の version で太ると soundIntents[] の
///       stride = 後続 field の offset がズレ、旧 DLL を新 host で動かすと silent 破損するため、
///       version != host は load/reload とも明示エラーで拒否する (= ABI bump は要再ビルド)。
constexpr std::uint32_t kCurrentApiVersion = 22;  // v22: Screen::drawModel + IRenderer3D 末尾 virtual (ADR 0027)
                                                  // v21: dt/論理解像度の InputSnapshot 統合 (ADR 0024)
                                                  // v20 の weave intent は v21 内で撤去 (未リリース窓、ADR 0024 撤去節)
                                                  // v19: audio engine v2 (audioLatencySec / transport / scheduleSec)

// ── build fingerprint (H-1/H-4 短期対策、ADR 0024 追記) ─────────────────────
// Screen* (STL 内包 class) が境界を渡り、GameMemory の new/delete も DLL 世代を跨ぐため、
// 数値 version が一致しても Debug/Release CRT・/MD//MT・toolset 系列の混成は silent な
// heap/layout 破損になる。そこで ModuleApi::version の上位 bit へ「自分がどう
// コンパイルされたか」を焼き、loader が数値 + 指紋の完全一致を検査する。
//
// bit 配置 (32bit version field):
//   [ 0..15] ABI 番号 (kCurrentApiVersion)
//   [16..17] _ITERATOR_DEBUG_LEVEL (0/1/2。未定義なら _DEBUG→2 / それ以外→0)
//   [18]     CRT 種別 (1 = DLL CRT /MD 系 (_DLL 定義)、0 = static CRT /MT 系)
//   [19..24] _MSC_VER / 100 (VC toolset 系列。非 MSVC = 0)
//   [25..31] 予約 (0)

/// @brief 自ビルドの build 指紋 bit 群。DLL / host が各自のコンパイル時に計算する。
[[nodiscard]] constexpr std::uint32_t buildFingerprintBits() noexcept
{
	std::uint32_t idl = 0;
#if defined(_ITERATOR_DEBUG_LEVEL)
	idl = static_cast<std::uint32_t>(_ITERATOR_DEBUG_LEVEL) & 0x3u;
#elif defined(_DEBUG)
	idl = 2u;  // MSVC 既定 (/MDd, /MTd)
#endif
	std::uint32_t crtDll = 0;
#if defined(_DLL)
	crtDll = 1u;  // /MD or /MDd
#endif
	std::uint32_t msc = 0;
#if defined(_MSC_VER)
	msc = static_cast<std::uint32_t>(_MSC_VER / 100) & 0x3Fu;
#endif
	return (idl << 16) | (crtDll << 18) | (msc << 19);
}

/// @brief 数値 ABI 番号 + 自ビルド指紋の合成。ModuleApi::version にはこの値が入る。
[[nodiscard]] constexpr std::uint32_t makeWireVersion(std::uint32_t abiNumber) noexcept
{
	return (abiNumber & 0xFFFFu) | buildFingerprintBits();
}

/// @brief 自ビルド構成での wire version (DLL 側は registerGame が、host 側は loader が使う)。
///        loader は **この値の完全一致のみ受理** する — 数値一致でもビルド構成が違えば拒否。
constexpr std::uint32_t kWireApiVersion = makeWireVersion(kCurrentApiVersion);

// wire version の分解 (loader の拒否メッセージ / .mtrr 診断表示用)
[[nodiscard]] constexpr std::uint32_t wireAbiNumber(std::uint32_t v) noexcept { return v & 0xFFFFu; }
[[nodiscard]] constexpr std::uint32_t wireIdl(std::uint32_t v)       noexcept { return (v >> 16) & 0x3u; }
[[nodiscard]] constexpr bool          wireCrtIsDll(std::uint32_t v)  noexcept { return ((v >> 18) & 0x1u) != 0; }
[[nodiscard]] constexpr std::uint32_t wireMscSeries(std::uint32_t v) noexcept { return (v >> 19) & 0x3Fu; }

/// @brief load 時のエントリ関数名 — host が `GetProcAddress` で探す symbol
constexpr const char* kLoadSymbol = "mitiru_module_load";

/// @brief unload 時のエントリ関数名 (optional — 無くてもよい)
constexpr const char* kUnloadSymbol = "mitiru_module_unload";

/// @brief write-blame 問い合わせ関数名 (optional — `mitiru why` に opt-in する game だけが export)。
/// @details host が GetProcAddress で解決し、分岐 byte offset を渡して「最後に書いた phase 名」を
///          引く (ADR0005: host→DLL pull、ABI/ModuleApi は不変)。
constexpr const char* kWhyBlameSymbol = "mitiru_why_blame_at";

/// @brief 巻き戻しバッファ長 (フレーム数) を返す関数名 (optional — MITIRU_REWIND_BUFFER で export)。
/// @details host が GetProcAddress で解決し、リングバッファをこのフレーム数で作る。不在なら既定 300。
///          (ADR0005: host→DLL pull。ModuleApi struct は不変 = 別 export なので後方互換)。
constexpr const char* kRewindBufferSymbol = "mitiru_module_rewind_buffer_frames";

/// @brief game が「巻き戻しで何フレーム前まで戻れるか」を宣言する (optional)。MITIRU_GAME と併記する。
/// @details host が起動時にこの値でリングバッファを作る (host の `--rewind-frames N` が指定されればそちら優先)。
/// @code
///   MITIRU_GAME(MyGame);
///   MITIRU_REWIND_BUFFER(900);   // 60fps で 15 秒分さかのぼれる
/// @endcode
#define MITIRU_REWIND_BUFFER(frames)                                           \
	extern "C" MITIRU_GAME_EXPORT                                              \
	std::uint32_t mitiru_module_rewind_buffer_frames()                        \
	{                                                                         \
		return static_cast<std::uint32_t>(frames);                            \
	}

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
	std::uint8_t _pad[3];                 ///< 明示的 padding (次の int32 の 4B align。v21 で暗黙 2B を明示化)

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

	// ── 音声出力レイテンシ (ABI v19 で追記) ───────────────────────────────
	// host の audio backend が「デバイスバッファに積んでから実際にスピーカーから出る」までの
	// 遅延 (秒)。audioTimeSec はデバイスへ送った位置なので、耳に届く位置は audioTimeSec から
	// この値だけ過去になる。リズムゲームは判定窓を耳基準へ補正するのにこれを使う:
	//   耳に届いている位置 earTime = audioTimeSec - audioLatencySec (Input::earTime())。
	// 0 = 不明 (Null/headless 等) → game は従来どおり audioTimeSec で判定する。device 固定値なので
	// 毎フレーム同じ。replay 時も moduleInputOverride が記録値で上書きするので再現する。
	double audioLatencySec;

	// ── 実効 dt / pause (ABI v21 で追記、ADR 0024) ──────────────────────
	// host が実際に on_update へ渡す dt。pause / hitStop 中は 0、通常は固定ステップ dt。
	// dt gating は simulation 入力なので snapshot に載せる — replay / resim は記録値を
	// そのまま再投入し、GUI 録画 (F8 pause / hitStop 演出込み) → headless 再生でも
	// bit-exact が成立する (H-3)。game は on_update の dt 引数で同じ値を受け取る。
	float effectiveDt;

	// ── 論理解像度 (ABI v21 で追記、§8-5) ────────────────────────────────
	// host が毎フレーム Screen の logical size を供給する。game は 1280/720 等の
	// kScreenW/kScreenH 自前 constexpr を持たずにレイアウトできる。0 = 未供給 (screen 未生成)。
	std::uint16_t logicalW;
	std::uint16_t logicalH;

	// pause 状態 (1 = cfg.paused)。effectiveDt=0 の理由の区別用 (step 実行フレームは
	// paused=1 かつ effectiveDt>0)。replay 時は記録値が再投入される。
	std::uint8_t paused;
	std::uint8_t _padTail[7];             ///< 明示的 padding (struct 全体の 8B align)
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
/// @details kind = 0:None / 1:Tint / 2:FadeOut / 3:FadeIn / 4:Shake / 5:HitStop。
///          フィールドは kind ごとに読み替える (定数の下の表を参照)。
///          struct レイアウトは v7 から不変 — kind 追加は ABI 安全 (旧 host は未知 kind を無視)。
struct VisualIntent
{
	std::uint8_t kind;       ///< kVisualIntent* (下の定数)
	std::uint8_t _pad[3];
	float        r, g, b, a; ///< 意味は kind 依存 (Tint=色+初期alpha / Fade=色 / Shake=a が振幅px)
	float        durSec;     ///< 効果尺 (秒)
	float        _reserved;  ///< 8byte 境界 padding + 将来用
};

constexpr std::uint8_t kVisualIntentNone    = 0;
constexpr std::uint8_t kVisualIntentTint    = 1;  ///< 色フラッシュ (r,g,b,a=初期alpha、durSec で減衰)
constexpr std::uint8_t kVisualIntentFadeOut = 2;  ///< 画面を r,g,b へ durSec かけて覆う
constexpr std::uint8_t kVisualIntentFadeIn  = 3;  ///< r,g,b の覆いを durSec かけて晴らす
constexpr std::uint8_t kVisualIntentShake   = 4;  ///< 画面揺れ (a=振幅px、durSec で減衰。host が決定論オフセット生成)
constexpr std::uint8_t kVisualIntentHitStop = 5;  ///< durSec 秒だけ更新停止 (dt=0 で update が呼ばれ続ける)
constexpr std::uint8_t kVisualIntentLetterbox = 6;  ///< レターボックス帯 (a=目標量 0..1、durSec で遷移。イベント演出)

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
	// v19 で末尾に追加 (後方安全: 既存 offset 不変、host が zero-init)。
	std::uint8_t transport;   ///< BGM transport: 0=なし / 1=pause / 2=resume / 3=seek (category=1 のみ)。
	std::uint8_t _pad2[3];
	float        seekSec;     ///< transport=3 (seek) の目標位置 (秒)。
	std::uint8_t _pad3[4];    ///< 明示的 padding (次の double の 8B align。v21 で暗黙 4B を明示化)
	double       scheduleSec; ///< > 0 で「この音声クロック時刻 (Input::audioTime と同基準) に鳴らす」
	                          ///< サンプル精度予約 (SE 用)。0 = 即時再生。
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
/// host が毎フレーム頭で `FrameIntents::reset()` する。DLL は helper (pushInt /
/// playSound / requestSave …) で必要な intent だけを積み、helper が count を進める。
/// reader (host) は各配列を [0, count) しか読まない。
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

	// ── セーブ/ロード intent (ABI v17 末尾追記、ADR 0020) ────────────────────
	// save = host が GameMemory bytes を save/<slot>.msav へ書く。
	// load = host がファイルから GameMemory へ memcpy (rewind と同一機構)。
	// replay 中の load は記録済み state blob で代用される (ファイル不参照 = bit-exact 保証)。
	std::uint8_t saveRequest;     ///< 1 = このフレームで save
	std::uint8_t loadRequest;     ///< 1 = このフレームで load
	std::uint8_t _padSave[2];
	char         saveSlot[28];    ///< slot 名 ([a-zA-Z0-9_-]、null 終端)
	char         loadSlot[28];

	// ── restart intent (§8-4、v21 encoding 拡張・ADR 0024 追記) ────────────
	// 1 = GameMemory を unload せず初期状態から fresh 再構築するよう host に頼む。
	// host は on_update 直後・ring 記録前に memset 0 → on_init を適用する。
	// `*this = T{}` の手運びイディオムを構造的に不要にする。InputSnapshot には
	// 載せない — intent は (GameMemory, InputSnapshot) の純関数出力なので、
	// replay / resim では update が同フレームで再発行して bit-exact に再現される。
	std::uint8_t restartRequest;
	std::uint8_t _padRestart[3];  ///< struct 全体の 8B align 維持

	/// host が毎フレーム頭で呼ぶ。counter / flag / 文字列バッファ先頭を 0 に戻す。
	/// 配列本体はクリアしない (reader は各配列を [0, count) しか読まないため)。
	void reset() noexcept
	{
		requestStop = 0;
		requestScreenshot = 0;
		paletteToggle = 0;
		statePushCount = 0;
		exportedInspectableCount = 0;
		jsToExecuteLen = 0;
		jsToExecute[0] = '\0';
		soundIntentCount = 0;
		visualIntentCount = 0;
		toolRequestCount = 0;
		saveRequest = 0;
		loadRequest = 0;
		saveSlot[0] = '\0';
		loadSlot[0] = '\0';
		restartRequest = 0;
	}

	// ── 便利メソッド (game 作者向け) ──────────────────────────────────────
	/// GameMemory をスロットへセーブするよう host に頼む (ADR 0020)。
	void requestSave(const char* slot) noexcept
	{
		saveRequest = 1;
		copyStr(saveSlot, slot, sizeof(saveSlot));
	}
	/// スロットから GameMemory を復元するよう host に頼む (ADR 0020)。
	void requestLoad(const char* slot) noexcept
	{
		loadRequest = 1;
		copyStr(loadSlot, slot, sizeof(loadSlot));
	}
	/// GameMemory を初期状態から fresh 再構築するよう host に頼む (§8-4)。
	/// unload なしで memset 0 → on_init が走る (`*this = T{}` の手運びが不要になる)。
	void requestRestart() noexcept { restartRequest = 1; }

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
	void playMusic(const char* id, float volume = 1.0f, bool loop = true,
	               float crossfadeSec = 0.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 1; s.volume = volume; s.loop = loop ? 1 : 0; s.pitchScale = 1.0f;
		// crossfade: 新曲側の fade-in 秒。旧曲のフェードアウトは host (SoundIntentRouter) が
		// 「別 id へ切り替わった」ことを検知して同じ秒数で自動発行する。
		s.fadeInSec = crossfadeSec;
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

	/// 再生中の BGM を一時停止する (再生位置を保持。停止とは違い resume で続きから鳴る、v19)。
	/// 会話形式チュートリアルで BGM を止めて間を取る等。
	void pauseMusic() noexcept { pushTransport(1, 0.0f); }
	/// pauseMusic で止めた BGM を続きから再開する (v19)。
	void resumeMusic() noexcept { pushTransport(2, 0.0f); }
	/// 再生中の BGM を指定位置 (秒) へシークする (v19)。
	void seekMusic(float positionSec) noexcept { pushTransport(3, positionSec); }

	/// 効果音を「音声クロック上の時刻 atSec」にサンプル精度で鳴らす予約 (v19)。
	/// atSec は Input::audioTime() と同じ音声クロック基準の絶対時刻。毎フレーム clock>=t を
	/// 見て発火するとフレーム量子化 (~16ms) のジッタが乗るが、これは host が audio backend の
	/// サンプル単位で発火させるので低ジッタ。リズムゲームの「次の拍でこの音」に使う。
	void scheduleSound(const char* id, double atSec, float volume = 1.0f, float pitch = 1.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 0; s.volume = volume; s.pitchScale = (pitch > 0.0f) ? pitch : 1.0f;
		s.scheduleSec = (atSec > 0.0) ? atSec : 0.0;
	}

	/// 画面を一瞬色フラッシュさせる (被弾演出など)。host が Screen::pushTint に渡す。
	void pushTint(float r, float g, float b, float a, float durationSec) noexcept
	{
		pushVisual(kVisualIntentTint, r, g, b, a, durationSec);
	}

	/// 任意 kind の視覚演出 intent を積む (フィールドの意味は kVisualIntent* の表を参照)。
	void pushVisual(std::uint8_t kind, float r, float g, float b, float a,
	                float durationSec) noexcept
	{
		const int cap = static_cast<int>(sizeof(visualIntents) / sizeof(visualIntents[0]));
		if (visualIntentCount >= cap) { return; }
		VisualIntent& v = visualIntents[visualIntentCount++];
		v = VisualIntent{};
		v.kind = kind;
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
	/// BGM transport intent (pause/resume/seek) を 1 件積む。id 不要 (現 BGM に作用)。
	void pushTransport(std::uint8_t transportKind, float seekSec) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		s.category = 1; s.transport = transportKind; s.seekSec = seekSec;
	}
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

// ── wire format のピン留め (v21) ─────────────────────────────────────────
// sizeof / offsetof を数値で固定する。game 側の /Zp・#pragma pack 等で layout が
// 変わると version 一致のまま silent 破損するため、コンパイル時に検出する。
// (fn pointer を含む ModuleApi / SeriesProbe は memcpy wire ではないため対象外。)
// これらの数値を変える変更は ABI break — kCurrentApiVersion の bump と、
// .mtrr 録画 (header frameSize = sizeof(InputSnapshot)) の録り直しが必要。
static_assert(sizeof(ActionEvent)       == 320,  "ActionEvent wire size 固定");
static_assert(sizeof(InputSnapshot)     == 5992, "InputSnapshot wire size 固定 (v21)");
static_assert(sizeof(StatePushItem)     == 4076, "StatePushItem wire size 固定");
static_assert(sizeof(InspectableExport) == 4100, "InspectableExport wire size 固定");
static_assert(sizeof(VisualIntent)      == 28,   "VisualIntent wire size 固定");
static_assert(sizeof(SoundIntent)       == 104,  "SoundIntent wire size 固定");
static_assert(sizeof(RequestToolWindow) == 192,  "RequestToolWindow wire size 固定");
static_assert(sizeof(FrameIntents)      == 297632, "FrameIntents wire size 固定 (v21: restartRequest 追記)");

static_assert(offsetof(InputSnapshot, mouseX)           == 768,  "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, actionEventCount) == 788,  "InputSnapshot layout (明示 pad 786-788)");
static_assert(offsetof(InputSnapshot, gamepadConnected) == 5912, "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, rngSeed)          == 5952, "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, audioTimeSec)     == 5960, "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, effectiveDt)      == 5976, "InputSnapshot layout (v21)");
static_assert(offsetof(InputSnapshot, logicalW)         == 5980, "InputSnapshot layout (v21)");
static_assert(offsetof(InputSnapshot, paused)           == 5984, "InputSnapshot layout (v21)");
static_assert(offsetof(SoundIntent, seekSec)            == 88,   "SoundIntent layout");
static_assert(offsetof(SoundIntent, scheduleSec)        == 96,   "SoundIntent layout (明示 pad 92-96)");
static_assert(offsetof(FrameIntents, statePushes)       == 12,     "FrameIntents layout");
static_assert(offsetof(FrameIntents, soundIntents)      == 295736, "FrameIntents layout");
static_assert(offsetof(FrameIntents, restartRequest)    == 297628, "FrameIntents layout (v21 restart)");

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
	/// @brief wire version = ABI 番号 + build 指紋 (kWireApiVersion)。engine が自分の
	///        kWireApiVersion を初期値で埋めて DLL に渡し、DLL (MITIRU_GAME の registerGame)
	///        は自分がコンパイルされた時点の kWireApiVersion で上書きする。host は
	///        **完全一致のみ受理** する (kCurrentApiVersion の @note、D1) — 数値一致でも
	///        CRT 種別 / IDL / toolset 系列の混成 (H-1/H-4) は拒否。不一致 = 要再ビルド。
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

/// @brief 申告済み reflect 記述子から GameMemory layout hash を引く (ADR 0024 追記)。
/// @details 0 = reflection 未宣言 (照合 skip)。.msav header / reload 状態温存判定が使う。
[[nodiscard]] inline std::uint64_t moduleLayoutHash(const ModuleApi& api) noexcept
{
	std::int32_t fc = api.reflectFieldCount;
	const std::int32_t fcap =
		static_cast<std::int32_t>(sizeof(api.reflectFields) / sizeof(api.reflectFields[0]));
	if (fc > fcap) { fc = fcap; }
	std::int32_t sc = api.reflectSchemaCount;
	const std::int32_t scap =
		static_cast<std::int32_t>(sizeof(api.reflectSchemas) / sizeof(api.reflectSchemas[0]));
	if (sc > scap) { sc = scap; }
	if (sc < 0)    { sc = 0; }
	return layoutHash(api.reflectFields, fc, api.reflectSchemas, sc);
}

/// @brief DLL が export すべき load 関数のシグネチャ
using ModuleLoadFn = void (*)(ModuleApi* api, void** memory);

/// @brief DLL が export すべき unload 関数のシグネチャ (optional)
using ModuleUnloadFn = void (*)(void* memory);

/// @brief write-blame 問い合わせ関数のシグネチャ (optional、`mitiru why` 用)。
/// @details 引数 = GameMemory 内の byte offset。返り値 = その byte を当該フレームで最後に書いた
///          phase 名 (game 所有の静的文字列、host は即読みする)。未対応 game は symbol 自体が無い。
using ModuleWhyBlameFn = const char* (*)(std::uint32_t offset);

/// @brief 巻き戻しバッファ長を返す関数のシグネチャ (optional、MITIRU_REWIND_BUFFER 用)。
/// @details 返り値 = リングに保持するフレーム数 (0 なら既定)。未宣言 game は symbol 自体が無い。
using ModuleRewindBufferFn = std::uint32_t (*)();

}  // namespace mitiru::module
