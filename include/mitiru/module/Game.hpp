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
/// `init` / `update` / `draw` はすべて任意。書いたものだけ呼ばれる。`MyGame` が
/// flat POD なら録画再生の対象にもなる (byte 数を自動申告)。中身は `ModuleApi.hpp`
/// の C-ABI そのままで、ホスト側は何も変わらない。

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <mitiru/core/Color.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/core/PodTiming.hpp>  // POD タイマー/トゥイーン (GameMemory に埋めて使う)
#include <mitiru/core/MenuCursor.hpp>  // メニューのスティック/十字ナビ (POD)
#include <mitiru/core/Collide2D.hpp>   // タイルマップ AABB 移動解決 (moveAndCollide)
#include <mitiru/debug/ToolRegistry.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru
{

// 図形の基本型の短い別名。作者は sgc:: を書かなくてよい (色 Color は <mitiru/core/Color.hpp>)。
using Rect = sgc::Rectf;   ///< 矩形 {x, y, 幅, 高さ}
using Vec2 = sgc::Vec2f;   ///< 2D 座標 / ベクトル

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

/// ゲームパッド (XInput 主コントローラ) のボタン。値は ModuleApi の gamepad:: ビット。
enum class Pad : std::uint32_t
{
	Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008,
	Start = 0x0010, Back = 0x0020, LStick = 0x0040, RStick = 0x0080,
	LB = 0x0100, RB = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
};

/// スティックの傾き (各成分 -1..1)。
struct Stick { float x, y; };

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

	float mouseX() const noexcept { return s_->mouseX; }
	float mouseY() const noexcept { return s_->mouseY; }
	float mouseDeltaX() const noexcept { return s_->mouseDeltaX; }  ///< このフレームの移動量 (px、右が正)
	float mouseDeltaY() const noexcept { return s_->mouseDeltaY; }  ///< 同 (下が正)。ロック中も動く (FPS 視線)
	bool  mouseDown(int button = 0) const noexcept   ///< 押されている間 (0=左 1=右 2=中)
	{
		return button >= 0 && button < 3 && s_->mouseButtonsDown[button] != 0;
	}
	bool  mousePressed(int button = 0) const noexcept   ///< 押した瞬間だけ
	{
		return button >= 0 && button < 3 && s_->mouseButtonsJustPressed[button] != 0;
	}
	bool  mouseReleased(int button = 0) const noexcept  ///< 離した瞬間だけ
	{
		return button >= 0 && button < 3 && s_->mouseButtonsJustReleased[button] != 0;
	}

	/// HTML のボタン等から届いたアクションが来たフレームだけ true (例: "game.restart")。
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
	/// HTML ボタンが値 (難易度・スロット番号など) を伴うとき使う。
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

	// ── ゲームパッド (XInput 主コントローラ) ──────────────────────
	bool padConnected() const noexcept { return s_->gamepadConnected != 0; }
	bool padDown(Pad b)     const noexcept { return (s_->gamepadButtonsDown        & static_cast<std::uint32_t>(b)) != 0; }
	bool padPressed(Pad b)  const noexcept { return (s_->gamepadButtonsJustPressed  & static_cast<std::uint32_t>(b)) != 0; }
	bool padReleased(Pad b) const noexcept { return (s_->gamepadButtonsJustReleased & static_cast<std::uint32_t>(b)) != 0; }
	Stick leftStick()  const noexcept { return { s_->gamepadAxes[0], s_->gamepadAxes[1] }; }
	Stick rightStick() const noexcept { return { s_->gamepadAxes[2], s_->gamepadAxes[3] }; }
	float leftTrigger()  const noexcept { return s_->gamepadAxes[4]; }
	float rightTrigger() const noexcept { return s_->gamepadAxes[5]; }

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

	/// 音声クロック (秒、ABI v13)。host の audio backend の再生サンプル位置。
	/// **契約**: 0 = 未準備/非対応 (起動直後の数フレームや Null/headless) → game は
	/// フレーム dt 積算へフォールバックすること。**非ゼロになった後は単調非減少を
	/// engine が保証する** (backend の供給の谷でも巻き戻らない)。録画再生でも再現する。
	/// リズムゲームの同期は「audioTime()<=0 の間は dt クロック、以降は緩く lerp」が定石。
	double audioTime() const noexcept { return s_->audioTimeSec; }

	/// 音声出力レイテンシ (秒、ABI v19)。デバイスバッファに積んでから実際に耳へ届くまでの遅延。
	/// 0 = 不明 (Null/headless 等)。判定窓の耳基準補正に使う。
	double audioLatency() const noexcept { return s_->audioLatencySec; }

	/// 実際に耳へ届いている音声クロック位置 (秒、ABI v19)。= audioTime() - audioLatency()。
	/// **リズムゲームの判定はこの earTime() を基準にすると出力レイテンシ分のズレが構造的に消える**
	/// (audioTime() はデバイスへ送った位置 = 耳より先行)。audioTime() が未準備 (<=0) の間は 0。
	double earTime() const noexcept
	{
		const double t = s_->audioTimeSec - s_->audioLatencySec;
		return (s_->audioTimeSec > 0.0 && t > 0.0) ? t : 0.0;
	}

	/// 生の InputSnapshot へのアクセス (全 256 キー走査など、ラッパで足りない高度用途の escape hatch)。
	const module::InputSnapshot* raw() const noexcept { return s_; }

private:
	static bool held(int vk, const std::uint8_t* table) noexcept
	{
		return vk >= 0 && vk < 256 && table[vk] != 0;
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

// `Tool` enum + 開ける窓の registry (kToolTable) は <mitiru/debug/ToolRegistry.hpp>
// に置き、host 側 (openTool) と共有している。

/// 画面 (HUD) へ値を送る + 音を鳴らす + ツール窓を開く (`FrameIntents` の薄いビュー)。
class Hud
{
public:
	explicit Hud(module::FrameIntents* s) noexcept : s_(s) {}

	void set(const char* key, int v)         noexcept { s_->pushInt(key, v); }      ///< HTML の data-m-text へ
	void set(const char* key, float v)       noexcept { s_->pushFloat(key, v); }
	void set(const char* key, bool v)        noexcept { s_->pushBool(key, v); }
	void set(const char* key, const char* v) noexcept { s_->pushString(key, v); }

	/// 効果音を鳴らす。volume は 0..1 (1=原音量)。**volume 0 = 無音** (鳴らしたくない時は
	/// 呼ばないのが普通だが、変数で 0 が来ても最大音量にはならない)。
	void play(const char* soundId, float volume = 1.0f) noexcept
	{
		s_->playSound(soundId, clampVolume(volume));
	}
	/// 音をピッチ付きで鳴らす (pitch 0.5..2.0、1.0=原音)。1 つの SE を音階で鳴らすリズムゲーム等。
	/// **volume 0 = 無音**。pitch 0 は無意味なので、明示した pitch <= 0 は 1.0 (原音) に丸められる。
	void play(const char* soundId, float volume, float pitch) noexcept
	{
		s_->playSound(soundId, clampVolume(volume), pitch);
	}
	/// 効果音をループ再生する。stopLoop で止めるまで鳴り続ける。
	/// 長押しのように「押している間ずっと」鳴らしたい音に使う。短い音を継ぎ足して
	/// 伸ばすと継ぎ目が聴こえ、離した瞬間に切れる。同じ id が鳴っている間の再呼び出しは
	/// 鳴らし直さず音量とピッチだけを寄せる (音量スライダーの試聴のように、鳴らしたまま
	/// 音量を動かせる)。
	void playLoop(const char* soundId, float volume = 1.0f, float pitch = 1.0f,
	              float fadeInSec = 0.0f) noexcept
	{
		s_->loopSound(soundId, clampVolume(volume), pitch, fadeInSec);
	}
	/// playLoop で鳴らしている音を止める。releaseSec > 0 で減衰させてから止める。
	void stopLoop(const char* soundId, float releaseSec = 0.0f) noexcept
	{
		s_->stopSoundId(soundId, releaseSec);
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
	/// GameMemory をまるごとスロットへセーブする (`save/<slot>.msav`)。
	/// flat POD だからセーブ = スナップショット。巻き戻し・リプレイと同一機構。
	void save(const char* slot = "slot0") noexcept { s_->requestSave(slot); }
	/// スロットから GameMemory を復元する。GameMemory の struct を変更した後の
	/// 旧セーブは安全のため拒否される (初回 1 回警告)。リプレイ中は記録済み state で
	/// 代用されるため、セーブファイルが変わっていても再現は壊れない。
	void load(const char* slot = "slot0") noexcept { s_->requestLoad(slot); }
	/// ゲームを最初からやり直す (GameMemory を unload なしで fresh 再構築、§8-4)。
	/// update 内の `*this = MyGame{}` 手運びの代わり。host が memset 0 → init() を適用する。
	/// intent なので replay / resim では update が同フレームで再発行し bit-exact に再現される。
	void requestRestart() noexcept { s_->requestRestart(); }
	/// カーソルをロックする (FPS 視線)。毎フレーム呼ぶ。呼ばないフレームで解除される。
	void lockMouse() noexcept { s_->requestMouseLock(); }
	/// このフレームのスクリーンショットを保存する。
	void screenshot() noexcept { s_->requestScreenshotNow(); }
	/// inspector (別窓のデバッグツール) に観察データ (JSON 文字列) を送る。
	/// 必要なときだけ呼べばよい。inspector が開いている時にだけ映る。
	void watch(const char* name, const char* title, const char* json) noexcept
	{
		s_->pushInspectable(name, title, json);
	}

	/// 別窓のツールを開くよう host に頼む (必要なときだけ呼ぶ。既定では何も開かない)。
	void open(Tool t) noexcept
	{
		for (const auto& spec : detail::kToolTable)
		{
			if (spec.tool == t) { s_->requestToolWindow(spec.exe, spec.args); return; }
		}
	}
	/// 任意のツール窓を名前で開く (host が mitiru_<tool>.exe を探す)。上級者向け。
	void open(const char* tool, const char* args = "") noexcept { s_->requestToolWindow(tool, args); }
	/// 生 JS を実行 (escape hatch、data-m-* で足りるなら使わない)。
	void runJs(const char* code) noexcept { s_->runJs(code); }

private:
	/// 明示 volume <= 0 を実質無音 (0.0001) に丸める。intent の wire 上では 0 が
	/// 「未指定 = 既定音量 1.0」に予約されているため (zero-init 互換、SoundIntentRouter)、
	/// 「無音」は 0 でなく可聴未満の微小値で表す。
	static constexpr float clampVolume(float v) noexcept { return v > 0.0f ? v : 0.0001f; }

	module::FrameIntents* s_;
};

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
	// update(Input in, float dt) だけ書けば動く。Hud (HTML UI / 音) は要るときだけ。
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

template<class T>
void gameShutdown(void* mem)
{
	if (mem == nullptr) { return; }
	T& g = *static_cast<T*>(mem);
	if constexpr (requires { g.shutdown(); }) { g.shutdown(); }
	else { (void)g; }
}

// T が update / draw のどれかを「正しい署名で」持っているかを判定する。
// これが false の時に MITIRU_GAME すると、署名ミス (引数型 / dt 落とし / 大文字小文字) で
// update が無言で呼ばれない footgun になるため、compile error にして気付かせる。
template<class T>
inline constexpr bool kHasGameEntry =
	requires(T& g, mitiru::Input in, mitiru::Hud hud, float dt) { g.update(in, hud, dt); } ||
	requires(T& g, mitiru::Input in, float dt) { g.update(in, dt); } ||
	requires(T& g, mitiru::Hud hud, float dt) { g.update(hud, dt); } ||
	requires(T& g, float dt) { g.update(dt); } ||
	requires(T& g, mitiru::Screen& s) { g.draw(s); };

/// @brief GameMemory リフレクション記述子。`MITIRU_REFLECT` が特殊化する。
///        既定は no-op (reflection 非宣言 game は reflectFieldCount=0 のまま)。
template<class T> struct ReflectionOf { static void fillApi(ModuleApi*) noexcept {} };

/// `mitiru_module_load` の中身。状態を確保し callback table を埋める。
template<class T>
void registerGame(ModuleApi* api, void** memory)
{
	static_assert(kHasGameEntry<T>,
		"MITIRU_GAME(T): T に update(Input, Hud, float) / update(Input, float) / draw(Screen&) の"
		"いずれも見つかりません。メソッド名と引数 (型・dt・大文字小文字) を確認してください。");

	// GameMemory は flat POD 必須。host が GameMemory を bytes として memcpy で
	// 記録・rewind するため、ポインタ (std::vector/std::string/std::deque 等) を含むと
	// time-travel / replay が再現しない。固定長コンテナに置き換えること。
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
	// GameMemory は flat POD 保証済み (上の static_assert)。録画再生・time-travel・rewind の
	// 単一 state 源として byte 数を無条件に申告する。
	api->memorySize        = static_cast<std::uint32_t>(sizeof(T));
	api->seriesProbeCount  = 0;  // MITIRU_GAME_SERIES が観測 probe を上書きする
	api->reflectFieldCount = 0;  // MITIRU_REFLECT が reflection 記述子を上書きする
	api->reflectSchemaCount = 0;
	ReflectionOf<T>::fillApi(api);  // MITIRU_REFLECT 済みなら GameMemory 構造を申告
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

/// @brief reflection 記述子表 + 登録簿の要素 schema を ModuleApi に詰める (MITIRU_REFLECT が使う)。
inline void registerReflection(ModuleApi* api, const FieldDescriptor* fields, std::int32_t n)
{
	if (api == nullptr || fields == nullptr) { return; }
	const std::int32_t fcap =
		static_cast<std::int32_t>(sizeof(api->reflectFields) / sizeof(api->reflectFields[0]));
	if (n > fcap)
	{
		mitiru::debug::warnOnce("reflect.fields.overflow",
			"reflection のフィールド申告が ModuleApi の上限を超えています。超過分は無視されます");
	}
	const std::int32_t fc = (n < fcap) ? n : fcap;
	for (std::int32_t i = 0; i < fc; ++i) { api->reflectFields[i] = fields[i]; }
	api->reflectFieldCount = fc;

	const auto&        reg  = ::mitiru::module::reflectSchemaRegistry();
	const std::int32_t scap =
		static_cast<std::int32_t>(sizeof(api->reflectSchemas) / sizeof(api->reflectSchemas[0]));
	std::int32_t sc = static_cast<std::int32_t>(reg.size());
	if (sc > scap)
	{
		// 黙って切り捨てない: 9 個目以降の要素 struct は inspector / AI に出ない。
		mitiru::debug::warnOnce("reflect.schemas.overflow",
			"MITIRU_REFLECT_STRUCT の登録が上限 8 個を超えています。"
			"9 個目以降の要素 struct は inspector / AI へ出ません");
		sc = scap;
	}
	for (std::int32_t i = 0; i < sc; ++i) { api->reflectSchemas[i] = reg[static_cast<std::size_t>(i)]; }
	api->reflectSchemaCount = sc;
}

template<class T>
void unregisterGame(void* memory) { delete static_cast<T*>(memory); }

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
	}

/// 旧名の後方互換エイリアス。flat POD 必須は MITIRU_GAME 自体に統合された ので
/// 中身は同じ。新規コードは MITIRU_GAME を使ってよい。
#define MITIRU_GAME_RECORDABLE(GameType) MITIRU_GAME(GameType)

/// MITIRU_GAME に加えて time-travel 観測 probe を宣言する。
/// GameMemory から double を引く capture 無しの純関数を列挙すると、host が GameMemoryRing の
/// 各フレームに適用して HP 履歴等の系列を自動生成し、inspector の time-travel graph に出す。
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
	}

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

/// 同上 + 人間向けラベルと danger 閾値 (閾値跨ぎが time-travel marker になる)。
#define MITIRU_SERIES_FIELD_DANGER(GameType, field, title, thresholdValue)     \
	::mitiru::module::makeSeriesProbe<GameType, &GameType::field>(              \
		#field, title, thresholdValue, true)

// ── GameMemory リフレクション ──────────────────────────────────
// MITIRU_REFLECT(Type, field...) で GameMemory の全フィールドを host に申告する。
// host が GameMemory バイト列 (現フレーム + time-travel ring の過去) を構造化 JSON 化し、
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

// MITIRU_REFLECT / MITIRU_REFLECT_STRUCT は最大 16 フィールド。17 個以上 (24 個まで) は
// MITIRU_FE_ERR が選ばれ、削除済み関数
// `mitiruReflect_Max16Fields_SplitOrUseReflectStruct` (Reflection.hpp) の使用エラーになる
//。関数名がそのまま対処法: フィールドを分割するか、ネスト部分を MITIRU_REFLECT_STRUCT
// へ切り出す。25 個以上はプリプロセッサ構造上ここで拾えず、別の compile error になる。
#define MITIRU_FE_ERR(M, T, ...)                                               \
	::mitiru::module::detail::mitiruReflect_Max16Fields_SplitOrUseReflectStruct()

#define MITIRU_FE_PICK(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, \
	_17,_18,_19,_20,_21,_22,_23,_24,NAME,...) NAME
#define MITIRU_FOR_EACH(M, T, ...)                                             \
	MITIRU_RFL_EXPAND(MITIRU_FE_PICK(__VA_ARGS__,                              \
		MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR,            \
		MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR, MITIRU_FE_ERR,            \
		MITIRU_FE_16, MITIRU_FE_15, MITIRU_FE_14, MITIRU_FE_13, MITIRU_FE_12,  \
		MITIRU_FE_11, MITIRU_FE_10, MITIRU_FE_9, MITIRU_FE_8, MITIRU_FE_7,     \
		MITIRU_FE_6, MITIRU_FE_5, MITIRU_FE_4, MITIRU_FE_3, MITIRU_FE_2,       \
		MITIRU_FE_1)(M, T, __VA_ARGS__))

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
			static void fillApi(::mitiru::module::ModuleApi* api) {            \
				const ::mitiru::module::FieldDescriptor _mitiruFields[] = {    \
					MITIRU_FOR_EACH(MITIRU_RFL_MK, Type, __VA_ARGS__) };       \
				::mitiru::module::detail::registerReflection(api, _mitiruFields,\
					static_cast<std::int32_t>(                                \
						sizeof(_mitiruFields) / sizeof(_mitiruFields[0])));   \
			}                                                                  \
		};                                                                     \
	} } }
