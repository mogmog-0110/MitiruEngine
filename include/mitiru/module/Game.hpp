#pragma once

/// @file Game.hpp
/// @brief 初心者向けの薄い C++ ラッパ — `void*` / 生ポインタ / VK 添字を隠す。
/// @details
/// `ModuleApi.hpp` の C-ABI (`void* memory` / `InputSnapshot*` / `FrameIntents*`)
/// はホットリロード・録画再生・POD 境界 (ADR 0005) のために必要だが、ゲーム作者が
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
/// `init` / `update` / `draw` はすべて任意 — 書いたものだけ呼ばれる。`MyGame` が
/// flat POD なら録画再生の対象にもなる (byte 数を自動申告)。中身は `ModuleApi.hpp`
/// の C-ABI そのままで、ホスト側は何も変わらない。

#include <cstdint>
#include <type_traits>

#include <mitiru/core/Color.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/debug/ToolRegistry.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru
{

/// よく使うキー (値は Windows の仮想キーコード)。一覧に無いキーも `Key{0x..}` で渡せる。
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

	/// 決定論 seed (録画再生で bit-exact 再現するため、乱数は mitiru::Random rng(in.rngSeed()) で seed する)。
	std::uint64_t rngSeed() const noexcept { return s_->rngSeed; }

	/// 生の InputSnapshot へのアクセス (全 256 キー走査など、ラッパで足りない高度用途の escape hatch)。
	const module::InputSnapshot* raw() const noexcept { return s_; }

private:
	static bool held(int vk, const std::uint8_t* table) noexcept
	{
		return vk >= 0 && vk < 256 && table[vk] != 0;
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

	void play(const char* soundId, float volume = 1.0f) noexcept { s_->playSound(soundId, volume); }
	void quit() noexcept { s_->requestStop = 1; }   ///< ゲームを終了する

	// ── 演出 / デバッグ (必要なときだけ呼ぶ — pulled UI、ゲーム窓は汚さない) ──
	/// 画面を一瞬 c 色にフラッシュさせる (被弾演出など)。
	void flash(Color c, float seconds = 0.18f) noexcept { s_->pushTint(c.r, c.g, c.b, c.a, seconds); }
	/// このフレームのスクリーンショットを保存する。
	void screenshot() noexcept { s_->requestScreenshotNow(); }
	/// inspector (別窓のデバッグツール) に観察データ (JSON 文字列) を送る。
	/// 必要なときだけ呼べばよい — inspector が開いている時にだけ映る。
	void watch(const char* name, const char* title, const char* json) noexcept
	{
		s_->pushInspectable(name, title, json);
	}

	/// 別窓のツールを開くよう host に頼む (必要なときだけ呼ぶ — 既定では何も開かない)。
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
	module::FrameIntents* s_;
};

namespace module::detail
{

// ── C-ABI コールバックへの trampoline (void* を型に戻して typed メソッドを呼ぶ) ──
template<class T>
void gameInit(void* mem)
{
	if (mem == nullptr) { return; }
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
	// update(Input in, float dt) だけ書けば動く — Hud (HTML UI / 音) は要るときだけ。
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

/// `mitiru_module_load` の中身。状態を確保し callback table を埋める。
template<class T>
void registerGame(ModuleApi* api, void** memory)
{
	static_assert(kHasGameEntry<T>,
		"MITIRU_GAME(T): T に update(Input, Hud, float) / update(Input, float) / draw(Screen&) の"
		"いずれも見つかりません。メソッド名と引数 (型・dt・大文字小文字) を確認してください。");

	// GameMemory は flat POD 必須 (ADR 0017)。host が GameMemory を bytes として memcpy で
	// 記録・rewind するため、ポインタ (std::vector/std::string/std::deque 等) を含むと
	// time-travel / replay が再現しない。固定長コンテナに置き換えること。
	static_assert(std::is_trivially_copyable_v<T>,
		"MITIRU_GAME(T): GameMemory は flat POD (trivially_copyable) である必要があります。"
		"std::vector / std::string / std::deque 等のヒープ所有メンバを mitiru::FixedVec<T,N> / "
		"mitiru::FixedString<N> (#include <mitiru/core/FixedVec.hpp>) に置き換えてください。"
		"観測ログ等の非 gameplay state は GameMemory の外 (DLL 内 static) へ。理由: host が "
		"GameMemory を bytes で memcpy 記録・rewind するため (ADR 0005/0017)。");

	if (api == nullptr || memory == nullptr) { return; }
	if (*memory == nullptr) { *memory = new T{}; }   // reload 時はホストが既存 pointer を渡す
	api->version     = kCurrentApiVersion;
	api->on_init     = &gameInit<T>;
	api->on_update   = &gameUpdate<T>;
	api->on_draw     = &gameDraw<T>;
	api->on_shutdown = &gameShutdown<T>;
	// GameMemory は flat POD 保証済み (上の static_assert)。録画再生・time-travel・rewind の
	// 単一 state 源として byte 数を無条件に申告する (ADR 0013/0017)。
	api->memorySize       = static_cast<std::uint32_t>(sizeof(T));
	api->seriesProbeCount = 0;  // MITIRU_GAME_SERIES が観測 probe を上書きする
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

}  // namespace module::detail
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

/// 旧名の後方互換エイリアス。flat POD 必須は MITIRU_GAME 自体に統合された (ADR 0017) ので
/// 中身は同じ。新規コードは MITIRU_GAME を使ってよい。
#define MITIRU_GAME_RECORDABLE(GameType) MITIRU_GAME(GameType)

/// MITIRU_GAME に加えて time-travel 観測 probe を宣言する (ADR 0017)。
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
