#pragma once

/// @file ModuleApi.hpp
/// @brief Engine ↔ Game DLL の C ABI 契約 (v0.2.0 hot reload foundation)
/// @details
/// Game DLL は **純関数** に近い形で実装される:
///
/// @code
///   (memory_in, input_snapshot, dt) → (memory_out, render, intents)
/// @endcode
///
/// **3 つの不変条件**:
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

#include <mitiru/module/Reflection.hpp>  // FieldDescriptor / ReflectSchema

// Forward declare engine types so the header is light. Concrete definitions
// come from the engine when the DLL links against `Mitiru::mitiru`.
namespace mitiru { class Screen; }
namespace mitiru::module { struct DrawContext; struct DrawCommandBuffer; }

namespace mitiru::module
{

/// @brief 現在の ABI version。breaking change で bump する。
/// @details 1 version = 1 行の台帳。詳細は各 field の vNN コメント。
///   - v1: on_init / on_update / on_draw / on_shutdown
///   - v2: InputSnapshot / FrameIntents 追加、on_update sig 変更
///   - v3: StatePushItem::strVal 160 → 3968
///   - v4: FrameIntents に soundIntents
///   - v5: InputSnapshot に gamepad
///   - v6: SoundIntent に pitchScale / fadeInSec / fadeOutSec
///   - v7: FrameIntents に visualIntents
///   - v8: InputSnapshot に rngSeed
///   - v9: ModuleApi に memorySize
///   - v10: FrameIntents に toolRequests
///   - v11: ModuleApi に seriesProbes
///   - v12: ModuleApi に reflectFields / reflectSchemas
///   - v13: InputSnapshot に audioTimeSec
///   - v14: Screen に AI 観測用 draw log (/api/ai/frame)
///   - v15: Screen に SW ラスタライズ gating
///   - v16: Screen に sprite resolver (s.sprite(id))
///   - v17: FrameIntents に save / load
///   - v18: Screen に 3D facade (s.camera3D / s.drawMesh)
///   - v19: audioLatencySec + SoundIntent transport / seekSec / scheduleSec
///   - v20: weave intent (v21 内で撤去)
///   - v21: effectiveDt / paused / logicalW / logicalH + 明示 pad + sizeof/offset static_assert
///   - v22: Screen::drawModel + IRenderer3D 末尾 virtual
///   - v23: InputSnapshot に mouseDeltaX/Y、FrameIntents に wantMouseLock (FPS 視線)
///   - v24: Screen::drawModel(clip,time) / drawModelBlend + IRenderer3D 末尾 virtual
///          drawSkinnedModel
///   - v25: Screen::camera3D(eye, target, fov, rollDeg) + Screen 末尾メンバ m_cam3DUp
///   - v26: Screen::drawModel(path, pos, rotDeg, scale) + IRenderer3D 末尾 virtual drawModelRot
///   - v27: Screen::toon3D / outline3D + Screen 末尾メンバ 4 個 + IRenderer3D 末尾 virtual
///          setOutlineParams
///   - v28: Screen::fog3D + Screen 末尾メンバ 4 個 + IRenderer3D 末尾 virtual setFog
///   - v29: Screen::shadowCaster3D + IRenderer3D 末尾 virtual setShadowCaster
///   - v30: InputSnapshot に dtByLayer[8]（layer 別時間倍率、§1-2）、FrameIntents に
///          debugDrawCount/debugDraws[256]（ゲーム内 3D デバッグ描画 intent、§9-1）
///   - v33: InputSnapshot に lastSaveResult/lastLoadResult（Hud::save/load の成否、D1）
///          + fadeProgress01（fadeOut/fadeIn の覆い alpha、D2）
///   - v34: InputSnapshot に textInput[32]/textInputLen（このフレームに確定した UTF-8 テキスト入力、IME 確定含む。J5）
///   - v35: Screen::sceneLook3D(const render::SceneLook&) + Screen 末尾メンバ 9 個（§1-12）。
///          `render/SceneLook.hpp` の 1 POD に exposure/gamma/ambient/outline/shadow/fog を
///          集約し、ISceneFx の既存 setter 群 (v22〜v29 で個別追加されたもの) へまとめて流す。
///          `ISceneFx`/`IRenderer3D` に新しい virtual は 1 つも足していない (既存 setter を
///          呼ぶだけ)。POD 末尾の `reserved[64]` を名前付きフィールドへ差し替えるだけの追加は
///          sizeof(SceneLook) を保つ限り以後 kCurrentApiVersion を上げる理由にならない
///          (vtable も Screen のメンバー構成も増えないため)。詳細: ADR 0036。
///   - v36: reflectFields を 64 → 128 に拡張。struct 配列 (敵 6 体 × 5 field 等) を持つ小さな
///          example でも 64 を使い切り、MsgQueue の待ちフレーム数 1 個で末尾 field が黙って
///          落ちていた。InputSnapshot/FrameIntents は無変更 (録画の frameSize も同じ)。ADR 0037。
///   - v37: 物理問い合わせ job (HE2 の PhysicsQueryJob / PhysicsRaycastJob 相当)。FrameIntents 末尾に
///          physicsQueryCount/physicsQueries[64]、InputSnapshot 末尾に physicsResultCount/physicsResults[64]。
///          要求は intent、結果は次フレームの InputSnapshot に乗る (= 録画に乗り、リプレイでは host の
///          物理を回さなくても同じ結果が返る)。sizeof(InputSnapshot) が変わるので .mtrr は全部再録。ADR 0038。
///   - v38: SceneLook の reserved から shadowCascadeAutoFit / shadowDistance / shadowCascadeCount を名前付きにし
///          (sizeof 140 は不変)、ISceneFx 末尾に virtual setCascadedShadowAutoFit / setShadowCascadeCount、
///          Screen 末尾メンバ 3 個。3 カスケード (近/中/遠、遠側 2 段は 1 枚のアトラス) と、カスケードの
///          分割距離と ortho の大きさをカメラ視錐台から決める (Shadow.hpp fitCascadesToCamera)。ADR 0039。
///          非 POD の game の入口 `MITIRU_GAME_OBJECTS(Game, Progress)` のために ModuleApi 末尾へ
///          `stateFlags` (kModuleStatePartial) と `on_rebuild` を追記。ADR 0040。
///
/// @note **host は version の完全一致を要求する** (Engine_Module_Loader、D1)。
///       末尾追記で既存 offset は保たれるが、古い DLL の runtime 受理はしない。
///       配列要素が太ると後続 field の offset がズレ silent 破損するため、
///       version != host は load/reload とも明示エラーで拒否する (= ABI bump は要再ビルド)。
constexpr std::uint32_t kCurrentApiVersion = 38;

// ── build fingerprint (H-1/H-4 短期対策) ─────────────────────
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
///        loader は **この値の完全一致のみ受理** する。数値一致でもビルド構成が違えば拒否。
constexpr std::uint32_t kWireApiVersion = makeWireVersion(kCurrentApiVersion);

// wire version の分解 (loader の拒否メッセージ / .mtrr 診断表示用)
[[nodiscard]] constexpr std::uint32_t wireAbiNumber(std::uint32_t v) noexcept { return v & 0xFFFFu; }
[[nodiscard]] constexpr std::uint32_t wireIdl(std::uint32_t v)       noexcept { return (v >> 16) & 0x3u; }
[[nodiscard]] constexpr bool          wireCrtIsDll(std::uint32_t v)  noexcept { return ((v >> 18) & 0x1u) != 0; }
[[nodiscard]] constexpr std::uint32_t wireMscSeries(std::uint32_t v) noexcept { return (v >> 19) & 0x3Fu; }

/// @brief load 時のエントリ関数名。host が `GetProcAddress` で探す symbol
constexpr const char* kLoadSymbol = "mitiru_module_load";

/// @brief unload 時のエントリ関数名 (optional。無くてもよい)
constexpr const char* kUnloadSymbol = "mitiru_module_unload";

/// @brief write-blame 問い合わせ関数名 (optional。`mitiru why` に opt-in する game だけが export)。
/// @details host が GetProcAddress で解決し、分岐 byte offset を渡して「最後に書いた phase 名」を
///          引く (host→DLL pull、ABI/ModuleApi は不変)。
constexpr const char* kWhyBlameSymbol = "mitiru_why_blame_at";

/// @brief 「これまでに一度でも書いた phase」問い合わせ関数名 (optional。`mitiru_why_blame_at` と
///        同じ game が追加で export する)。「なぜ変わらないのか」に答えるための逆引き (4-4)。
/// @details host が GetProcAddress で解決し、byte offset を渡してカンマ区切りの phase 名一覧
///          (game 所有の静的文字列) を引く。未対応 game は symbol 自体が無い。
constexpr const char* kEverWroteSymbol = "mitiru_why_everwrote_at";

/// @brief 巻き戻しバッファ長 (フレーム数) を返す関数名 (optional。MITIRU_REWIND_BUFFER で export)。
/// @details host が GetProcAddress で解決し、リングバッファをこのフレーム数で作る。不在なら既定 300。
///          (host→DLL pull。ModuleApi struct は不変 = 別 export なので後方互換)。
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

/// @brief 物理問い合わせ 1 件 (DLL → host の intent、v37)。結果は次フレームの `InputSnapshot::physicsResults`
///        に `tag` で対応付いて返る。host の body id は渡さない (game は `tag` だけで照合する)。
struct PhysicsQuery
{
	std::uint8_t  kind;      ///< kPhysicsQuery* (下の定数)
	std::uint8_t  _pad[3];
	float         a[3];      ///< Raycast: 始点 / OverlapSphere: 中心
	float         b[3];      ///< Raycast: 方向 (正規化推奨) / OverlapSphere: 未使用
	float         radius;    ///< Raycast: 最大距離 (0 以下なら無制限) / OverlapSphere: 半径
	std::uint32_t mask;      ///< 対象レイヤーの bit mask (0 は全レイヤー)
	std::uint32_t tag;       ///< game が付ける照合キー
};

constexpr std::uint8_t kPhysicsQueryNone          = 0;
constexpr std::uint8_t kPhysicsQueryRaycast       = 1;  ///< 最も近いヒット 1 件
constexpr std::uint8_t kPhysicsQueryOverlapSphere = 2;  ///< 球に重なる body があるか (t = 件数)

/// @brief 物理問い合わせの結果 1 件 (host → DLL、v37)。
struct PhysicsResult
{
	std::uint32_t tag;        ///< 対応する PhysicsQuery::tag
	std::uint8_t  hit;        ///< kPhysicsHit* (下の定数)
	std::uint8_t  _pad[3];
	float         point[3];   ///< Raycast: ヒット座標 (hit=1 のとき)
	float         normal[3];  ///< Raycast: ヒット面の法線 (hit=1 のとき)
	float         t;          ///< Raycast: 始点からの距離 / OverlapSphere: 重なった件数
	std::uint32_t bodyLayer;  ///< Raycast: ヒットした collider のレイヤー
};

constexpr std::uint8_t kPhysicsHitNone        = 0;  ///< 当たらなかった
constexpr std::uint8_t kPhysicsHitYes         = 1;
constexpr std::uint8_t kPhysicsHitUnsupported = 2;  ///< host が物理 world を持たない (--collision 無し等)。「当たらない」と区別する

/// @brief 1 フレーム分の input 状態 (host → DLL の push)
/// @details 全 256 VK code 対応。エッジは host が前フレーム diff から組み立て、DLL は
/// stateless view として受け取る。replay 時は snapshot 全体が記録値で置換される
/// (= bit-exact 再現の構造保証)。新 field は末尾へ追記 (既存 offset 不変)。
/// field 先頭の vNN = その field が入った ABI version。
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
	std::uint8_t _pad[3];                 ///< 4B align

	/// このフレームに溜まった CEF JS からの action event
	std::int32_t actionEventCount;
	ActionEvent  actionEvents[16];

	// v5: gamepad (主コントローラ)。非対応 platform / 未接続時は全 0
	std::int32_t  gamepadConnected;            ///< 1 = 接続中
	std::uint32_t gamepadButtonsDown;          ///< gamepad:: ビットマスク (押下中)
	std::uint32_t gamepadButtonsJustPressed;   ///< このフレームで押された
	std::uint32_t gamepadButtonsJustReleased;  ///< このフレームで離された
	float         gamepadAxes[6];              ///< gamepad::Axis 添字。stick [-1,1] / trigger [0,1]

	/// v8: 決定論 seed。`mitiru::Random rng(input->rngSeed)` で使う。0 = 未供給
	std::uint64_t rngSeed;

	/// v13: 音声クロック (秒) = backend が再生したサンプル位置。0 = 非対応 → dt 積算へ
	/// フォールバック。非ゼロ後は単調非減少を engine が保証する
	double audioTimeSec;

	/// v19: 音声出力レイテンシ (秒)。耳基準の判定は earTime = audioTimeSec - この値。0 = 不明
	double audioLatencySec;

	/// v21: host が on_update へ渡す実効 dt。pause / hitStop 中は 0
	float effectiveDt;

	/// v21: 論理解像度 (Screen logical size)。0 = 未供給
	std::uint16_t logicalW;
	std::uint16_t logicalH;

	/// v21: 1 = cfg.paused。effectiveDt=0 の理由の区別用 (step 実行は paused=1 かつ dt>0)
	std::uint8_t paused;
	std::uint8_t _padTail[7];             ///< 8B align

	/// v23: このフレームのカーソル移動量 (px、右/下が正)。ロック中も生の移動量が入る
	float mouseDeltaX;
	float mouseDeltaY;

	/// v30: layer 別の実効 dt (§1-2)。layer 0 = gameplay (= effectiveDt と同値)、
	/// layer 1 = UI（既定では hitStop で止まらない）、2..7 は予約。host が
	/// `EngineConfig::layerTimeScale` / `layerFrozenByHitStop` を適用して書く。
	/// pause の gating は全 layer 共通 (effectiveDt と同じ扱い)。
	float dtByLayer[8];

	/// v33: 直前の Hud::save()/load() 結果 (D1)。0=未実行 / 1=成功 / 2=失敗。
	/// host (drainModuleFrameIntents) が処理直後に書き、次フレームの
	/// Input::saveSucceeded()/loadSucceeded() で読める (= 1 フレーム遅れて分かる。
	/// intent → 結果は元々非同期なのでゲームは on_update の外側で状態確認する設計)。
	std::uint8_t lastSaveResult;
	std::uint8_t lastLoadResult;
	std::uint8_t _padResult[2];   ///< 4B align

	/// v33: fadeOut/fadeIn の覆い alpha (0=覆い無し / 1=完全に覆う、D2)。host
	/// (VisualIntentFx::overlay().a) をそのまま供給する。fadeOut 完了は 1.0 到達、
	/// fadeIn 完了は 0.0 到達で判定する (tint/shake/hitStop/letterbox はここに乗らない)。
	float fadeProgress01;

	/// v34: このフレームに確定した UTF-8 テキスト入力 (IME 確定含む、J5)。本命は CEF
	/// <input> 経由 (HTML UI)。ここはゲーム内の簡易テキスト入力 (プレイヤー名等) 用の
	/// 最小手段。null 終端、収まらない分は切り捨て (warnOnce なし。長文入力は CEF 側へ)。
	char textInput[32];
	std::uint8_t textInputLen;   ///< textInput の有効バイト数 (null 終端を含まない)
	std::uint8_t _padText[7];    ///< 8B align

	/// v37: 前フレームの `FrameIntents::physicsQueries` に対する結果 (要求と同じ順、tag で照合)。
	/// host が物理 world を持たなければ件数はそのままで hit=kPhysicsHitUnsupported。
	std::int32_t  physicsResultCount;
	PhysicsResult physicsResults[64];
	std::uint8_t  _padPhysics[4];  ///< 8B align
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

/// @brief 「この音を鳴らして」という DLL → host の intent
/// @details ゲームは mixer / AudioEngine pointer を持たず、id を
///          書くだけ。host が所有する audio engine が再生する。id は host が
///          assets/audio/ からロードした論理名 (拡張子抜きファイル名)。
/// @brief 「この画面演出をやって」という DLL → host の intent (#33、v7 追加)。
/// @details kind = 0:None / 1:Tint / 2:FadeOut / 3:FadeIn / 4:Shake / 5:HitStop / 6:Letterbox / 7:Rumble。
///          フィールドは kind ごとに読み替える (定数の下の表を参照)。
///          struct レイアウトは v7 から不変。kind 追加は ABI 安全 (旧 host は未知 kind を無視)。
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
constexpr std::uint8_t kVisualIntentRumble    = 7;  ///< パッド振動 (r=左モータ 0..1、g=右モータ 0..1、durSec で線形減衰。未接続/headless は no-op)

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

/// @brief 「このツール窓を開いて」という DLL → host の intent (v10 追加)。
/// @details game は Engine* を持てない ので、独立ウィンドウのツール
///          (inspector / input monitor / rewind など) を自分では開けない。代わりに
///          tool 名を書いて「開いて」と頼み、host が別 exe (mitiru_<tool>.exe) を spawn する。
///          必要なときだけ呼ぶ。既定では何も開かない (pulled UI、アトミックツール哲学)。
struct RequestToolWindow
{
	char tool[64];   ///< ツール名 (例: "inspector")。host が mitiru_<tool>.exe を探す。null 終端。
	char args[128];  ///< 追加 CLI 引数 (例: "--inspectable input")。null 終端。
};

/// @brief ゲーム内 3D デバッグ描画 1 件 (DLL → host の intent、v30、§9-1)。
/// @details kind = 1:線分 / 2:箱 / 3:球 / 4:文字。フィールドは kind ごとに読み替える
///          (1=線分: a=始点・b=終点 / 2=箱: a=中心・b=半径ベクトル / 3=球: a=中心・b[0]=半径 /
///          4=文字: a=位置・text 使用)。POD で snapshot でなく intent 側にあるため、
///          **録画に入るのはこのフレームの入力だけで、この intent 自体は録画されない**
///          (ModuleApi.hpp 冒頭の 3 不変条件どおり)。durationSec > 0 は host (Adapter) が
///          固定長リストで持って複数フレーム描き続けるが、リプレイ再生時にゲーム DLL が
///          毎フレーム出し直さない限りその減衰は再現されない (Hud::debugLine 等のコメント参照)。
struct DebugDrawIntent
{
	std::uint8_t kind;        ///< 1=線分 2=箱 3=球 4=文字
	std::uint8_t _pad[3];
	float        a[3];        ///< kind 毎の意味は上表参照
	float        b[3];        ///< kind 毎の意味は上表参照
	float        color[4];    ///< r,g,b,a
	float        durationSec; ///< 0 = このフレームのみ、> 0 = host が減衰させながら描き続ける
	char         text[32];    ///< kind=4 のみ使用、null 終端
};

/// @brief 1 フレーム分の DLL → host への要求 (intent)
/// @details
/// host が毎フレーム頭で `FrameIntents::reset()` する。DLL は helper (pushInt /
/// playSound / requestSave …) で必要な intent だけを積み、helper が count を進める。
/// reader (host) は各配列を [0, count) しか読まない。新 field は末尾へ追記
/// (既存 offset 不変)。field 先頭の vNN = その field が入った ABI version。
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

	/// v4: sound 再生要求
	std::int32_t soundIntentCount;
	SoundIntent  soundIntents[8];

	/// v7: 画面演出要求 (Tint / Fade / Shake / HitStop)
	std::int32_t visualIntentCount;
	VisualIntent visualIntents[8];

	/// v10: ツール窓 spawn 要求
	std::int32_t      toolRequestCount;
	RequestToolWindow toolRequests[4];

	// v17: セーブ/ロード。save = GameMemory bytes → save/<slot>.msav、
	// load = ファイル → GameMemory memcpy。replay 中の load は記録済み state で代用される
	std::uint8_t saveRequest;     ///< 1 = このフレームで save
	std::uint8_t loadRequest;     ///< 1 = このフレームで load
	std::uint8_t _padSave[2];
	char         saveSlot[28];    ///< slot 名 ([a-zA-Z0-9_-]、null 終端)
	char         loadSlot[28];

	/// v21: 1 = GameMemory を unload なしで初期状態から再構築するよう頼む (§8-4)。
	/// host は on_update 直後・ring 記録前に memset 0 → on_init を適用する
	std::uint8_t restartRequest;
	std::uint8_t _padRestart[3];  ///< 8B align

	/// v23: 1 = カーソルロックを望む (毎フレーム宣言、立てないフレームで解除)
	std::uint8_t wantMouseLock;
	std::uint8_t _padMouseLock[7];  ///< 8B align 維持

	/// v30: ゲーム内 3D デバッグ描画要求 (§9-1)。host が Screen 経由で重ねて描く。
	std::int32_t    debugDrawCount;
	DebugDrawIntent debugDraws[256];
	std::uint8_t    _padDebugTail[4];  ///< 8B align (double を含む SoundIntent が同居するため)

	/// v37: 物理問い合わせ (HE2 の PhysicsQueryJob 相当)。host が次フレームの InputSnapshot::physicsResults へ答える。
	std::int32_t physicsQueryCount;
	PhysicsQuery physicsQueries[64];
	std::uint8_t _padPhysicsTail[4];  ///< 8B align

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
		wantMouseLock = 0;
		debugDrawCount = 0;
		physicsQueryCount = 0;
	}

	// ── 便利メソッド (game 作者向け) ──────────────────────────────────────
	/// GameMemory をスロットへセーブするよう host に頼む。
	void requestSave(const char* slot) noexcept
	{
		saveRequest = 1;
		copyStr(saveSlot, slot, sizeof(saveSlot));
	}
	/// スロットから GameMemory を復元するよう host に頼む。
	void requestLoad(const char* slot) noexcept
	{
		loadRequest = 1;
		copyStr(loadSlot, slot, sizeof(loadSlot));
	}
	/// GameMemory を初期状態から fresh 再構築するよう host に頼む (§8-4)。
	/// unload なしで memset 0 → on_init が走る (`*this = T{}` の手運びが不要になる)。
	void requestRestart() noexcept { restartRequest = 1; }
	/// カーソルロックを頼む (FPS 視線)。毎フレーム呼ぶ。呼ばないフレームで解除される。
	void requestMouseLock() noexcept { wantMouseLock = 1; }

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

	/// 効果音をループ再生する (v22)。stopSoundId で止めるまで鳴り続ける。
	void loopSound(const char* id, float volume = 1.0f, float pitch = 1.0f,
	               float fadeInSec = 0.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 0; s.loop = 1; s.volume = volume;
		s.pitchScale = (pitch > 0.0f) ? pitch : 1.0f;
		s.fadeInSec = fadeInSec;
	}

	/// 鳴っている効果音を id で止める (v22)。fadeOutSec > 0 で減衰させてから止める。
	void stopSoundId(const char* id, float fadeOutSec = 0.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 0; s.stop = 1; s.fadeOutSec = fadeOutSec;
	}

	/// ボイスを鳴らす (category=2)。host 側は BGM / SE と独立した 1 本のスロットで再生し、
	/// 前のボイスが鳴っていれば頭出しせず差し替える (台詞は重ねない)。
	void playVoice(const char* id, float volume = 1.0f, float fadeInSec = 0.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		copyStr(s.id, id, sizeof(s.id));
		s.category = 2; s.volume = volume; s.pitchScale = 1.0f; s.fadeInSec = fadeInSec;
	}

	/// 鳴っているボイスを止める (category=2、id 不要)。fadeOutSec > 0 で減衰させてから止める。
	void stopVoice(float fadeOutSec = 0.0f) noexcept
	{
		const int cap = static_cast<int>(sizeof(soundIntents) / sizeof(soundIntents[0]));
		if (soundIntentCount >= cap) { return; }
		SoundIntent& s = soundIntents[soundIntentCount++];
		s = SoundIntent{};
		s.category = 2; s.stop = 1; s.fadeOutSec = fadeOutSec;
	}

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
	/// 必要なときだけ呼べばよい。inspector が開いている時にだけ映る (pulled UI)。
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
	/// host は mitiru_<tool>.exe を別窓で spawn する。
	void requestToolWindow(const char* tool, const char* args = "") noexcept
	{
		const int cap = static_cast<int>(sizeof(toolRequests) / sizeof(toolRequests[0]));
		if (toolRequestCount >= cap) { return; }
		RequestToolWindow& r = toolRequests[toolRequestCount++];
		r = RequestToolWindow{};
		copyStr(r.tool, tool, sizeof(r.tool));
		copyStr(r.args, args ? args : "", sizeof(r.args));
	}

	/// 線分のデバッグ描画を積む (§9-1)。durationSec=0 はこのフレームのみ。
	void pushDebugLine(const float a[3], const float b[3], const float color[4],
	                    float durationSec = 0.0f) noexcept
	{
		if (DebugDrawIntent* d = nextDebugDraw())
		{
			d->kind = 1;
			for (int i = 0; i < 3; ++i) { d->a[i] = a[i]; d->b[i] = b[i]; }
			for (int i = 0; i < 4; ++i) { d->color[i] = color[i]; }
			d->durationSec = durationSec;
		}
	}
	/// 箱 (中心 + 半径ベクトル) のデバッグ描画を積む。
	void pushDebugBox(const float center[3], const float halfExtents[3], const float color[4],
	                   float durationSec = 0.0f) noexcept
	{
		if (DebugDrawIntent* d = nextDebugDraw())
		{
			d->kind = 2;
			for (int i = 0; i < 3; ++i) { d->a[i] = center[i]; d->b[i] = halfExtents[i]; }
			for (int i = 0; i < 4; ++i) { d->color[i] = color[i]; }
			d->durationSec = durationSec;
		}
	}
	/// 球のデバッグ描画を積む。
	void pushDebugSphere(const float center[3], float radius, const float color[4],
	                      float durationSec = 0.0f) noexcept
	{
		if (DebugDrawIntent* d = nextDebugDraw())
		{
			d->kind = 3;
			for (int i = 0; i < 3; ++i) { d->a[i] = center[i]; }
			d->b[0] = radius; d->b[1] = 0.0f; d->b[2] = 0.0f;
			for (int i = 0; i < 4; ++i) { d->color[i] = color[i]; }
			d->durationSec = durationSec;
		}
	}
	/// 文字のデバッグ描画を積む (world 座標に投影して描く)。
	void pushDebugText(const float pos[3], const char* text, const float color[4],
	                    float durationSec = 0.0f) noexcept
	{
		if (DebugDrawIntent* d = nextDebugDraw())
		{
			d->kind = 4;
			for (int i = 0; i < 3; ++i) { d->a[i] = pos[i]; }
			for (int i = 0; i < 4; ++i) { d->color[i] = color[i]; }
			d->durationSec = durationSec;
			copyStr(d->text, text, sizeof(d->text));
		}
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

	/// 空き物理問い合わせスロットを 1 つ確保する。満杯 (64 件) なら nullptr。結果は次フレーム。
	PhysicsQuery* nextPhysicsQuery() noexcept
	{
		const int cap = static_cast<int>(sizeof(physicsQueries) / sizeof(physicsQueries[0]));
		if (physicsQueryCount >= cap) { return nullptr; }
		PhysicsQuery& q = physicsQueries[physicsQueryCount++];
		q = PhysicsQuery{};
		return &q;
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
	/// 空きデバッグ描画スロットを 1 つ確保する。満杯なら nullptr。
	DebugDrawIntent* nextDebugDraw() noexcept
	{
		const int cap = static_cast<int>(sizeof(debugDraws) / sizeof(debugDraws[0]));
		if (debugDrawCount >= cap) { return nullptr; }
		DebugDrawIntent& d = debugDraws[debugDrawCount++];
		d = DebugDrawIntent{};
		return &d;
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
// これらの数値を変える変更は ABI break。kCurrentApiVersion の bump と、
// .mtrr 録画 (header frameSize = sizeof(InputSnapshot)) の録り直しが必要。
static_assert(sizeof(ActionEvent)       == 320,  "ActionEvent wire size 固定");
static_assert(sizeof(PhysicsQuery)      == 40,   "PhysicsQuery wire size 固定 (v37)");
static_assert(sizeof(PhysicsResult)     == 40,   "PhysicsResult wire size 固定 (v37)");
static_assert(sizeof(InputSnapshot)     == 8648, "InputSnapshot wire size 固定 (v37: physicsResults 追記)");
static_assert(sizeof(StatePushItem)     == 4076, "StatePushItem wire size 固定");
static_assert(sizeof(InspectableExport) == 4100, "InspectableExport wire size 固定");
static_assert(sizeof(VisualIntent)      == 28,   "VisualIntent wire size 固定");
static_assert(sizeof(SoundIntent)       == 104,  "SoundIntent wire size 固定");
static_assert(sizeof(RequestToolWindow) == 192,  "RequestToolWindow wire size 固定");
static_assert(sizeof(DebugDrawIntent)   == 80,   "DebugDrawIntent wire size 固定");
static_assert(sizeof(FrameIntents)      == 320696, "FrameIntents wire size 固定 (v37: physicsQueries 追記)");

static_assert(offsetof(InputSnapshot, mouseX)           == 768,  "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, actionEventCount) == 788,  "InputSnapshot layout (明示 pad 786-788)");
static_assert(offsetof(InputSnapshot, gamepadConnected) == 5912, "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, rngSeed)          == 5952, "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, audioTimeSec)     == 5960, "InputSnapshot layout");
static_assert(offsetof(InputSnapshot, effectiveDt)      == 5976, "InputSnapshot layout (v21)");
static_assert(offsetof(InputSnapshot, logicalW)         == 5980, "InputSnapshot layout (v21)");
static_assert(offsetof(InputSnapshot, paused)           == 5984, "InputSnapshot layout (v21)");
static_assert(offsetof(InputSnapshot, mouseDeltaX)      == 5992, "InputSnapshot layout (v23)");
static_assert(offsetof(InputSnapshot, dtByLayer)        == 6000, "InputSnapshot layout (v30)");
static_assert(offsetof(InputSnapshot, lastSaveResult)   == 6032, "InputSnapshot layout (v33)");
static_assert(offsetof(InputSnapshot, fadeProgress01)   == 6036, "InputSnapshot layout (v33)");
static_assert(offsetof(InputSnapshot, textInput)        == 6040, "InputSnapshot layout (v34)");
static_assert(offsetof(InputSnapshot, textInputLen)     == 6072, "InputSnapshot layout (v34)");
static_assert(offsetof(InputSnapshot, physicsResultCount) == 6080, "InputSnapshot layout (v37)");
static_assert(offsetof(FrameIntents, physicsQueryCount)   == 318128, "FrameIntents layout (v37)");
static_assert(offsetof(SoundIntent, seekSec)            == 88,   "SoundIntent layout");
static_assert(offsetof(SoundIntent, scheduleSec)        == 96,   "SoundIntent layout (明示 pad 92-96)");
static_assert(offsetof(DebugDrawIntent, a)              == 4,    "DebugDrawIntent layout");
static_assert(offsetof(DebugDrawIntent, b)              == 16,   "DebugDrawIntent layout");
static_assert(offsetof(DebugDrawIntent, color)          == 28,   "DebugDrawIntent layout");
static_assert(offsetof(DebugDrawIntent, text)           == 48,   "DebugDrawIntent layout");
static_assert(offsetof(FrameIntents, statePushes)       == 12,     "FrameIntents layout");
static_assert(offsetof(FrameIntents, soundIntents)      == 295736, "FrameIntents layout");
static_assert(offsetof(FrameIntents, restartRequest)    == 297628, "FrameIntents layout (v21 restart)");
static_assert(offsetof(FrameIntents, wantMouseLock)     == 297632, "FrameIntents layout (v23)");
static_assert(offsetof(FrameIntents, debugDrawCount)    == 297640, "FrameIntents layout (v30)");
static_assert(offsetof(FrameIntents, debugDraws)        == 297644, "FrameIntents layout (v30)");

// ── 観測 probe (ABI v11) ───────────────────────────────────────

/// @brief GameMemory から追跡スカラーを引く純関数 (C 生関数ポインタなので POD)
/// @details host が GameMemory bytes へのポインタを渡して呼ぶ。読むのは GameMemory のみ
///          (static / 外部状態を読むと replay 非再現になる)。capture を持たない lambda は
///          暗黙変換可、capture ありは変換不可でコンパイル拒否される (footgun 防止)。
using SeriesProbeFn = double (*)(const void* gameMemory);

/// @brief 「GameMemory のこの値を rewind graph で追って」という DLL → host の宣言 (v11)。
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
	///        **完全一致のみ受理** する (kCurrentApiVersion の @note、D1)。数値一致でも
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

	/// @brief GameMemory (DLL が *memory にセットした state) のバイト数 (ABI v9)。
	/// @details DLL は `api->memorySize = sizeof(自分の GameMemory)` を申告する。0 = 未申告で、
	///          host は GameMemory を記録せず観測 view.* にフォールバックする。host は replay
	///          記録時にこのサイズだけ opaque に memcpy する (中身は parse しない)。
	std::uint32_t memorySize;

	/// @brief 観測 probe テーブル (ABI v11)。末尾追記なので v≤10 module は後方安全
	///        (zero-init で seriesProbeCount=0 = 観測なし)。`MITIRU_GAME_SERIES` が埋める。
	std::int32_t seriesProbeCount;
	SeriesProbe  seriesProbes[8];

	/// @brief GameMemory リフレクション記述表 (ABI v12)。末尾追記で v≤11 後方安全
	///        (zero-init で reflectFieldCount=0 = 非対応)。`MITIRU_REFLECT` が埋める。host が
	///        GameMemory バイト列を構造化 JSON 化して AI に全状態を開放する。
	std::int32_t  reflectFieldCount;
	FieldDescriptor reflectFields[128];  ///< トップ GameMemory のリーフ (v36 で 64 → 128)
	std::int32_t  reflectSchemaCount;
	ReflectSchema reflectSchemas[8];     ///< FixedVec<struct,N> の要素型スキーマ (1 段ネスト)

	/// @brief `on_draw` の代わりに POD コマンドバッファへ積む経路 (ABI v31、ADR 0025)。
	/// null なら未対応 (`registerGame` は game が `draw(Canvas&)` を持つ時だけ埋める)。
	/// 両方 non-null な module では host がこちらを優先し、`on_draw` は呼ばない。
	/// ctx / out は per-frame 引数。DLL は保持しない。末尾追記なので v≤30 module は後方安全
	/// (zero-init で nullptr = 旧経路のまま)。
	void (*on_draw_commands)(void* memory, const module::DrawContext* ctx,
	                          module::DrawCommandBuffer* out) = nullptr;

	/// @brief GameMemory の性質 (ABI v38、ADR 0040)。`kModuleStatePartial` が立っていれば、GameMemory は
	/// 進行データだけで、場面の中身 (オブジェクトの木) は DLL 内に非 POD で持っている。host は
	/// 「GameMemory = 全状態」を前提にした操作 (rewind の scrub / resim / 分岐 / 候補の並走) を断り、
	/// セーブ・ロード・録画の blob は進行データに対して働かせる。0 = 従来どおり全状態が flat POD。
	std::uint32_t stateFlags = 0;
	std::uint32_t _padStateFlags = 0;

	/// @brief host が GameMemory を外から書き換えた直後に呼ぶ (ロード、replay 中のロード代用)。
	/// DLL は GameMemory (進行データ) から場面を組み立て直す。null = 不要 (全状態 POD の game)。
	/// reason は `kModuleRebuild*`。HE2 の「配置データから作り直す」(Restart / RespawnByObjectId) に当たる。
	void (*on_rebuild)(void* memory, std::uint32_t reason) = nullptr;
};

/// @brief `ModuleApi::stateFlags`: GameMemory は状態の一部 (進行データ) だけを持つ。
inline constexpr std::uint32_t kModuleStatePartial = 1u << 0;

/// @brief `ModuleApi::on_rebuild` の reason。
inline constexpr std::uint32_t kModuleRebuildRestore = 1;  ///< host が GameMemory を書き戻した (ロード等)

/// @brief 申告済み reflect 記述子から GameMemory layout hash を引く。
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

/// @brief everWrote 問い合わせ関数のシグネチャ (optional、`mitiru_why_blame_at` と対の export)。
/// @details 引数 = GameMemory 内の byte offset。返り値 = これまでに一度でもその byte を書いた
///          phase 名をカンマ区切りにしたもの (game 所有の静的文字列)。未対応 game は symbol 自体が無い。
using ModuleWhyEverWroteFn = const char* (*)(std::uint32_t offset);

/// @brief 巻き戻しバッファ長を返す関数のシグネチャ (optional、MITIRU_REWIND_BUFFER 用)。
/// @details 返り値 = リングに保持するフレーム数 (0 なら既定)。未宣言 game は symbol 自体が無い。
using ModuleRewindBufferFn = std::uint32_t (*)();

}  // namespace mitiru::module
