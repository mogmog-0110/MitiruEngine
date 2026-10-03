#pragma once

/// @file Game.hpp
/// @brief 初心者向けの薄い C++ ラッパ。`void*` / 生ポインタ / VK 添字を隠す。
/// @details
/// `ModuleApi.hpp` の C-ABI (`void* memory` / `InputSnapshot*` / `FrameIntents*`)
/// はホットリロード・録画再生・POD 境界 のために必要だが、ゲーム作者が
/// それを直に触るのは初心者に厳しい。この header はその上に「普通のゲームフレーム
/// ワーク」の手触りを乗せる:
///
/// @code
///   #include <mitiru/module/Game.hpp>
///   using namespace mitiru;
///
///   struct MyGame {                 // 状態はここに置くだけ (ホストが保持する)
///       float x = 600;
///       int   score = 0;
///       void update(Input in, Hud hud, float dt) {
///           if (in.down(Key::Right)) x += 320 * dt;   // → で右へ
///           hud.set("view.hud.score", score);          // 画面へ送る
///       }
///       void draw(Screen& screen) { /* 描画 */ }
///   };
///   MITIRU_GAME(MyGame)             // これだけで DLL の入口が出来る
/// @endcode
///
/// `init` / `update` / `draw` はすべて任意。書いたものだけ呼ばれる。`MyGame` は flat POD 必須
/// (host が bytes として記録・rewind・replay・セーブする単一の state 源。ADR 0017)。
/// クラスの木や仮想関数で書きたい game は `MITIRU_GAME_OBJECTS(Game, Progress)` を使う
/// (進行データだけ POD、場面の中身は DLL 内のオブジェクト。ADR 0040)。中身は `ModuleApi.hpp`
/// の C-ABI そのままで、ホスト側は何も変わらない。

#include <algorithm>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <mitiru/core/Color.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/core/PodTiming.hpp>  // POD タイマー/トゥイーン (GameMemory に埋めて使う)
#include <mitiru/core/MenuCursor.hpp>  // メニューのスティック/十字ナビ (POD)
#include <mitiru/core/Collide2D.hpp>   // タイルマップ AABB 移動解決 (moveAndCollide)
#include <mitiru/debug/ToolRegistry.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/input/GamepadFeatures.hpp>  // in.pad(n).kind() の機種と電源の enum
#include <mitiru/input/InputDeviceKind.hpp>  // in.inputDevice() / in.padFamily()
#include <mitiru/module/DrawCommands.hpp>
#include <mitiru/module/LayoutFingerprint.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/ModuleReflection.hpp>

namespace mitiru
{

// 図形の基本型の短い別名。作者は sgc:: を書かなくてよい (色 Color は <mitiru/core/Color.hpp>)。
using Rect = sgc::Rectf;   ///< 矩形 {x, y, 幅, 高さ}
using Vec2 = sgc::Vec2f;   ///< 2D 座標 / ベクトル
using Vec3 = sgc::Vec3f;   ///< 3D 座標 / ベクトル (デバッグ描画等)

/// `Input::dt(Layer)` / `EngineConfig::layerTimeScale` の添字 (v30、§1-2)。
/// 2..7 は予約 (ゲームが自由に使ってよいが、host は既定倍率 1.0・hitStop で停止させる)。
enum class Layer : int { Gameplay = 0, Ui = 1 };

/// よく使うキー (値は Windows の仮想キーコード)。一覧に無いキーも `Key{0x..}` で渡せる。
/// 注意: 英字の VK は大文字 ('A'=0x41..'Z') のみ。`Key{'a'}` (小文字) は別の値になり
/// 一致しない。文字から作るときは `key('a')` ヘルパを使う (自動で大文字化する)。
enum class Key : int
{
	Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28,
	Space = 0x20, Enter = 0x0D, Escape = 0x1B, Tab = 0x09, Shift = 0x10, Ctrl = 0x11,
	Alt = 0x12, CapsLock = 0x14, Backspace = 0x08, Delete = 0x2E, Insert = 0x2D,
	Home = 0x24, End = 0x23, PageUp = 0x21, PageDown = 0x22,
	F1 = 0x70, F2 = 0x71, F3 = 0x72, F4 = 0x73, F5 = 0x74, F6 = 0x75,
	F7 = 0x76, F8 = 0x77, F9 = 0x78, F10 = 0x79, F11 = 0x7A, F12 = 0x7B,
	A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G', H = 'H', I = 'I',
	J = 'J', K = 'K', L = 'L', M = 'M', N = 'N', O = 'O', P = 'P', Q = 'Q', R = 'R',
	S = 'S', T = 'T', U = 'U', V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z',
	Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
	Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',
};

/// ゲームパッドのボタン。値は ModuleApi の gamepad:: ビット。
enum class Pad : std::uint32_t
{
	Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008,
	Start = 0x0010, Back = 0x0020, LStick = 0x0040, RStick = 0x0080,
	LB = 0x0100, RB = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
};

/// マウスのボタン。`in.mouseDown(Mouse::X1)` のように渡す (int で 0..4 を渡しても同じ)。
enum class Mouse : int { Left = 0, Right = 1, Middle = 2, X1 = 3, X2 = 4 };

/// スティックの傾き (各成分 -1..1)。
struct Stick { float x, y; };

/// タッチパッドの指 1 本 (`in.pad(n).touch(i)`)。x, y は 0..1 (左上が 0)。
struct PadTouchPoint { bool down; float x, y; };

/// パッド 1 台の読み取り (`in.pad(n)` が返す)。コピーは安全 (ポインタ 2 個)。
class PadInput
{
public:
	explicit PadInput(const module::GamepadState* s, const module::GamepadExt* e = nullptr) noexcept : s_(s), e_(e) {}

	bool connected() const noexcept { return s_ != nullptr && s_->connected != 0; }
	bool down(Pad b)     const noexcept { return has(&module::GamepadState::buttonsDown, b); }
	bool pressed(Pad b)  const noexcept { return has(&module::GamepadState::buttonsJustPressed, b); }
	bool released(Pad b) const noexcept { return has(&module::GamepadState::buttonsJustReleased, b); }
	Stick leftStick()  const noexcept { return { axis(0), axis(1) }; }
	Stick rightStick() const noexcept { return { axis(2), axis(3) }; }
	float leftTrigger()  const noexcept { return axis(4); }
	float rightTrigger() const noexcept { return axis(5); }

	// ── v48: 機種・電池・ジャイロ・タッチパッド (台本では gyro / accel / touch の行で流せる) ──
	/// ボタンの絵 (A/B か ×/○ か) を選ぶための機種。A ビットは機種によらず下のボタン
	input::PadKind kind() const noexcept { return static_cast<input::PadKind>(e_ != nullptr ? e_->kind : 0); }
	input::PadPower power() const noexcept { return static_cast<input::PadPower>(e_ != nullptr ? e_->power : 0); }
	int battery() const noexcept { return e_ != nullptr ? e_->battery : -1; }   ///< 0..100。分からなければ -1
	/// そのパッドが持つ機能 (module::kPadCapGyro など) か
	bool hasCap(std::uint8_t cap) const noexcept { return e_ != nullptr && (e_->caps & cap) != 0; }
	/// ジャイロと加速度が今の値か (`hud.padMotion(n, true)` で有効にする。電池を使う)
	bool motionActive() const noexcept { return e_ != nullptr && e_->motionActive != 0; }
	Vec3 gyro()  const noexcept { return e_ != nullptr ? Vec3{e_->gyro[0], e_->gyro[1], e_->gyro[2]} : Vec3{0, 0, 0}; }    ///< rad/s
	Vec3 accel() const noexcept { return e_ != nullptr ? Vec3{e_->accel[0], e_->accel[1], e_->accel[2]} : Vec3{0, 0, 0}; } ///< m/s^2
	PadTouchPoint touch(int finger) const noexcept
	{
		if (e_ == nullptr || finger < 0 || finger > 1) { return {false, 0.0f, 0.0f}; }
		const auto& t = e_->touch[finger];
		return {t.down != 0, t.x, t.y};
	}
	bool touchpadDown()     const noexcept { return e_ != nullptr && (e_->extraDown & module::kPadExtraTouchpad) != 0; }
	bool touchpadPressed()  const noexcept { return e_ != nullptr && (e_->extraPressed & module::kPadExtraTouchpad) != 0; }
	bool touchpadReleased() const noexcept { return e_ != nullptr && (e_->extraReleased & module::kPadExtraTouchpad) != 0; }

private:
	bool has(std::uint32_t module::GamepadState::* field, Pad b) const noexcept
	{
		return s_ != nullptr && ((s_->*field) & static_cast<std::uint32_t>(b)) != 0;
	}
	float axis(int a) const noexcept { return (s_ != nullptr) ? s_->axes[a] : 0.0f; }

	const module::GamepadState* s_;
	const module::GamepadExt*   e_;
};

/// 度 → ラジアン変換。Screen の drawArc / drawPie / pushRotation はラジアン指定なので、
/// 度で書きたいときは `deg(90)` のように包んで渡す (drawRectRotated / drawGroup は度のまま)。
[[nodiscard]] constexpr float deg(float degrees) noexcept
{
	return degrees * (3.14159265358979323846f / 180.0f);
}

/// アクションマップの 1 行。「論理アクション → キー/パッドの束」。
/// 表は constexpr 定数 (DLL 焼き込み) か GameMemory のどちらかに置くこと
/// (リバインド UI を作るなら GameMemory に置けば、キー設定変更も記録/巻き戻し対象になる)。
/// 未使用スロットは 0 のままで無害 (Key 0 = 無効 VK、Pad 0 = 空ビット)。
///
/// ```cpp
/// enum class Act : std::uint8_t { Jump, Confirm };
/// static constexpr mitiru::Binding<Act> kMap[] = {
///     { Act::Jump,    { Key::Space, Key::W, Key::Up },    { Pad::A } },
///     { Act::Confirm, { Key::Space, Key::Z, Key::Enter }, { Pad::A, Pad::Start } },
/// };
/// // update 内: if (in.pressed(kMap, Act::Jump)) jump();
/// ```
template <typename Act>
struct Binding
{
	Act act;        ///< 論理アクション (ゲーム定義の enum)
	Key keys[4];    ///< この内どれかが該当すれば成立 (OR)。未使用は 0 のまま
	Pad pads[2];    ///< 同上 (パッドボタン)。未使用は 0 のまま
};

/// 2D カメラ (POD)。GameMemory に置けば巻き戻し・リプレイ対象に自動編入。
/// update でカメラ位置を動かし、draw の冒頭で `s.applyCamera(mem.cam)`、
/// 末尾 (HUD など画面固定要素の前) で `s.endCamera()`。
/// update / draw で同じ変換を二重実装するバグ源 (カメラ計算の重複) を消すための一元化。
struct Camera
{
	float x    = 0.0f;   ///< 注視点 (world 座標)。画面中央に来る
	float y    = 0.0f;
	float zoom = 1.0f;   ///< 1 = 等倍、2 = 2 倍拡大
};

/// 文字 → Key 変換。英字の仮想キーコードは大文字 ('A'..'Z') のみ有効なので、
/// 英小文字は自動で大文字化する (`key('a') == Key::A`)。数字 '0'..'9' はそのまま。
[[nodiscard]] constexpr Key key(char c) noexcept
{
	return (c >= 'a' && c <= 'z') ? static_cast<Key>(c - 'a' + 'A') : static_cast<Key>(c);
}

/// 入力の読み取り (`InputSnapshot` の薄いビュー)。コピーは安全 (ポインタ 1 個)。
class Input
{
public:
	explicit Input(const module::InputSnapshot* s) noexcept : s_(s) {}

	bool down(Key k)     const noexcept { return held((int)k, s_->keysDown); }          ///< 押されている間ずっと
	bool pressed(Key k)  const noexcept { return held((int)k, s_->keysJustPressed); }   ///< 押した瞬間だけ
	bool released(Key k) const noexcept { return held((int)k, s_->keysJustReleased); }   ///< 離した瞬間だけ

	/// host が止めている種類 (0 = 動作中 / 1 = ingame ポーズメニュー / 2 = host の debug 停止 / 3 = object だけ停止)。
	/// `MITIRU_PAUSE_LAYERS_BY_KIND` で種類ごとに動かす layer を分けられる。
	std::uint8_t pauseKind() const noexcept { return s_->paused; }
	bool paused() const noexcept { return s_->paused != 0; }

	float mouseX() const noexcept { return s_->mouseX; }
	float mouseY() const noexcept { return s_->mouseY; }
	float mouseDeltaX() const noexcept { return s_->mouseDeltaX; }  ///< このフレームの移動量 (px、右が正)
	float mouseDeltaY() const noexcept { return s_->mouseDeltaY; }  ///< 同 (下が正)。ロック中も動く (FPS 視線)
	/// 押されている間 (0=左 1=右 2=中 3=X1 (戻る) 4=X2 (進む))
	bool  mouseDown(int button = 0) const noexcept
	{
		return mouseFlag(button, s_->mouseButtonsDown, s_->mouseXButtonsDown);
	}
	bool  mousePressed(int button = 0) const noexcept   ///< 押した瞬間だけ
	{
		return mouseFlag(button, s_->mouseButtonsJustPressed, s_->mouseXButtonsJustPressed);
	}
	bool  mouseReleased(int button = 0) const noexcept  ///< 離した瞬間だけ
	{
		return mouseFlag(button, s_->mouseButtonsJustReleased, s_->mouseXButtonsJustReleased);
	}
	bool  mouseDown(Mouse b)     const noexcept { return mouseDown(static_cast<int>(b)); }
	bool  mousePressed(Mouse b)  const noexcept { return mousePressed(static_cast<int>(b)); }
	bool  mouseReleased(Mouse b) const noexcept { return mouseReleased(static_cast<int>(b)); }
	/// このフレームに回したホイール (ノッチ数、+ = 奥)。`zoom += in.wheel() * 0.1f` のように使う。
	float wheel()  const noexcept { return s_->mouseWheel; }
	/// 横ホイール (チルト、+ = 右)。
	float wheelH() const noexcept { return s_->mouseWheelH; }

	/// UI (RML) のボタン等から届いたアクションが来たフレームだけ true (例: "game.restart")。
	bool action(const char* name) const noexcept
	{
		if (name == nullptr) { return false; }
		for (int i = 0; i < s_->actionEventCount; ++i)
		{
			const char* a = s_->actionEvents[i].name;
			int j = 0;
			while (a[j] != '\0' && name[j] != '\0' && a[j] == name[j]) { ++j; }
			if (a[j] == '\0' && name[j] == '\0') { return true; }
		}
		return false;
	}

	/// action に付いてきた payload (JSON 文字列) を返す。無ければ nullptr。
	/// UI のボタンが値 (難易度・スロット番号など) を伴うとき使う。
	const char* actionPayload(const char* name) const noexcept
	{
		if (name == nullptr) { return nullptr; }
		for (int i = 0; i < s_->actionEventCount; ++i)
		{
			const char* a = s_->actionEvents[i].name;
			int j = 0;
			while (a[j] != '\0' && name[j] != '\0' && a[j] == name[j]) { ++j; }
			if (a[j] == '\0' && name[j] == '\0') { return s_->actionEvents[i].payloadJson; }
		}
		return nullptr;
	}

	// ── payload JSON の値ヘルパ (D5) ────────────────────────────────────
	// フラットな "key": value を 1 つ拾う。ネストした JSON は非対応 (それが要るなら actionPayload() を自分で読む)。

	/// payload の JSON から key の int 値を読む (無ければ defaultValue)。
	int actionPayloadInt(const char* name, const char* key, int defaultValue = 0) const noexcept
	{
		const char* v = findJsonValue(actionPayload(name), key);
		return (v != nullptr) ? static_cast<int>(std::strtol(v, nullptr, 10)) : defaultValue;
	}
	/// payload の JSON から key の float 値を読む (無ければ defaultValue)。
	float actionPayloadFloat(const char* name, const char* key, float defaultValue = 0.0f) const noexcept
	{
		const char* v = findJsonValue(actionPayload(name), key);
		return (v != nullptr) ? std::strtof(v, nullptr) : defaultValue;
	}
	/// payload の JSON から key の文字列値を out へコピーする。outCap に収まりきらず
	/// 切り詰めた場合は false を返す (out 自体は切り詰めた内容で null 終端済み。
	/// 呼び出し元が戻り値を無視しても壊れた文字列にはならない)。
	/// エスケープは `\"` のみ簡易対応 (それ以上凝った文字列は actionPayload() を自分で読む)。
	bool actionPayloadString(const char* name, const char* key, char* out, std::size_t outCap) const noexcept
	{
		if (out == nullptr || outCap == 0) { return false; }
		out[0] = '\0';
		const char* v = findJsonValue(actionPayload(name), key);
		if (v == nullptr || *v != '"') { return false; }
		++v;
		std::size_t i = 0;
		while (*v != '\0' && *v != '"' && i + 1 < outCap)
		{
			if (*v == '\\' && v[1] == '"') { ++v; }
			out[i++] = *v++;
		}
		out[i] = '\0';
		return *v == '"';  // ループを抜けた理由が outCap 不足なら (*v はまだ終端引用符でない) 切り詰め
	}

	// ── ゲームパッド (繋がっている全台の合成。1 人用はこれで足りる。台ごとは pad(n)) ──────
	bool padConnected() const noexcept { return s_->gamepadConnected != 0; }
	bool padDown(Pad b)     const noexcept { return (s_->gamepadButtonsDown        & static_cast<std::uint32_t>(b)) != 0; }
	bool padPressed(Pad b)  const noexcept { return (s_->gamepadButtonsJustPressed  & static_cast<std::uint32_t>(b)) != 0; }
	bool padReleased(Pad b) const noexcept { return (s_->gamepadButtonsJustReleased & static_cast<std::uint32_t>(b)) != 0; }
	Stick leftStick()  const noexcept { return { s_->gamepadAxes[0], s_->gamepadAxes[1] }; }
	Stick rightStick() const noexcept { return { s_->gamepadAxes[2], s_->gamepadAxes[3] }; }
	float leftTrigger()  const noexcept { return s_->gamepadAxes[4]; }
	float rightTrigger() const noexcept { return s_->gamepadAxes[5]; }
	/// n 台目のパッド (0..3、ローカル対戦用)。番号は XInput の player 番号どおりで、誰かが抜いても
	/// 他の人の番号は動かない。範囲外は未接続として読める。例: `in.pad(1).pressed(Pad::A)`
	PadInput pad(int n) const noexcept
	{
		const int count = static_cast<int>(sizeof(s_->gamepads) / sizeof(s_->gamepads[0]));
		const bool in = (n >= 0 && n < count);
		return PadInput{in ? &s_->gamepads[n] : nullptr, in ? &s_->gamepadsExt[n] : nullptr};
	}

	// ── アクションマップ (キーもパッドも 1 つの名前で。表 = 操作仕様書) ──────
	/// 表の中で act に束ねたキー/パッドのどれかが「押されている間」true。
	template <typename Act, std::size_t N>
	bool down(const Binding<Act> (&map)[N], Act act) const noexcept
	{
		return boundAny(map, N, act, s_->keysDown, s_->gamepadButtonsDown);
	}
	/// 同じく「押した瞬間」true (キー/パッドどちらのエッジでも)。
	template <typename Act, std::size_t N>
	bool pressed(const Binding<Act> (&map)[N], Act act) const noexcept
	{
		return boundAny(map, N, act, s_->keysJustPressed, s_->gamepadButtonsJustPressed);
	}
	/// 同じく「離した瞬間」true。可変ジャンプの頭打ち等。
	template <typename Act, std::size_t N>
	bool released(const Binding<Act> (&map)[N], Act act) const noexcept
	{
		return boundAny(map, N, act, s_->keysJustReleased, s_->gamepadButtonsJustReleased);
	}

	// ── 定番セット (宣言ゼロで動く既定。例外が出てきたら Binding 表へ) ────────
	/// 「決定」を押した瞬間 (Space / Z / Enter + パッド A / Start)。メニュー送り等。
	bool confirmPressed() const noexcept
	{
		return pressed(Key::Space) || pressed(Key::Z) || pressed(Key::Enter) ||
		       padPressed(Pad::A) || padPressed(Pad::Start);
	}
	/// 「キャンセル」を押した瞬間 (Escape + パッド B / Back)。
	bool cancelPressed() const noexcept
	{
		return pressed(Key::Escape) || padPressed(Pad::B) || padPressed(Pad::Back);
	}
	/// 移動入力の合成 (矢印 + WASD + 十字キー + 左スティック)。各成分 -1..1。
	/// `x += in.move().x * speed * dt` だけで全デバイス対応の移動になる。
	Stick move() const noexcept
	{
		float x = s_->gamepadAxes[0];
		float y = -s_->gamepadAxes[1];   // スティック生値は +y=上。move() は画面系 (+y=下) なので反転
		if (down(Key::Left)  || down(Key::A) || padDown(Pad::Left))  { x -= 1.0f; }
		if (down(Key::Right) || down(Key::D) || padDown(Pad::Right)) { x += 1.0f; }
		if (down(Key::Up)    || down(Key::W) || padDown(Pad::Up))    { y -= 1.0f; }
		if (down(Key::Down)  || down(Key::S) || padDown(Pad::Down))  { y += 1.0f; }
		x = (x < -1.0f) ? -1.0f : (x > 1.0f ? 1.0f : x);
		y = (y < -1.0f) ? -1.0f : (y > 1.0f ? 1.0f : y);
		return { x, y };
	}

	/// 決定論 seed (録画再生で bit-exact 再現するため、乱数は mitiru::Random rng(in.rngSeed()) で seed する)。
	std::uint64_t rngSeed() const noexcept { return s_->rngSeed; }

	/// layer 別の実効 dt (v30、§1-2)。`in.dt(Layer::Ui)` で HUD 用の dt を読む。
	/// 範囲外 (負値・8 以上) は layer 0 (effectiveDt と同値) にフォールバックする。
	float dt(Layer layer) const noexcept { return dt(static_cast<int>(layer)); }
	/// 同上、添字直書き版 (2..7 の予約 layer 用)。
	float dt(int layer) const noexcept
	{
		const int cap = static_cast<int>(sizeof(s_->dtByLayer) / sizeof(s_->dtByLayer[0]));
		return (layer >= 0 && layer < cap) ? s_->dtByLayer[layer] : s_->effectiveDt;
	}

	/// 音声クロック (秒、ABI v13)。host の audio backend の再生サンプル位置。
	/// **契約**: 0 = 未準備/非対応 (起動直後の数フレームや Null/headless) → game は
	/// フレーム dt 積算へフォールバックすること。**非ゼロになった後は単調非減少を
	/// engine が保証する** (backend の供給が一時的に落ち込んでも巻き戻らない)。録画再生でも再現する。
	/// リズムゲームの同期は「audioTime()<=0 の間は dt クロック、以降は緩く lerp」が定石。
	double audioTime() const noexcept { return s_->audioTimeSec; }

	/// 音声出力レイテンシ (秒、ABI v19)。デバイスバッファに積んでから実際に耳へ届くまでの遅延。
	/// 0 = 不明 (Null/headless 等)。判定窓の耳基準補正に使う。
	double audioLatency() const noexcept { return s_->audioLatencySec; }

	/// 実際に耳へ届いている音声クロック位置 (秒、ABI v19)。= audioTime() - audioLatency()。
	/// **リズムゲームの判定はこの earTime() を基準にすれば、出力レイテンシ分のズレは仕組みのうえで生じない**
	/// (audioTime() はデバイスへ送った位置 = 耳より先行)。audioTime() が未準備 (<=0) の間は 0。
	double earTime() const noexcept
	{
		const double t = s_->audioTimeSec - s_->audioLatencySec;
		return (s_->audioTimeSec > 0.0 && t > 0.0) ? t : 0.0;
	}

	// ── セーブ/ロード結果・演出進行度 (ABI v33、D1/D2) ──────────────────────
	/// 直前の `hud.save()` が成功したか (D1)。結果は 1 フレーム遅れて分かる
	/// (intent → host 処理 → 次フレームの snapshot、非同期な処理系のため)。
	/// まだ何もセーブしていない場合も false を返す (raw()->lastSaveResult で 0/1/2 を区別できる)。
	bool saveSucceeded() const noexcept { return s_->lastSaveResult == 1; }
	/// 直前の `hud.load()` が成功したか (D1)。意味論は saveSucceeded() と同じ。
	bool loadSucceeded() const noexcept { return s_->lastLoadResult == 1; }
	/// fadeOut/fadeIn の覆い alpha (0=覆い無し / 1=完全に覆う、D2)。fadeOut の完了は
	/// 1.0 到達、fadeIn の完了は 0.0 到達で判定する (シーン切り替えのタイミング合わせに使う)。
	float fadeProgress01() const noexcept { return s_->fadeProgress01; }

	/// このフレームに確定した UTF-8 テキスト入力 (ABI v34、J5)。IME 確定文字を含む。プレイヤー名入力の
	/// ような、ゲーム内の簡易テキスト入力用 (32B 上限、収まらない分は切り捨て)。日本語を打たせたい間は
	/// `hud.wantTextInput(入力欄)` を毎フレーム呼ぶ (呼ばないフレームは IME が切れている)。
	/// 現状 Win32 のみ供給、他 platform は常に空。
	std::string_view textInput() const noexcept
	{
		return std::string_view(s_->textInput, s_->textInputLen);
	}
	/// IME で変換中の、まだ確定していない文字列 (UTF-8、ABI v45)。入力欄に下線付きで出したいときに読む。
	/// 確定すると消えて textInput() に来る。録画に乗るので、再生でも同じ表示になる。
	std::string_view imeComposition() const noexcept
	{
		return std::string_view(s_->imeComposition, s_->imeCompositionLen);
	}
	/// imeComposition() の中のキャレット位置 (先頭からの byte 数)。
	int imeCursor() const noexcept { return s_->imeCursor; }

	/// 前フレームに `hud.raycast()` / `hud.overlapSphere()` で頼んだ物理問い合わせの結果 (v37)。
	/// `tag` で照合する。無ければ nullptr。hit == kPhysicsHitUnsupported は「host に物理 world が無い」。
	const module::PhysicsResult* physicsResult(std::uint32_t tag) const noexcept
	{
		const int n = s_->physicsResultCount < 64 ? s_->physicsResultCount : 64;
		for (int i = 0; i < n; ++i) { if (s_->physicsResults[i].tag == tag) { return &s_->physicsResults[i]; } }
		return nullptr;
	}
	int physicsResultCount() const noexcept { return s_->physicsResultCount; }

	// ── v48: 入力のアクション (MITIRU_ACTIONS で export した表の i 番目 = 番号 i)。利用者のキー割り当て後 ──
	bool actionDown(int index)     const noexcept { return actionBit(s_->actionsDown, index); }
	bool actionPressed(int index)  const noexcept { return actionBit(s_->actionsPressed, index); }
	bool actionReleased(int index) const noexcept { return actionBit(s_->actionsReleased, index); }
	/// 表の並びと同じ順の enum で読む。`enum class Act { Jump, Attack };` なら `in.actionPressed(Act::Jump)`
	template <typename E> requires std::is_enum_v<E>
	bool actionDown(E a) const noexcept { return actionDown(static_cast<int>(a)); }
	template <typename E> requires std::is_enum_v<E>
	bool actionPressed(E a) const noexcept { return actionPressed(static_cast<int>(a)); }
	template <typename E> requires std::is_enum_v<E>
	bool actionReleased(E a) const noexcept { return actionReleased(static_cast<int>(a)); }

	// ── v48: セーブスロットの一覧 (`hud.listSlots()` の答え。次のフレームに届き、次に頼むまで残る) ──
	int slotCount() const noexcept { return s_->slotCount; }
	/// i 番目 (新しい順) のスロット。範囲外は nullptr
	const module::SlotSummary* slot(int i) const noexcept
	{
		return (i >= 0 && i < s_->slotCount && i < module::kMaxSlotSummaries) ? &s_->slots[i] : nullptr;
	}
	/// 一覧に答えるたびに 1 増える番号。前に見た値と比べれば、新しい答えが届いたフレームが分かる
	std::uint32_t slotListSerial() const noexcept { return s_->slotListSerial; }
	/// 直前の `hud.deleteSlot()` が成功したか (1 フレーム遅れて分かる)
	bool deleteSucceeded() const noexcept { return s_->lastDeleteResult == 1; }

	// ── v48: 利用者の設定と言語 ──
	/// 設定が変わったフレームだけ立つ bit (settings::kChange* と同じ番号。言語は 1 << 6)
	std::uint32_t settingsChanged() const noexcept { return s_->settingsChangedMask; }
	/// 表示言語 (例 "ja")。設定画面で変わると settingsChanged() の言語の bit も立つ
	std::string_view language() const noexcept
	{
		std::size_t n = 0;
		while (n < sizeof(s_->language) && s_->language[n] != '\0') { ++n; }
		return std::string_view(s_->language, n);
	}

	/// v48: 曲の拍 (music.json の区間を鳴らしている間。耳に届いている位置)。拍に合わせた演出に使う
	const module::MusicClock& music() const noexcept { return s_->music; }

	// ── v50: 先読みの進み・オンラインの人数と人ごとの操作・今の機器 ──
	/// `hud.preload` / `hud.stage` で頼んだ資産のうち、まだ描けない数。0 になったらロード画面を外す
	std::uint32_t preloadPending() const noexcept { return s_->preloadPending; }
	/// 今のステージで読めなかった資産の数
	std::uint32_t preloadFailed() const noexcept { return s_->preloadFailed; }
	/// オンラインの人数 2..4。オフラインは 0。全員の PC で同じ値なので update で読んでよい
	int netPlayers() const noexcept { return s_->netPlayerCount; }
	/// player 番目の人の操作 (表の i 番目)。オンラインは席の番号、オフラインは 0 が手元の人
	bool actionDown(int player, int index) const noexcept { return actionBit(playerMask(s_->actionsDownByPlayer, player), index); }
	bool actionPressed(int player, int index) const noexcept { return actionBit(playerMask(s_->actionsPressedByPlayer, player), index); }
	bool actionReleased(int player, int index) const noexcept { return actionBit(playerMask(s_->actionsReleasedByPlayer, player), index); }
	template <typename E> requires std::is_enum_v<E>
	bool actionDown(int player, E a) const noexcept { return actionDown(player, static_cast<int>(a)); }
	template <typename E> requires std::is_enum_v<E>
	bool actionPressed(int player, E a) const noexcept { return actionPressed(player, static_cast<int>(a)); }
	template <typename E> requires std::is_enum_v<E>
	bool actionReleased(int player, E a) const noexcept { return actionReleased(player, static_cast<int>(a)); }
	/// 最後に触った機器とパッドの書き方。`input::glyphFor` に渡すと、自分で描く HUD のボタンの絵柄の名前が引ける。
	/// オンラインでは PC ごとに違う値なので 0 (キーボード、Xbox) が届く
	input::InputDevice inputDevice() const noexcept { return static_cast<input::InputDevice>(s_->inputDevice); }
	input::PadFamily padFamily() const noexcept { return static_cast<input::PadFamily>(s_->padFamily); }

	/// 生の InputSnapshot へのアクセス (全 256 キー走査など、ラッパで足りない高度用途の escape hatch)。
	const module::InputSnapshot* raw() const noexcept { return s_; }

private:
	static bool actionBit(std::uint64_t mask, int index) noexcept
	{
		return index >= 0 && index < module::kMaxModuleActions && ((mask >> index) & 1u) != 0;
	}
	static std::uint64_t playerMask(const std::uint64_t (&masks)[module::kMaxNetPlayers], int player) noexcept
	{
		return (player >= 0 && player < module::kMaxNetPlayers) ? masks[player] : 0u;
	}
	// 'a'..'z' (0x61..0x7A) は弾かない。VK ではテンキーと Key::F1..F11 の値で、小文字の誤用と区別できない
	static bool held(int vk, const std::uint8_t* table) noexcept
	{
		return vk >= 0 && vk < 256 && table[vk] != 0;
	}
	/// X1 / X2 は別の配列にある (InputSnapshot は末尾にしか足せないため)。
	static bool mouseFlag(int button, const std::uint8_t (&main)[3], const std::uint8_t (&ext)[2]) noexcept
	{
		if (button >= 0 && button < 3) { return main[button] != 0; }
		if (button >= 3 && button < 5) { return ext[button - 3] != 0; }
		return false;
	}
	/// エスケープされていない次の '"' を探す (`\"` を文字列終端と誤認しない)。
	/// 値の中に `\"key\":` のような文字列が入っていると、素の strchr は
	/// エスケープされた `"` を本物の区切りと取り違え、他フィールドの値の中身を
	/// key の値として誤って拾ってしまうため findJsonValue から独立させてある。
	static const char* nextJsonQuote(const char* p) noexcept
	{
		for (; *p != '\0'; ++p)
		{
			if (*p == '\\' && p[1] != '\0') { ++p; continue; }
			if (*p == '"') { return p; }
		}
		return nullptr;
	}
	/// payload JSON から "key": の直後 (値の先頭) を指すポインタを返す (無ければ nullptr)。
	/// フラットな 1 段 JSON のみ対応 (D5)。
	static const char* findJsonValue(const char* json, const char* key) noexcept
	{
		if (json == nullptr || key == nullptr) { return nullptr; }
		const std::size_t keyLen = std::strlen(key);
		for (const char* p = json; (p = nextJsonQuote(p)) != nullptr; )
		{
			const char* start = p + 1;
			if (std::strncmp(start, key, keyLen) == 0 && start[keyLen] == '"')
			{
				const char* after = start + keyLen + 1;
				while (*after == ' ' || *after == '\t') { ++after; }
				if (*after == ':')
				{
					++after;
					while (*after == ' ' || *after == '\t') { ++after; }
					return after;
				}
			}
			p = start;
		}
		return nullptr;
	}
	/// Binding 表の線形走査 (N は十数行が普通なので十分速い)。同一 act の複数行は OR 合成。
	template <typename Act>
	static bool boundAny(const Binding<Act>* map, std::size_t n, Act act,
	                     const std::uint8_t* keyTable, std::uint32_t padMask) noexcept
	{
		for (std::size_t i = 0; i < n; ++i)
		{
			if (map[i].act != act) { continue; }
			for (const Key k : map[i].keys)
			{
				if ((int)k != 0 && held((int)k, keyTable)) { return true; }
			}
			for (const Pad p : map[i].pads)
			{
				if ((padMask & static_cast<std::uint32_t>(p)) != 0) { return true; }
			}
		}
		return false;
	}
	const module::InputSnapshot* s_;
};

/// 音のバス。`hud.busVolume(SoundBus::Sfx, 0.5f)` でまとめて音量を変える (Master は全部に掛かる)。
/// 鳴らす音には `.bus(SoundBus::Ui)` で付ける。付けなければ効果音は Sfx、BGM は Music、ボイスは Voice。
enum class SoundBus : std::uint8_t { Master = 0, Music = 1, Sfx = 2, Voice = 3, Ui = 4, Ambient = 5 };

/// `hud.play` / `hud.playLoop` が返す、いま積んだ 1 本の音への設定口。続けて書く。
/// `hud.play("hit").at(enemy.pos)`、`hud.playLoop("engine").handle(car.id).at(car.pos)`。
/// 1 フレームに積める音 (8 本) を超えて積めなかった時は、何を呼んでも何もしない。
class SoundCall
{
public:
	explicit SoundCall(module::SoundIntent* s) noexcept : s_(s) {}

	/// この位置で鳴らす (world 座標、1 = 1m)。`hud.listener()` から見て遠いほど小さく、左右に振れる。
	SoundCall at(Vec3 pos) noexcept
	{
		if (s_ != nullptr) { s_->spatial = 1; s_->position[0] = pos.x; s_->position[1] = pos.y; s_->position[2] = pos.z; }
		return *this;
	}
	/// 左右の振り (-1 = 左、0 = 中央、1 = 右)。at() と一緒に使うと at() が優先される。
	SoundCall pan(float p) noexcept
	{
		if (s_ != nullptr) { s_->pan = p; }
		return *this;
	}
	/// バス。`hud.busVolume()` でバスごとに音量を変えられる。
	SoundCall bus(SoundBus b) noexcept
	{
		if (s_ != nullptr) { s_->bus = static_cast<std::uint8_t>(b); }
		return *this;
	}
	/// 再生の番号 (ゲームが決める 1 以上の数。敵の番号など)。同じ音を何本も鳴らして 1 本ずつ止めたい時に付け、
	/// `hud.stopSound(番号)` で止める。同じ番号の音が鳴っている間にもう一度鳴らすと、頭から鳴らし直さず
	/// 音量・ピッチ・位置だけが変わる (動く物のループ音は毎フレーム同じ番号で鳴らせばよい)。
	SoundCall handle(std::uint32_t h) noexcept
	{
		if (s_ != nullptr) { s_->handle = h; }
		return *this;
	}
	/// 壁に遮られている量 0..1 (1 で sounds.json の occlusionDb まで下がり、高い音が削れる)。当たり判定のレイの結果を
	/// 毎フレーム渡す。番号 (`.handle`) を付けて鳴らし直すたびに置き換わる
	SoundCall occlusion(float amount01) noexcept
	{
		const float a = amount01 < 0.0f ? 0.0f : (amount01 > 1.0f ? 1.0f : amount01);
		if (s_ != nullptr) { s_->occlusion = static_cast<std::uint8_t>(a * 255.0f + 0.5f); }
		return *this;
	}
	/// 声の優先度 1..255 (大きいほど残る。既定は sounds.json の priority、無ければ 128)。声が足りない時に低い方から消える
	SoundCall priority(int p) noexcept
	{
		if (s_ != nullptr) { s_->priority = static_cast<std::uint8_t>(p < 1 ? 1 : (p > 255 ? 255 : p)); }
		return *this;
	}

private:
	module::SoundIntent* s_;
};

// `Tool` enum + 開ける窓の registry (kToolTable) は <mitiru/debug/ToolRegistry.hpp>
// に置き、host 側 (openTool) と共有している。

/// 画面 (HUD) へ値を送る + 音を鳴らす + ツール窓を開く (`FrameIntents` の薄いビュー)。
class Hud
{
public:
	explicit Hud(module::FrameIntents* s) noexcept : s_(s) {}

	void set(const char* key, int v)         noexcept { s_->pushInt(key, v); }      ///< RML の {{ }} へ (view. より後ろの名前で読む)
	void set(const char* key, float v)       noexcept { s_->pushFloat(key, v); }
	void set(const char* key, bool v)        noexcept { s_->pushBool(key, v); }
	void set(const char* key, const char* v) noexcept { s_->pushString(key, v); }

	/// 数値配列を 1 件の statePush で送る (D4)。`set()` は 1 フレーム 64 件の statePush
	/// 上限があり、敵・弾多数の座標を毎フレーム 1 体 1 件で送る shooter 系がすぐ当たる。
	/// 代わりに JSON 配列文字列 1 本 (`[1,2,3]`) にまとめ、statePush の消費を 1 件にする。
	/// RML 側は JSON の配列として読むので `data-for="v : key"` で並べられる。
	/// count が strVal (3968B) に収まらない場合は収まる分だけで配列を
	/// 閉じ、初回のみ warnOnce する。対処法: 配列を分割 key にするか送る件数を間引く。
	void setArray(const char* key, const float* values, std::size_t count) noexcept
	{
		char buf[3968];
		std::size_t pos = 0;
		buf[pos++] = '[';
		bool truncated = false;
		for (std::size_t i = 0; i < count; ++i)
		{
			char num[32];
			const int n = std::snprintf(num, sizeof(num), "%s%.6g",
				(i > 0) ? "," : "", static_cast<double>(values[i]));
			if (n < 0 || pos + static_cast<std::size_t>(n) + 2 >= sizeof(buf))
			{
				truncated = true;
				break;
			}
			std::memcpy(buf + pos, num, static_cast<std::size_t>(n));
			pos += static_cast<std::size_t>(n);
		}
		buf[pos++] = ']';
		buf[pos] = '\0';
		if (truncated)
		{
			mitiru::debug::warnOnce(std::string("hud.setArray.trunc.") + key,
				std::string("hud.setArray: \"") + key + "\" の配列が statePush 1 件 (約 3968B) "
				"に収まらず途中で切りました。配列を分割するか送る件数を間引いてください");
		}
		s_->pushString(key, buf);
	}

	/// 効果音を鳴らす。volume は 0..1 (1=原音量)。**volume 0 = 無音** (鳴らしたくない時は
	/// 呼ばないのが普通だが、変数で 0 が来ても最大音量にはならない)。
	SoundCall play(const char* soundId, float volume = 1.0f) noexcept
	{
		return SoundCall{s_->playSound(soundId, clampVolume(volume))};
	}
	/// 音をピッチ付きで鳴らす (pitch 0.5..2.0、1.0=原音)。1 つの SE を音階で鳴らすリズムゲーム等。
	/// **volume 0 = 無音**。pitch 0 は無意味なので、明示した pitch <= 0 は 1.0 (原音) に丸められる。
	SoundCall play(const char* soundId, float volume, float pitch) noexcept
	{
		return SoundCall{s_->playSound(soundId, clampVolume(volume), pitch)};
	}
	/// 効果音をループ再生する。stopLoop で止めるまで鳴り続ける。
	/// 長押しのように「押している間ずっと」鳴らしたい音に使う。短い音を継ぎ足して
	/// 伸ばすと継ぎ目が聴こえ、離した瞬間に切れる。同じ id が鳴っている間の再呼び出しは
	/// 鳴らし直さず音量とピッチだけを寄せる (音量スライダーの試聴のように、鳴らしたまま
	/// 音量を動かせる)。
	SoundCall playLoop(const char* soundId, float volume = 1.0f, float pitch = 1.0f,
	                   float fadeInSec = 0.0f) noexcept
	{
		return SoundCall{s_->loopSound(soundId, clampVolume(volume), pitch, fadeInSec)};
	}
	/// playLoop で鳴らしている音を止める。releaseSec > 0 で減衰させてから止める。
	/// `.handle(n)` を付けて鳴らした音は stopSound(n) で止める。
	void stopLoop(const char* soundId, float releaseSec = 0.0f) noexcept
	{
		s_->stopSoundId(soundId, releaseSec);
	}
	/// `.handle(n)` を付けて鳴らした音を 1 本だけ止める。releaseSec > 0 で減衰させてから止める。
	void stopSound(std::uint32_t handle, float releaseSec = 0.0f) noexcept
	{
		s_->stopSoundHandle(handle, releaseSec);
	}
	/// 3D の音を聞く位置と向き (ふつうはカメラ)。`.at(pos)` で鳴らした音はここから見て遠いほど小さく、
	/// 左右に振れる。host が覚えているので、動いたフレームだけ呼べばよい。
	void listener(Vec3 position, Vec3 forward, Vec3 up = Vec3{0.0f, 1.0f, 0.0f}) noexcept
	{
		const float p[3]{position.x, position.y, position.z};
		const float f[3]{forward.x, forward.y, forward.z};
		const float u[3]{up.x, up.y, up.z};
		s_->setListener(p, f, u);
	}
	/// バスの音量 (0..1)。オプション画面の「効果音」「BGM」の音量に使う。host が覚えていて、
	/// 鳴っているループ音と BGM にもすぐ反映される。
	void busVolume(SoundBus bus, float volume) noexcept
	{
		s_->setBusVolume(static_cast<std::uint8_t>(bus), volume);
	}
	/// BGM を再生する (連続トラック、既定ループ)。同じ id なら毎フレーム呼んでも安全。
	/// host が直前と同じ id / loop / volume の BGM を重複再生しない (冪等)。**volume 0 = 無音**。
	/// crossfadeSec > 0 なら、別の BGM が再生中のとき旧曲をフェードアウトしつつ新曲を
	/// フェードインする (場面転換の定番が 1 行になる)。
	void music(const char* id, bool loop = true, float volume = 1.0f,
	           float crossfadeSec = 0.0f) noexcept
	{
		s_->playMusic(id, clampVolume(volume), loop, crossfadeSec);
	}
	/// 再生中の BGM を停止する (fadeOutSec > 0 でフェードアウト)。
	void stopMusic(float fadeOutSec = 0.0f) noexcept { s_->stopMusic(fadeOutSec); }
	/// ボイス (台詞) を鳴らす。BGM / SE とは別の 1 本のスロットで鳴り、前の台詞が
	/// まだ鳴っていれば重ねず差し替える。**volume 0 = 無音** (play() と同じ)。mixer 窓の「voice 一覧」に
	/// id / 残り秒が出るのはこの経路で鳴らした音だけ (play() は SE 扱い)。
	void voice(const char* soundId, float volume = 1.0f, float fadeInSec = 0.0f) noexcept
	{
		s_->playVoice(soundId, clampVolume(volume), fadeInSec);
	}
	/// 鳴っているボイスを止める (fadeOutSec > 0 でフェードアウト)。
	void stopVoice(float fadeOutSec = 0.0f) noexcept { s_->stopVoice(fadeOutSec); }
	/// 再生中の BGM を一時停止する (再生位置を保持。resumeMusic で続きから。会話チュートリアルで
	/// BGM を止めて間を取る等。stopMusic と違い曲は破棄されない)。
	void pauseMusic() noexcept { s_->pauseMusic(); }
	/// pauseMusic で止めた BGM を続きから再開する。
	void resumeMusic() noexcept { s_->resumeMusic(); }
	/// 再生中の BGM を指定位置 (秒) へシークする。
	void seekMusic(float positionSec) noexcept { s_->seekMusic(positionSec); }
	/// 効果音を「音声クロック上の時刻 atSec」にサンプル精度で予約再生する (リズムゲームの
	/// 「次の拍でこの音」)。atSec は in.audioTime() と同じ基準の絶対時刻。毎フレーム判定で鳴らすと
	/// フレーム量子化 (~16ms) のジッタが乗るが、これは host が音声サンプル単位で発火させる。
	/// **volume 0 = 無音**。pitch <= 0 は 1.0 (原音) に丸める。
	void playAt(const char* soundId, double atSec, float volume = 1.0f, float pitch = 1.0f) noexcept
	{
		s_->scheduleSound(soundId, atSec, clampVolume(volume), pitch);
	}
	void quit() noexcept { s_->requestStop = 1; }   ///< ゲームを終了する

	// ── 演出 / デバッグ (必要なときだけ呼ぶ — pulled UI、ゲーム窓は汚さない) ──
	/// 画面を一瞬 c 色にフラッシュさせる (被弾演出など)。
	void flash(Color c, float seconds = 0.18f) noexcept { s_->pushTint(c.r, c.g, c.b, c.a, seconds); }
	/// 画面を黒 (または c 色) で覆っていく。シーン転換の出口。
	void fadeOut(float seconds = 0.4f, Color c = {0, 0, 0, 1}) noexcept
	{
		s_->pushVisual(module::kVisualIntentFadeOut, c.r, c.g, c.b, 1.0f, seconds);
	}
	/// 覆いを晴らしていく。シーン転換の入口 (fadeOut と対で使う)。
	void fadeIn(float seconds = 0.4f, Color c = {0, 0, 0, 1}) noexcept
	{
		s_->pushVisual(module::kVisualIntentFadeIn, c.r, c.g, c.b, 1.0f, seconds);
	}
	/// 画面を揺らす (被弾・着地・爆発)。magnitude は振幅 px。決定論は host が保証する
	/// (ゲーム側で乱数を引く必要なし = リプレイも bit-exact)。
	void shake(float seconds = 0.3f, float magnitude = 8.0f) noexcept
	{
		s_->pushVisual(module::kVisualIntentShake, 0, 0, 0, magnitude, seconds);
	}
	/// パッドを振動させる (被弾・着地)。low/high は左右モータ 0..1、seconds で線形に弱まる。
	/// 出力だけの演出なので録画には乗らず、リプレイの一致にも影響しない。
	void rumble(float low, float high, float seconds = 0.2f) noexcept
	{
		s_->pushVisual(module::kVisualIntentRumble, low, high, 0, 0, seconds);
	}
	/// ヒットストップ (seconds の間 dt=0 で時が止まる。update は呼ばれ続ける)。
	/// 撃破・パリィの手応えが 1 行になる。
	void hitStop(float seconds = 0.08f) noexcept
	{
		s_->pushVisual(module::kVisualIntentHitStop, 0, 0, 0, 0, seconds);
	}
	/// レターボックス (上下の黒帯)。イベント・カットシーンの定番。amount は帯の量 (0..1)、
	/// seconds かけて遷移する。戻すときは `hud.letterbox(0.0f)`。
	void letterbox(float amount01, float seconds = 0.4f) noexcept
	{
		s_->pushVisual(module::kVisualIntentLetterbox, 0, 0, 0, amount01, seconds);
	}

	// ── セーブ/ロード (セーブ = GameMemory の memcpy) ─────────────────────
	/// セーブに章の名前を付ける (v48)。スロットの一覧 (`in.slot(i)->chapter`) に出る。
	void saveSlot(const char* slot, const char* chapter) noexcept
	{
		s_->requestSave(slot);
		s_->setSaveChapter(chapter);
	}
	/// スロットを消す (v48)。結果は次のフレームの `in.deleteSucceeded()`。
	void deleteSlot(const char* slot) noexcept { s_->requestDeleteSlot(slot); }
	/// スロットの一覧を頼む (v48)。次のフレームの `in.slot(i)` に新しい順で最大 8 個届く。
	void listSlots() noexcept { s_->slotListRequest = 1; }
	/// GameMemory をまるごとスロットへセーブする (既定は `save/<slot>.mslot`、置き場は host が決める)。
	/// flat POD だからセーブ = スナップショット。巻き戻し・リプレイと同一機構。
	/// slot に "auto" を渡すと自動セーブの輪番になる (load の "auto" は一番新しい自動セーブ)。
	void save(const char* slot = "slot0") noexcept { s_->requestSave(slot); }
	/// スロットから GameMemory を復元する。GameMemory の struct を変更した後の
	/// 旧セーブは安全のため拒否される (初回 1 回警告)。リプレイ中は記録済み state で
	/// 代用されるため、セーブファイルが変わっていても再現はずれない。
	void load(const char* slot = "slot0") noexcept { s_->requestLoad(slot); }
	/// ゲームを最初からやり直す (GameMemory を unload なしで fresh 再構築、§8-4)。
	/// update 内の `*this = MyGame{}` 手運びの代わり。host が memset 0 → init() を適用する。
	/// intent なので replay / resim では update が同フレームで再発行し bit-exact に再現される。
	void requestRestart() noexcept { s_->requestRestart(); }
	/// カーソルロックの状態を毎フレーム宣言する (D3)。true でロック要求、false は何もしない
	/// (= このフレームは要求を出さない)。wantMouseLock は「呼ばれたら立つ」意思表示なので、
	/// 明示 unlock intent は無い。次フレームで呼ばなければ host が自然に解除する。
	void setMouseLock(bool locked) noexcept { if (locked) { s_->requestMouseLock(); } }
	/// カーソルをロックする (FPS 視線)。毎フレーム呼ぶ。呼ばないフレームで解除される
	/// (`setMouseLock(true)` の別名。unlock したいときは単に呼ぶのをやめる)。
	void lockMouse() noexcept { setMouseLock(true); }
	/// このフレームはテキストを受けたい、入力欄は field (画面座標) だと伝える。毎フレーム呼ぶ。
	/// 呼んでいる間だけ IME が働き、変換窓と候補窓が入力欄に出る。呼ばないフレームは IME が切れていて、
	/// 遊んでいる最中に日本語入力の窓が出ない (lockMouse と同じ「毎フレーム宣言」)。
	void wantTextInput(Rect field) noexcept
	{
		s_->requestTextInput(field.position.x, field.position.y, field.size.x, field.size.y);
	}
	/// このフレームのスクリーンショットを保存する。
	void screenshot() noexcept { s_->requestScreenshotNow(); }

	// ── v48: カメラの切り替え・曲の強さ・残響の場所・検証の印 ──
	/// カメラが別の場所へ飛んだフレームに呼ぶ。TAA と動きのぼけが前の絵を引きずらない
	void cameraCut() noexcept { s_->cameraCut = 1; }
	/// 曲の強さ 0..1 (music.json の層の音量の曲線を動かす)。host が覚えているので変わった時だけ呼べばよい
	void musicIntensity(float intensity01) noexcept { s_->musicIntensitySet = 1; s_->musicIntensity = intensity01; }
	/// 残響の場所を mix.json の id で決める。空文字で聞き手の位置から選ぶ既定に戻す
	void reverbZone(const char* id) noexcept { s_->setReverbZone(id); }
	/// 検証用の印 (例 "hit")。--state-trace の行と音の記録 (events.jsonl) に同じフレームで残る。1 フレーム 8 個まで
	void mark(const char* name) noexcept { s_->pushMark(name); }

	// ── v50: 資産の先読み (ADR 0064)。読み終わりは次のフレームからの in.preloadPending() が 0 になるのを見る ──
	/// 資産を描く前に読み始める (`model:` `clod:` `sound:` の印を付けられる)。1 フレーム 16 件まで。満杯なら false
	bool preload(const char* path) noexcept { return s_->pushPreload(module::kPreloadLoad, path); }
	/// 資産を手放す。次に描くと読み直す
	bool release(const char* path) noexcept { return s_->pushPreload(module::kPreloadRelease, path); }
	/// 今のステージの資産を paths に切り替える。前のステージにだけある資産を手放し、新しい資産を読み始める。
	/// count が 0 なら全部を手放す。preload と合わせて 1 フレーム 16 件まで。入りきらなければ 1 件も積まずに false
	/// (一部だけ積むと、host は切り詰めた一覧をステージにして残りの資産を手放す)
	bool stage(const char* const* paths, int count) noexcept
	{
		const int room = module::kMaxPreloadIntents - static_cast<int>(s_->preloadCount);
		if (std::max(count, 1) > room) { return false; }
		if (count <= 0) { return s_->pushPreload(module::kPreloadStage, ""); }
		for (int i = 0; i < count; ++i)
		{
			if (!s_->pushPreload(module::kPreloadStage, paths[i])) { return false; }
		}
		return true;
	}

	// ── v50: オンライン協力プレイ (ADR 0066)。player は 0 = この PC、1..4 = その席の PC だけが受ける ──
	// オンライン中のシミュレーションは全員の PC で同じに進むので、抜ける・準備は player (席 + 1) を付けて頼む。
	/// 待合室の画面を出す
	void netOpen() noexcept { s_->setNetRequest(module::kNetRequestOpen, 0); }
	/// 部屋を作る (2..4 人)。GameMemory は on_init の直後に戻り、全員が揃うと in.netPlayers() が人数になる
	void netHost(int players = 2) noexcept
	{
		s_->setNetRequest(module::kNetRequestHost, 0, static_cast<std::uint8_t>(players < 2 ? 2 : (players > 4 ? 4 : players)));
	}
	/// 部屋を作る時に方式も選ぶ (v51、mode は module::kNetMode*)。snapshotHz は host 権威で状態を配る回数 (60 の約数へ丸める。
	/// 0 は起動の引数のまま)。方式は部屋ごとに host が決め、参加者は従う
	void netHost(int players, std::uint8_t mode, int snapshotHz = 0) noexcept
	{
		netHost(players);
		s_->setNetModeRequest(mode, static_cast<std::uint8_t>(snapshotHz < 0 ? 0 : (snapshotHz > 60 ? 60 : snapshotHz)));
	}
	/// 参加コード (か ip:port) の部屋に入る。GameMemory は on_init の直後に戻る
	void netJoin(const char* code) noexcept { s_->setNetRequest(module::kNetRequestJoin, 0, 0, 0, code); }
	void netLeave(int player = 0) noexcept { s_->setNetRequest(module::kNetRequestLeave, netPlayerByte(player)); }
	void netReady(bool ready, int player = 0) noexcept
	{
		s_->setNetRequest(module::kNetRequestReady, netPlayerByte(player), 0, ready ? 1 : 0);
	}

	// ── v50: カットシーン (ADR 0063)。narrative::pushSequence が再生中に両方を呼ぶ ──
	/// カットシーン中だと伝える (毎フレーム呼ぶ)。host は F7〜F9 の止める・コマ送り・速さを効かなくし、UI へ view.cinematic を送る
	void cinematic() noexcept { s_->cinematicActive = 1; }
	/// Rewind 窓のバーにカットシーンの区間を出す。再生中は毎フレーム、今の時刻と長さで呼ぶ。1 フレーム 4 件まで
	void timelineMarker(const char* name, float timeSec, float durationSec) noexcept
	{
		s_->pushTimelineMarker(name, timeSec, durationSec);
	}

	// ── v48: 実績・統計・Rich Presence (Steam が無い host では何もしない。進行は変わらない) ──
	void achievement(const char* id) noexcept { (void)s_->pushAchievement(module::kAchievementUnlock, id); }
	void clearAchievement(const char* id) noexcept { (void)s_->pushAchievement(module::kAchievementClear, id); }
	void stat(const char* id, int value) noexcept
	{
		if (auto* a = s_->pushAchievement(module::kAchievementStatInt, id)) { a->intValue = value; }
	}
	void stat(const char* id, float value) noexcept
	{
		if (auto* a = s_->pushAchievement(module::kAchievementStatFloat, id)) { a->floatValue = value; }
	}
	/// 統計と実績をサーバーへ送る (stat を変えた後、区切りのよい所で 1 回)
	void storeStats() noexcept { (void)s_->pushAchievement(module::kAchievementStore, ""); }
	void presence(const char* key, const char* value) noexcept { (void)s_->pushAchievement(module::kAchievementPresence, key, value); }
	void clearPresence() noexcept { (void)s_->pushAchievement(module::kAchievementClearPresence, ""); }

	// ── v48: パッド 1 台への出力 (slot は in.pad(n) と同じ番号)。出力だけの演出なので録画には乗らない ──
	void padLight(int slot, Color c) noexcept
	{
		if (auto* p = padOut(slot))
		{
			p->set |= module::kPadOutLight;
			p->light[0] = colorByte(c.r); p->light[1] = colorByte(c.g); p->light[2] = colorByte(c.b);
		}
	}
	/// アダプティブトリガー (DualSense だけ)。side は 0 = 左、1 = 右。padTriggerResistance などで作る
	void padTrigger(int slot, int side, module::PadTriggerOut effect) noexcept
	{
		auto* p = padOut(slot);
		if (p == nullptr || side < 0 || side > 1) { return; }
		p->set |= (side == 0) ? module::kPadOutTriggerLeft : module::kPadOutTriggerRight;
		p->trigger[side] = effect;
	}
	/// ジャイロと加速度を読むか (電池を使うので要る間だけ)。読めると in.pad(n).motionActive() が立つ
	void padMotion(int slot, bool enabled) noexcept
	{
		if (auto* p = padOut(slot)) { p->set |= module::kPadOutMotion; p->motionEnabled = enabled ? 1 : 0; }
	}
	/// 1 台だけを揺らす (rumble は全台)。low / high は左右のモーター 0..1
	void rumblePad(int slot, float low, float high, float seconds = 0.2f) noexcept
	{
		if (auto* p = padOut(slot))
		{
			p->set |= module::kPadOutRumble;
			p->rumbleLow = low; p->rumbleHigh = high; p->rumbleSec = seconds;
		}
	}
	/// トリガーの振動 (Xbox One 以降)。left / right は 0..1
	void rumbleTriggers(int slot, float left, float right, float seconds = 0.2f) noexcept
	{
		if (auto* p = padOut(slot))
		{
			p->set |= module::kPadOutTriggerRumble;
			p->triggerRumble[0] = left; p->triggerRumble[1] = right; p->rumbleSec = seconds;
		}
	}
	/// inspector (別窓のデバッグツール) に観察データ (JSON 文字列) を送る。
	/// 必要なときだけ呼べばよい。inspector が開いている時にだけ映る。
	/// **消し方**: 明示の unwatch intent は無い。呼ぶのをやめると host は直近の内容を
	/// 表示し続けるので、消したいなら「消えた」ことを表す json (空 object 等) を送る。
	void watch(const char* name, const char* title, const char* json) noexcept
	{
		s_->pushInspectable(name, title, json);
	}

	// ── 物理問い合わせ job (v37、HE2 の PhysicsQueryJob 相当) ───────────────────
	// 同期呼び出しではなく intent。結果は次フレームの `in.physicsResult(tag)` で読む (1 フレーム遅れ)。
	// 結果は InputSnapshot に乗るので録画され、リプレイでも同じ値が返る。
	/// レイキャストを頼む。maxDist <= 0 は無制限。満杯 (64 件) なら false。
	bool raycast(Vec3 origin, Vec3 dir, float maxDist, std::uint32_t tag, std::uint32_t mask = 0xFFFFFFFFu) noexcept
	{
		module::PhysicsQuery* q = s_->nextPhysicsQuery();
		if (q == nullptr) { return false; }
		q->kind = module::kPhysicsQueryRaycast;
		q->a[0] = origin.x; q->a[1] = origin.y; q->a[2] = origin.z;
		q->b[0] = dir.x;    q->b[1] = dir.y;    q->b[2] = dir.z;
		q->radius = maxDist; q->mask = mask; q->tag = tag;
		return true;
	}
	/// 球に重なる body があるかを頼む (結果の t が件数)。満杯なら false。
	bool overlapSphere(Vec3 center, float radius, std::uint32_t tag, std::uint32_t mask = 0xFFFFFFFFu) noexcept
	{
		module::PhysicsQuery* q = s_->nextPhysicsQuery();
		if (q == nullptr) { return false; }
		q->kind = module::kPhysicsQueryOverlapSphere;
		q->a[0] = center.x; q->a[1] = center.y; q->a[2] = center.z;
		q->radius = radius; q->mask = mask; q->tag = tag;
		return true;
	}

	// ── ゲーム内 3D デバッグ描画 (v30、§9-1。必要なときだけ呼ぶ) ────────────────
	// **録画に入るのは snapshot (入力) だけで、この intent 自体は録画されない。**
	// リプレイ再生でデバッグ線を再現したいときは、ゲーム側が毎フレーム出し直すこと
	// (durationSec を使っても、host が減衰させるのは live 実行中だけ)。
	/// 線分を描く (durationSec=0 はこのフレームのみ)。
	void debugLine(Vec3 a, Vec3 b, Color color, float durationSec = 0.0f) noexcept
	{
		const float pa[3]{a.x, a.y, a.z}, pb[3]{b.x, b.y, b.z}, c[4]{color.r, color.g, color.b, color.a};
		s_->pushDebugLine(pa, pb, c, durationSec);
	}
	/// 箱 (中心 + 半径ベクトル) を描く。
	void debugBox(Vec3 center, Vec3 halfExtents, Color color, float durationSec = 0.0f) noexcept
	{
		const float pc[3]{center.x, center.y, center.z};
		const float he[3]{halfExtents.x, halfExtents.y, halfExtents.z};
		const float c[4]{color.r, color.g, color.b, color.a};
		s_->pushDebugBox(pc, he, c, durationSec);
	}
	/// 球を描く。
	void debugSphere(Vec3 center, float radius, Color color, float durationSec = 0.0f) noexcept
	{
		const float pc[3]{center.x, center.y, center.z};
		const float c[4]{color.r, color.g, color.b, color.a};
		s_->pushDebugSphere(pc, radius, c, durationSec);
	}
	/// world 座標に文字を描く (画面へ投影した位置に出る)。
	void debugText(Vec3 pos, const char* text, Color color, float durationSec = 0.0f) noexcept
	{
		const float pp[3]{pos.x, pos.y, pos.z};
		const float c[4]{color.r, color.g, color.b, color.a};
		s_->pushDebugText(pp, text, c, durationSec);
	}

	/// 別窓のツールを開くよう host に頼む (必要なときだけ呼ぶ。既定では何も開かない)。
	/// **閉じ方**: game 側に閉じる intent は無い (別プロセスの独立窓なので)。ユーザーが
	/// その窓を × で閉じる。同じツールを再度 open しても新しい窓は増えない想定 (host 側の重複起動抑止)。
	void open(Tool t) noexcept
	{
		for (const auto& spec : detail::kToolTable)
		{
			if (spec.tool == t) { s_->requestToolWindow(spec.exe, spec.args); return; }
		}
	}
	/// 任意のツール窓を名前で開く (host が mitiru_<tool>.exe を探す)。上級者向け。
	void open(const char* tool, const char* args = "") noexcept { s_->requestToolWindow(tool, args); }

private:
	/// 明示 volume <= 0 を実質無音 (0.0001) に丸める。intent の wire 上では 0 が
	/// 「未指定 = 既定音量 1.0」に予約されているため (zero-init 互換、SoundIntentRouter)、
	/// 「無音」は 0 でなく可聴未満の微小値で表す。
	static constexpr float clampVolume(float v) noexcept { return v > 0.0f ? v : 0.0001f; }
	module::PadOutIntent* padOut(int slot) noexcept { return (slot >= 0 && slot < 4) ? &s_->padOut[slot] : nullptr; }
	static std::uint8_t netPlayerByte(int player) noexcept
	{
		return static_cast<std::uint8_t>(player < 0 ? 0 : (player > module::kMaxNetPlayers ? 0 : player));
	}
	static std::uint8_t colorByte(float v) noexcept
	{
		return static_cast<std::uint8_t>((v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) * 255.0f + 0.5f);
	}

	module::FrameIntents* s_;
};

/// hud.padTrigger に渡す効き (v48)。start / strength は 0..255 (押し込みの位置と強さ)
[[nodiscard]] constexpr module::PadTriggerOut padTriggerOff() noexcept { return {module::kPadTriggerOff, 0, 0, 0}; }
[[nodiscard]] constexpr module::PadTriggerOut padTriggerResistance(std::uint8_t start, std::uint8_t strength) noexcept
{
	return {module::kPadTriggerResistance, start, strength, 0};
}
[[nodiscard]] constexpr module::PadTriggerOut padTriggerVibration(std::uint8_t start, std::uint8_t amplitude,
                                                                  std::uint8_t frequencyHz) noexcept
{
	return {module::kPadTriggerVibration, start, amplitude, frequencyHz};
}

namespace module::detail
{

// ── C-ABI コールバックへの trampoline (void* を型に戻して typed メソッドを呼ぶ) ──
template<class T>
void gameInit(void* mem)
{
	if (mem == nullptr) { return; }
	// 既定値イメージを static に 1 部だけ持つ (static 初期化で padding まで 0 化済み)。
	// 初回 load でも restart intent (§8-4) でも、この copy で NSDMI 既定値へ確実に戻る。
	// padding byte も決定論になる (ring / replay の memcmp 対象)。
	static const T kFresh{};
	std::memcpy(mem, &kFresh, sizeof(T));
	T& g = *static_cast<T*>(mem);
	if constexpr (requires { g.init(); }) { g.init(); }
	else { (void)g; }
}

template<class T>
void gameUpdate(void* mem, float dt, const InputSnapshot* in, FrameIntents* out)
{
	if (mem == nullptr || in == nullptr || out == nullptr) { return; }
	T& g = *static_cast<T*>(mem);
	mitiru::Input input{in};
	mitiru::Hud   hud{out};
	// update は欲しい引数だけ受け取ればよい (使わないものは省略可)。初心者は
	// update(Input in, float dt) だけ書けば動く。Hud (UI / 音) は要るときだけ。
	if      constexpr (requires { g.update(input, hud, dt); }) { g.update(input, hud, dt); }
	else if constexpr (requires { g.update(input, dt); })      { g.update(input, dt); }
	else if constexpr (requires { g.update(hud, dt); })        { g.update(hud, dt); }
	else if constexpr (requires { g.update(dt); })             { g.update(dt); }
	else { (void)input; (void)hud; (void)dt; }
}

template<class T>
void gameDraw(void* mem, mitiru::Screen* screen)
{
	if (mem == nullptr || screen == nullptr) { return; }
	T& g = *static_cast<T*>(mem);
	if constexpr (requires { g.draw(*screen); }) { g.draw(*screen); }
	else { (void)g; }
}

/// @brief `on_draw_commands` の trampoline (ABI v31、ADR 0025)。game が `draw(Canvas&)` を
/// 持つ時だけ `registerGame` がこれを配線する (POD コマンドバッファ経路)。
template<class T>
void gameDrawCommands(void* mem, const DrawContext* ctx, DrawCommandBuffer* out)
{
	if (mem == nullptr || ctx == nullptr || out == nullptr) { return; }
	T& g = *static_cast<T*>(mem);
	if constexpr (requires { g.draw(std::declval<mitiru::Canvas&>()); })
	{
		mitiru::Canvas canvas{*out, *ctx};
		g.draw(canvas);
	}
	else { (void)g; }
}

template<class T>
void gameShutdown(void* mem)
{
	if (mem == nullptr) { return; }
	T& g = *static_cast<T*>(mem);
	if constexpr (requires { g.shutdown(); }) { g.shutdown(); }
	else { (void)g; }
}

/// `mitiru_module_net_predict` の中身 (v51)。drawMemory は host が作った描画用の写しで、T の predict が自分の分だけを進める
template<class T>
void gameNetPredict(void* drawMemory, const InputSnapshot* local, std::uint8_t player)
{
	if (drawMemory == nullptr || local == nullptr) { return; }
	static_assert(requires(T& g, mitiru::Input in, int p, float dt) { g.predict(in, p, dt); },
		"MITIRU_NET_PREDICT(T): T に predict(Input, int player, float dt) がありません");
	T& g = *static_cast<T*>(drawMemory);
	g.predict(mitiru::Input{local}, static_cast<int>(player), local->effectiveDt);
}

// T が update / draw のどれかを「正しい署名で」持っているかを判定する。
// これが false の時に MITIRU_GAME すると、署名ミス (引数型 / dt 落とし / 大文字小文字) で
// 気づかないうちに update が呼ばれない footgun になるため、compile error にして気付かせる。
template<class T>
inline constexpr bool kHasGameEntry =
	requires(T& g, mitiru::Input in, mitiru::Hud hud, float dt) { g.update(in, hud, dt); } ||
	requires(T& g, mitiru::Input in, float dt) { g.update(in, dt); } ||
	requires(T& g, mitiru::Hud hud, float dt) { g.update(hud, dt); } ||
	requires(T& g, float dt) { g.update(dt); } ||
	requires(T& g, mitiru::Screen& s) { g.draw(s); };

/// @brief GameMemory リフレクション記述子。`MITIRU_REFLECT` / `MITIRU_REFLECT_AUTO` が特殊化する。
///        既定は no-op (反射を宣言しない game は記述子 0 件)。
template<class T> struct ReflectionOf { static void fill() noexcept {} };

/// @brief 反射の全フィールド。`mitiru_module_reflect_fields` が host へ渡す。
inline std::vector<FieldDescriptor>& fullReflectFields()
{
	static std::vector<FieldDescriptor> fields;
	return fields;
}

/// @brief 全件を out へ最大 cap 個写し、全件数を返す (out == nullptr なら数えるだけ)。
template<class E>
std::int32_t copyAllOut(const std::vector<E>& all, E* out, std::int32_t cap) noexcept
{
	const std::int32_t total = static_cast<std::int32_t>(all.size());
	if (out == nullptr || cap <= 0) { return total; }
	const std::int32_t n = (total < cap) ? total : cap;
	for (std::int32_t i = 0; i < n; ++i) { out[i] = all[static_cast<std::size_t>(i)]; }
	return total;
}

inline std::int32_t reflectFieldsOut(FieldDescriptor* out, std::int32_t cap) noexcept
{
	return copyAllOut(fullReflectFields(), out, cap);
}

inline std::int32_t reflectSchemasOut(ReflectSchema* out, std::int32_t cap) noexcept
{
	return copyAllOut(::mitiru::module::reflectSchemaRegistry(), out, cap);
}

template<class T>
std::uint64_t layoutHashOf() noexcept { return layoutFingerprint<T>(); }

/// @brief MITIRU_REFLECT / MITIRU_REFLECT_AUTO の記述子を全件控える。
inline void registerReflection(const FieldDescriptor* fields, std::int32_t n)
{
	if (fields == nullptr) { return; }
	fullReflectFields().assign(fields, fields + (n > 0 ? n : 0));
}

/// @brief GameMemory 型 T の反射を控え、静的リンクの host にも同じ 3 関数を渡す
///        (静的リンクには GetProcAddress で引く export が無いため)。
template<class T>
void registerStateReflection()
{
	fullReflectFields().clear();
	ReflectionOf<T>::fill();
	linkedReflectionExports() = ReflectionExports{&layoutHashOf<T>, &reflectFieldsOut, &reflectSchemasOut};
}

/// `mitiru_module_load` の中身。状態を確保し callback table を埋める。
template<class T>
void registerGame(ModuleApi* api, void** memory)
{
	static_assert(kHasGameEntry<T>,
		"MITIRU_GAME(T): T に update(Input, Hud, float) / update(Input, float) / draw(Screen&) の"
		"いずれも見つかりません。メソッド名と引数 (型・dt・大文字小文字) を確認してください。");

	// GameMemory は flat POD 必須。host が GameMemory を bytes として memcpy で
	// 記録・rewind するため、ポインタ (std::vector/std::string/std::deque 等) を含むと
	// 巻き戻し / replay が再現しない。固定長コンテナに置き換えること。
	static_assert(std::is_trivially_copyable_v<T>,
		"MITIRU_GAME(T): GameMemory は flat POD (trivially_copyable) である必要があります。"
		"std::vector / std::string / std::deque 等のヒープ所有メンバを mitiru::FixedVec<T,N> / "
		"mitiru::FixedString<N> (#include <mitiru/core/FixedVec.hpp>) に置き換えてください。"
		"観測ログ等の非 gameplay state は GameMemory の外 (DLL 内 static) へ。理由: host が "
		"GameMemory を bytes で memcpy 記録・rewind するため。");

	if (api == nullptr || memory == nullptr) { return; }
	if (*memory == nullptr) { *memory = new T{}; }   // reload 時はホストが既存 pointer を渡す
	api->version     = kWireApiVersion;  // 数値 + build 指紋 (H-1/H-4)。host は完全一致のみ受理
	api->on_init     = &gameInit<T>;
	api->on_update   = &gameUpdate<T>;
	api->on_draw     = &gameDraw<T>;
	api->on_shutdown = &gameShutdown<T>;
	// draw(Canvas&) を持つ game だけ POD コマンド経路 (ABI v31) を追加で export する。
	// draw(Screen&) は互換のため残す (host は on_draw_commands 非 null を優先)。
	if constexpr (requires(T& g) { g.draw(std::declval<mitiru::Canvas&>()); })
	{
		api->on_draw_commands = &gameDrawCommands<T>;
	}
	// GameMemory は flat POD 保証済み (上の static_assert)。録画再生・rewind の
	// 単一 state 源として byte 数を無条件に申告する。
	api->memorySize        = static_cast<std::uint32_t>(sizeof(T));
	api->seriesProbeCount  = 0;  // MITIRU_GAME_SERIES が観測 probe を上書きする
	registerStateReflection<T>();
}

/// @brief 観測 probe テーブルを ModuleApi に詰める (MITIRU_GAME_SERIES が使う)。
inline void registerSeriesProbes(ModuleApi* api, const SeriesProbe* probes, std::size_t n)
{
	if (api == nullptr || probes == nullptr) { return; }
	const std::size_t cap = sizeof(api->seriesProbes) / sizeof(api->seriesProbes[0]);
	const std::size_t count = (n < cap) ? n : cap;
	for (std::size_t i = 0; i < count; ++i) { api->seriesProbes[i] = probes[i]; }
	api->seriesProbeCount = static_cast<std::int32_t>(count);
}

template<class T>
void unregisterGame(void* memory) { delete static_cast<T*>(memory); }

// ── 非 POD の game (MITIRU_GAME_OBJECTS、ADR 0040) ─────────────────────────────
// GameMemory は進行データ P (flat POD) だけ。場面の中身 G はこの DLL の中に 1 個だけ生きる普通の
// C++ オブジェクトで、仮想関数もヒープも使ってよい。G は「P から組み立て直せる派生物」として扱う。
// 無ければ作る (ホットリロード直後・初回)、host が P を書き換えたら捨てて作り直す (ロード)。
template<class G>
std::unique_ptr<G>& objectsInstance() noexcept
{
	static std::unique_ptr<G> s_instance;
	return s_instance;
}

template<class G, class P>
G& ensureObjects(P& progress)
{
	auto& instance = objectsInstance<G>();
	if (!instance)
	{
		instance = std::make_unique<G>();
		if constexpr (requires { instance->build(std::as_const(progress)); }) { instance->build(std::as_const(progress)); }
	}
	return *instance;
}

template<class G, class P>
void objectsInit(void* mem)
{
	if (mem == nullptr) { return; }
	static const P kFresh{};
	std::memcpy(mem, &kFresh, sizeof(P));
	P& p = *static_cast<P*>(mem);
	if constexpr (requires { p.init(); }) { p.init(); }
	objectsInstance<G>().reset();  // restart でも同じ経路。次の update が新しい P から組み立てる
}

template<class G, class P>
void objectsUpdate(void* mem, float dt, const InputSnapshot* in, FrameIntents* out)
{
	if (mem == nullptr || in == nullptr || out == nullptr) { return; }
	P& p = *static_cast<P*>(mem);
	G& g = ensureObjects<G, P>(p);
	mitiru::Input input{in};
	mitiru::Hud   hud{out};
	if      constexpr (requires { g.update(p, input, hud, dt); }) { g.update(p, input, hud, dt); }
	else if constexpr (requires { g.update(p, input, dt); })      { g.update(p, input, dt); }
	else if constexpr (requires { g.update(p, dt); })             { g.update(p, dt); }
	else { (void)input; (void)hud; (void)dt; }
}

template<class G, class P>
void objectsDraw(void* mem, mitiru::Screen* screen)
{
	if (mem == nullptr || screen == nullptr) { return; }
	P& p = *static_cast<P*>(mem);
	G& g = ensureObjects<G, P>(p);
	if constexpr (requires { g.draw(std::as_const(p), *screen); }) { g.draw(std::as_const(p), *screen); }
	else { (void)g; }
}

template<class G, class P>
void objectsDrawCommands(void* mem, const DrawContext* ctx, DrawCommandBuffer* out)
{
	if (mem == nullptr || ctx == nullptr || out == nullptr) { return; }
	P& p = *static_cast<P*>(mem);
	G& g = ensureObjects<G, P>(p);
	if constexpr (requires { g.draw(std::as_const(p), std::declval<mitiru::Canvas&>()); })
	{
		mitiru::Canvas canvas{*out, *ctx};
		g.draw(std::as_const(p), canvas);
	}
	else { (void)g; }
}

template<class G, class P>
void objectsRebuild(void* mem, std::uint32_t /*reason*/)
{
	if (mem == nullptr) { return; }
	objectsInstance<G>().reset();
	(void)ensureObjects<G, P>(*static_cast<P*>(mem));
}

template<class G, class P>
void objectsShutdown(void* mem)
{
	auto& instance = objectsInstance<G>();
	if (instance && mem != nullptr)
	{
		P& p = *static_cast<P*>(mem);
		if constexpr (requires { instance->shutdown(p); }) { instance->shutdown(p); }
		else { (void)p; }
	}
	instance.reset();  // DLL unload 後に古い vtable を指すオブジェクトを残さない
}

template<class G, class P>
inline constexpr bool kHasObjectsEntry =
	requires(G& g, P& p, mitiru::Input in, mitiru::Hud hud, float dt) { g.update(p, in, hud, dt); } ||
	requires(G& g, P& p, mitiru::Input in, float dt) { g.update(p, in, dt); } ||
	requires(G& g, P& p, float dt) { g.update(p, dt); } ||
	requires(G& g, const P& p, mitiru::Screen& s) { g.draw(p, s); };

/// `MITIRU_GAME_OBJECTS` の中身。GameMemory = P を確保し、G への trampoline を埋める。
template<class G, class P>
void registerObjectsGame(ModuleApi* api, void** memory)
{
	static_assert(std::is_trivially_copyable_v<P>,
		"MITIRU_GAME_OBJECTS(Game, Progress): Progress は flat POD (trivially_copyable) である必要があります。"
		"セーブ・ロード・録画は Progress の bytes に対して働きます。クラスの木や std::vector は Game 側へ。");
	static_assert(std::is_default_constructible_v<G>,
		"MITIRU_GAME_OBJECTS(Game, Progress): Game は引数なしで構築できる必要があります (場面は build(const Progress&) で組み立てる)。");
	// build が進行データを書き換えると、replay のロード代用 (記録済みの bytes を書き戻してから on_rebuild) で
	// 二重に書き換わり、録画と 1 フレームで食い違う。組み立ては読むだけにする。
	static_assert(!requires(G& g, P& p) { g.build(p); } || requires(G& g, const P& p) { g.build(p); },
		"MITIRU_GAME_OBJECTS(Game, Progress): Game::build は const Progress& を受け取ってください "
		"(場面の組み立ては進行データを読むだけにする。書き換えると replay が一致しなくなります)。");
	static_assert(kHasObjectsEntry<G, P>,
		"MITIRU_GAME_OBJECTS(Game, Progress): Game に update(Progress&, Input, Hud, float) / update(Progress&, Input, float) / "
		"update(Progress&, float) / draw(const Progress&, Screen&) のいずれも見つかりません。");

	if (api == nullptr || memory == nullptr) { return; }
	if (*memory == nullptr) { *memory = new P{}; }   // reload 時はホストが既存 pointer を渡す
	api->version     = kWireApiVersion;
	api->on_init     = &objectsInit<G, P>;
	api->on_update   = &objectsUpdate<G, P>;
	api->on_draw     = &objectsDraw<G, P>;
	api->on_shutdown = &objectsShutdown<G, P>;
	api->on_rebuild  = &objectsRebuild<G, P>;
	if constexpr (requires(G& g, const P& p) { g.draw(p, std::declval<mitiru::Canvas&>()); })
	{
		api->on_draw_commands = &objectsDrawCommands<G, P>;
	}
	api->memorySize         = static_cast<std::uint32_t>(sizeof(P));
	api->stateFlags         = kModuleStatePartial;
	api->seriesProbeCount   = 0;
	registerStateReflection<P>();
}

template<class G, class P>
void unregisterObjectsGame(void* memory)
{
	objectsInstance<G>().reset();
	delete static_cast<P*>(memory);
}

}  // namespace module::detail

namespace module
{

/// @brief member pointer から SeriesProbe を合成する (§8-1)。MITIRU_SERIES_FIELD の実体。
/// @details accessor は capture 無し lambda の関数ポインタ変換 (= C 関数ポインタ) なので
///          DLL 境界に安全。offset / 型は member pointer から自動導出される。
template <class T, auto MemberPtr>
[[nodiscard]] inline SeriesProbe makeSeriesProbe(const char* name, const char* title,
                                                 double threshold = 0.0,
                                                 bool hasThreshold = false) noexcept
{
	using M = std::remove_cv_t<std::remove_reference_t<
		decltype(static_cast<const T*>(nullptr)->*MemberPtr)>>;
	static_assert(std::is_arithmetic_v<M>,
		"MITIRU_SERIES_FIELD: 数値 field (int / float / double 等) のみ系列化できます");
	SeriesProbe p{};
	detail::copyTag(p.name,  sizeof(p.name),  name);
	detail::copyTag(p.title, sizeof(p.title), title);
	p.accessor = [](const void* mem) noexcept -> double
	{
		return static_cast<double>(static_cast<const T*>(mem)->*MemberPtr);
	};
	p.threshold    = threshold;
	p.hasThreshold = hasThreshold ? 1 : 0;
	return p;
}

}  // namespace module
}  // namespace mitiru

#if defined(_WIN32)
#  define MITIRU_GAME_EXPORT __declspec(dllexport)
#else
#  define MITIRU_GAME_EXPORT __attribute__((visibility("default")))
#endif

/// GameMemory の形の hash と反射の全件を別 export で出す。host が反射を受け取る経路はこれだけ。
#define MITIRU_GAME_STATE_EXPORTS(StateType)                                  \
	extern "C" MITIRU_GAME_EXPORT                                                \
	std::uint64_t mitiru_module_layout_hash()                                    \
	{                                                                            \
		return ::mitiru::module::detail::layoutHashOf<StateType>();                 \
	}                                                                            \
	extern "C" MITIRU_GAME_EXPORT                                                \
	std::int32_t mitiru_module_reflect_fields(                                   \
		::mitiru::module::FieldDescriptor* out, std::int32_t cap)                   \
	{                                                                            \
		return ::mitiru::module::detail::reflectFieldsOut(out, cap);                \
	}                                                                            \
	extern "C" MITIRU_GAME_EXPORT                                                \
	std::int32_t mitiru_module_reflect_schemas(                                  \
		::mitiru::module::ReflectSchema* out, std::int32_t cap)                     \
	{                                                                            \
		return ::mitiru::module::detail::reflectSchemasOut(out, cap);               \
	}

/// ゲームの構造体を DLL の入口に結びつける。これ 1 行で mitiru_module_load / unload が出来る。
/// ファイルスコープ (関数の外) に 1 回だけ書く。
#define MITIRU_GAME(GameType)                                                  \
	extern "C" MITIRU_GAME_EXPORT                                              \
	void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)     \
	{                                                                         \
		mitiru::module::detail::registerGame<GameType>(api, memory);          \
	}                                                                         \
	extern "C" MITIRU_GAME_EXPORT                                              \
	void mitiru_module_unload(void* memory)                                   \
	{                                                                         \
		mitiru::module::detail::unregisterGame<GameType>(memory);             \
	}                                                                            \
	MITIRU_GAME_STATE_EXPORTS(GameType)

/// クラスの木・仮想関数・ヒープで書く game の入口 (ADR 0040)。`Progress` は flat POD の進行データ
/// (セーブ・ロード・録画の対象 = GameMemory)、`Game` は場面の中身で、`build(const Progress&)` で
/// 進行データから組み立て直せること。host は rewind の scrub / resim / 分岐 / 候補の並走を断る
/// (GameMemory が全状態ではないため)。`--replay` は入力の流し直しなので、Game が決定論なら通る。
#define MITIRU_GAME_OBJECTS(GameType, ProgressType)                                        \
	extern "C" MITIRU_GAME_EXPORT                                                          \
	void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)                 \
	{                                                                                     \
		mitiru::module::detail::registerObjectsGame<GameType, ProgressType>(api, memory);  \
	}                                                                                     \
	extern "C" MITIRU_GAME_EXPORT                                                          \
	void mitiru_module_unload(void* memory)                                               \
	{                                                                                     \
		mitiru::module::detail::unregisterObjectsGame<GameType, ProgressType>(memory);     \
	}                                                                            \
	MITIRU_GAME_STATE_EXPORTS(ProgressType)

/// 入力のアクションの表 (input_actions.json と同じ JSON) を DLL から host へ渡す (v48)。DLL の隣の
/// input_actions.json より優先する。表の i 番目の操作が in.actionDown(i) になる。ファイルスコープに 1 回。
#define MITIRU_ACTIONS(manifestJson)                                           \
	extern "C" MITIRU_GAME_EXPORT                                              \
	const char* mitiru_module_action_manifest()                               \
	{                                                                         \
		return manifestJson;                                                  \
	}

/// 形の変わった GameMemory のセーブを移す関数を host へ渡す (v48)。fn は ModuleMigrateFn と同じ形で、
/// newMemory には今の GameMemory の写しが入っている。名前の一致で移すより先に試される。
#define MITIRU_MIGRATE(fn)                                                     \
	extern "C" MITIRU_GAME_EXPORT                                              \
	std::int32_t mitiru_module_migrate(const void* oldBytes, std::uint64_t oldSize, \
	                                   std::uint64_t oldLayoutHash, void* newMemory, std::uint64_t newSize) \
	{                                                                         \
		return (fn)(oldBytes, oldSize, oldLayoutHash, newMemory, newSize);    \
	}

/// ツール窓 (ai / nav) に見せる資産を host へ渡す (v49)。fn は int32 fn(InspectAsset* out, int32 cap) で、書いた数を返す。
/// ビヘイビアツリーの JSON (kind "bt_tree") と焼いたナビメッシュ (kind "navmesh") は GameMemory に無いので、ここから渡すと
/// ツール窓が木の形と床の形を出せる。host は読み込みの時と、asset.reloaded を受けた update の後 (v50) に fn を呼んで写すので、
/// data は次に写すまで変えない。ファイルスコープに 1 回。
#define MITIRU_INSPECT_ASSETS(fn)                                              \
	extern "C" MITIRU_GAME_EXPORT                                              \
	std::int32_t mitiru_module_inspect_assets(mitiru::module::InspectAsset* out, std::int32_t cap) \
	{                                                                         \
		return (fn)(out, cap);                                                \
	}

/// host 権威のオンライン (ADR 0068) で、参加者の手元の自分を先に動かす関数を host へ渡す (v51)。T は
/// `void predict(mitiru::Input in, int player, float dt)` を持ち、player の分だけを update と同じ式で進める (他の人と
/// 敵には触らない)。host は描く直前に GameMemory の写しを作り、host にまだ使われていない自分の入力を 1 フレーム分ずつ
/// 渡して呼ぶ。写しは描くだけに使い、状態が届くたびに host の状態から作り直す。ファイルスコープに 1 回。
#define MITIRU_NET_PREDICT(GameType)                                           \
	extern "C" MITIRU_GAME_EXPORT                                              \
	void mitiru_module_net_predict(void* drawMemory, const mitiru::module::InputSnapshot* local, std::uint8_t player) \
	{                                                                         \
		mitiru::module::detail::gameNetPredict<GameType>(drawMemory, local, player); \
	}

/// 旧名の後方互換エイリアス。flat POD 必須は MITIRU_GAME 自体に統合されたので
/// 中身は同じ。新規コードは MITIRU_GAME を使ってよい。
#define MITIRU_GAME_RECORDABLE(GameType) MITIRU_GAME(GameType)

/// game が「巻き戻しリングに何バイトまで使ってよいか」を宣言する (optional)。MITIRU_GAME と併記する。
/// `MITIRU_REWIND_BUFFER(frames)` のバイト版で、対称に `ModuleHost::rewindBudgetBytesFn()` が
/// GetProcAddress で解決する別 export (ModuleApi 自体の ABI は変えない)。
/// 優先順位は host の `--rewind-mb N` (明示指定時) > この宣言 > 既定 512MB。
#define MITIRU_REWIND_BUDGET(bytes)                                            \
	extern "C" MITIRU_GAME_EXPORT                                              \
	std::uint64_t mitiru_module_rewind_budget_bytes()                         \
	{                                                                         \
		return static_cast<std::uint64_t>(bytes);                             \
	}

// MITIRU_REWIND_BUFFER (フレーム数) と MITIRU_REWIND_BUDGET (バイト数) は名前だけでは
// 単位が伝わらず取り違えやすい (D8)。単位を明示したエイリアスを併記する。中身は同じ
// マクロへの単純委譲で、docs はこちらの名前で統一する。
#define MITIRU_REWIND_BUFFER_FRAMES(frames) MITIRU_REWIND_BUFFER(frames)
#define MITIRU_REWIND_BUDGET_BYTES(bytes)   MITIRU_REWIND_BUDGET(bytes)

/// game が「pause 中でも dt を受け取り続けたい layer」を宣言する (optional、2-1)。
/// MITIRU_GAME と併記する。Godot の `process_mode = PROCESS_MODE_WHEN_PAUSED` 相当で、
/// `InputSnapshot::dtByLayer[8]` (ABI v30) の layout は変えず、`MITIRU_REWIND_BUDGET` と
/// 同じ別 export (`ModuleHost::pauseAlwaysLayersMaskFn()`) を host が起動時に解決する。
/// mask の bit i が立った layer i は pause 中も通常どおり dt を受け取る (ポーズメニュー
/// 演出用)。未宣言なら mask=0 = 従来どおり pause は全 layer 共通。
#define MITIRU_PAUSE_ALWAYS_LAYERS(mask)                                       \
	extern "C" MITIRU_GAME_EXPORT                                              \
	std::uint8_t mitiru_module_pause_always_layers_mask()                     \
	{                                                                         \
		return static_cast<std::uint8_t>(mask);                               \
	}

/// pause の種類ごとに「pause 中も dt を受け取る layer」を分けて宣言する (optional)。HE2 の
/// layersActiveDuringIngamePause / DebugPause / ObjectPause 相当。宣言しなければ 3 種類とも
/// `MITIRU_PAUSE_ALWAYS_LAYERS` の mask (未宣言なら 0)。種類は `Input::pauseKind()` で読める。
#define MITIRU_PAUSE_LAYERS_BY_KIND(ingameMask, debugMask, objectMask)          \
	extern "C" MITIRU_GAME_EXPORT                                              \
	std::uint8_t mitiru_module_pause_layers_by_kind(std::uint8_t kind)        \
	{                                                                         \
		switch (kind)                                                         \
		{                                                                     \
		case 1:  return static_cast<std::uint8_t>(ingameMask);                \
		case 2:  return static_cast<std::uint8_t>(debugMask);                 \
		case 3:  return static_cast<std::uint8_t>(objectMask);                \
		default: return 0;                                                    \
		}                                                                     \
	}

/// MITIRU_GAME に加えて rewind 観測 probe を宣言する。
/// GameMemory から double を引く capture 無しの純関数を列挙すると、host が GameMemoryRing の
/// 各フレームに適用して HP 履歴等の系列を自動生成し、inspector の rewind graph に出す。
/// 作者が手で履歴を貯めたり JSON を組んだりする必要はない。
///
/// @code
///   double hpProbe(const void* m){ return static_cast<const MyMem*>(m)->hp; }
///   MITIRU_GAME_SERIES(MyMem,
///       { "hp", "HP",       &hpProbe, 35.0, 1 },   // 35 を下抜けたら danger marker
///       { "x",  "Player X", &xProbe,  0.0,  0 });
/// @endcode
#define MITIRU_GAME_SERIES(GameType, ...)                                      \
	extern "C" MITIRU_GAME_EXPORT                                              \
	void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)     \
	{                                                                         \
		mitiru::module::detail::registerGame<GameType>(api, memory);          \
		const mitiru::module::SeriesProbe _mitiruProbes[] = { __VA_ARGS__ };   \
		mitiru::module::detail::registerSeriesProbes(                         \
			api, _mitiruProbes,                                               \
			sizeof(_mitiruProbes) / sizeof(_mitiruProbes[0]));                \
	}                                                                         \
	extern "C" MITIRU_GAME_EXPORT                                              \
	void mitiru_module_unload(void* memory)                                   \
	{                                                                         \
		mitiru::module::detail::unregisterGame<GameType>(memory);             \
	}                                                                            \
	MITIRU_GAME_STATE_EXPORTS(GameType)

/// probe 関数の手書き (cast 定型文) を消す糖衣 (§8-1)。field 名だけで系列化する。
/// offset / 型は member pointer から自動導出。MITIRU_GAME_SERIES の要素として使う。
///
/// @code
///   MITIRU_GAME_SERIES(MyGame,
///       MITIRU_SERIES_FIELD_DANGER(MyGame, hp, "HP", 35.0),  // 35 下抜けで danger marker
///       MITIRU_SERIES_FIELD(MyGame, score));                 // ラベル = field 名
/// @endcode
#define MITIRU_SERIES_FIELD(GameType, field)                                   \
	::mitiru::module::makeSeriesProbe<GameType, &GameType::field>(#field, #field)

/// 同上 + 人間向けラベルと danger 閾値 (閾値跨ぎが rewind marker になる)。
#define MITIRU_SERIES_FIELD_DANGER(GameType, field, title, thresholdValue)     \
	::mitiru::module::makeSeriesProbe<GameType, &GameType::field>(              \
		#field, title, thresholdValue, true)

// ── GameMemory リフレクション ──────────────────────────────────
// MITIRU_REFLECT(Type, field...) で GameMemory の全フィールドを host に申告する。
// host が GameMemory バイト列 (現フレーム + rewind ring の過去) を構造化 JSON 化し、
// AI が全状態を読めるようになる。MITIRU_GAME / MITIRU_GAME_SERIES と併用する。
//
//   MITIRU_REFLECT_STRUCT(ns::Enemy, x, y, alive);   // FixedVec の要素 struct を先に
//   MITIRU_REFLECT(ns::Memory, player, hp, enemies); // GameMemory 本体 (全部グローバル scope)
//
// 内部: __VA_ARGS__ の各フィールド名に makeFieldDescriptor<decltype(member)>(#member, offsetof)
// を適用する bounded FOR_EACH (最大 16 フィールド)。

#define MITIRU_RFL_CAT_(a, b) a##b
#define MITIRU_RFL_CAT(a, b)  MITIRU_RFL_CAT_(a, b)
#define MITIRU_RFL_EXPAND(x)  x

// 1 メンバ → FieldDescriptor (型は decltype、offset は offsetof で自動導出)
#define MITIRU_RFL_MK(Type, member)                                            \
	::mitiru::module::makeFieldDescriptor<                                     \
		std::remove_reference_t<decltype(((Type*)nullptr)->member)>>(          \
		#member, static_cast<std::uint32_t>(offsetof(Type, member)))

// bounded FOR_EACH: M(T,f1), M(T,f2), ... をカンマ区切りで展開 (最大 16)
#define MITIRU_FE_1(M, T, a)       M(T, a)
#define MITIRU_FE_2(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_1(M, T, __VA_ARGS__))
#define MITIRU_FE_3(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_2(M, T, __VA_ARGS__))
#define MITIRU_FE_4(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_3(M, T, __VA_ARGS__))
#define MITIRU_FE_5(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_4(M, T, __VA_ARGS__))
#define MITIRU_FE_6(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_5(M, T, __VA_ARGS__))
#define MITIRU_FE_7(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_6(M, T, __VA_ARGS__))
#define MITIRU_FE_8(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_7(M, T, __VA_ARGS__))
#define MITIRU_FE_9(M, T, a, ...)  M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_8(M, T, __VA_ARGS__))
#define MITIRU_FE_10(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_9(M, T, __VA_ARGS__))
#define MITIRU_FE_11(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_10(M, T, __VA_ARGS__))
#define MITIRU_FE_12(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_11(M, T, __VA_ARGS__))
#define MITIRU_FE_13(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_12(M, T, __VA_ARGS__))
#define MITIRU_FE_14(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_13(M, T, __VA_ARGS__))
#define MITIRU_FE_15(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_14(M, T, __VA_ARGS__))
#define MITIRU_FE_16(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_15(M, T, __VA_ARGS__))
#define MITIRU_FE_17(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_16(M, T, __VA_ARGS__))
#define MITIRU_FE_18(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_17(M, T, __VA_ARGS__))
#define MITIRU_FE_19(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_18(M, T, __VA_ARGS__))
#define MITIRU_FE_20(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_19(M, T, __VA_ARGS__))
#define MITIRU_FE_21(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_20(M, T, __VA_ARGS__))
#define MITIRU_FE_22(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_21(M, T, __VA_ARGS__))
#define MITIRU_FE_23(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_22(M, T, __VA_ARGS__))
#define MITIRU_FE_24(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_23(M, T, __VA_ARGS__))
#define MITIRU_FE_25(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_24(M, T, __VA_ARGS__))
#define MITIRU_FE_26(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_25(M, T, __VA_ARGS__))
#define MITIRU_FE_27(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_26(M, T, __VA_ARGS__))
#define MITIRU_FE_28(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_27(M, T, __VA_ARGS__))
#define MITIRU_FE_29(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_28(M, T, __VA_ARGS__))
#define MITIRU_FE_30(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_29(M, T, __VA_ARGS__))
#define MITIRU_FE_31(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_30(M, T, __VA_ARGS__))
#define MITIRU_FE_32(M, T, a, ...) M(T, a), MITIRU_RFL_EXPAND(MITIRU_FE_31(M, T, __VA_ARGS__))

// MITIRU_REFLECT / MITIRU_REFLECT_STRUCT は最大 32 フィールド。33 個以上 (40 個まで) は
// MITIRU_FE_ERR が選ばれ、削除済み関数
// `mitiruReflect_Max32Fields_SplitOrUseReflectStruct` (Reflection.hpp) の使用エラーになる。
// 関数名がそのまま対処法になっている。フィールドを分割するか、ネスト部分を MITIRU_REFLECT_STRUCT
// へ切り出す。41 個以上はプリプロセッサ構造上ここで拾えず、別の compile error になる。
#define MITIRU_FE_ERR(M, T, ...)                                               \
	::mitiru::module::detail::mitiruReflect_Max32Fields_SplitOrUseReflectStruct()

#define MITIRU_FE_PICK(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, \
	_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32,          \
	_33,_34,_35,_36,_37,_38,_39,_40,NAME,...) NAME
#define MITIRU_FOR_EACH(M, T, ...)                                             \
	MITIRU_RFL_EXPAND(MITIRU_FE_PICK(__VA_ARGS__,                              \
		MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR,            \
		MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR,            \
		MITIRU_FE_32, MITIRU_FE_31, MITIRU_FE_30, MITIRU_FE_29, MITIRU_FE_28,  \
		MITIRU_FE_27, MITIRU_FE_26, MITIRU_FE_25, MITIRU_FE_24, MITIRU_FE_23,  \
		MITIRU_FE_22, MITIRU_FE_21, MITIRU_FE_20, MITIRU_FE_19, MITIRU_FE_18,  \
		MITIRU_FE_17, MITIRU_FE_16, MITIRU_FE_15, MITIRU_FE_14, MITIRU_FE_13,  \
		MITIRU_FE_12, MITIRU_FE_11, MITIRU_FE_10, MITIRU_FE_9, MITIRU_FE_8,    \
		MITIRU_FE_7, MITIRU_FE_6, MITIRU_FE_5, MITIRU_FE_4, MITIRU_FE_3,       \
		MITIRU_FE_2, MITIRU_FE_1)(M, T, __VA_ARGS__))

/// FixedVec<Struct,N> の要素 struct を先に宣言する (host が要素を 1 段ネスト JSON 化できる)。
/// グローバル scope で、型は完全修飾名で書くこと (例 MITIRU_REFLECT_STRUCT(ns::Enemy, x, y))。
#define MITIRU_REFLECT_STRUCT(Type, ...)                                       \
	namespace mitiru { namespace module {                                      \
		template<> struct ReflectName<Type> {                                  \
			static constexpr const char* value = #Type; };                    \
	} }                                                                        \
	static const bool MITIRU_RFL_CAT(_mitiruSchema_, __COUNTER__) =            \
		::mitiru::module::registerSchema(#Type,                               \
			{ MITIRU_FOR_EACH(MITIRU_RFL_MK, Type, __VA_ARGS__) })

/// GameMemory のフィールドを host に申告する。グローバル scope、完全修飾名で。
#define MITIRU_REFLECT(Type, ...)                                              \
	namespace mitiru { namespace module { namespace detail {                   \
		template<> struct ReflectionOf<Type> {                                 \
			static void fill() {                                               \
				const ::mitiru::module::FieldDescriptor _mitiruFields[] = {    \
					MITIRU_FOR_EACH(MITIRU_RFL_MK, Type, __VA_ARGS__) };       \
				::mitiru::module::detail::registerReflection(_mitiruFields,    \
					static_cast<std::int32_t>(                                \
						sizeof(_mitiruFields) / sizeof(_mitiruFields[0])));   \
			}                                                                  \
		};                                                                     \
	} } }
