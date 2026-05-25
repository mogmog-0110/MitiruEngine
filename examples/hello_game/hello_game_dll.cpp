// hello_game — Game-as-DLL port (v0.2.0 / ADR 0005, pure survival game)
//
// ADR 0005 (Host-Game C-only signal flow) reference implementation. The game
// window is a *pure game*: HP / SURVIVE timer / win-lose modal / restart only.
// All debug & tool chrome (time-travel scrubber, record/replay, pause, time
// scale, snapshot slots, command palette, screenshots) has been removed from
// the game window — those concerns belong to inspector sub-windows / the CLI.
//
// **Features**:
//   Gameplay: 移動 / 敵 / HP / 30s survive / win/lose
//   Quit:     ESC                       → intents.requestStop
//   Restart:  CEF button (game.restart) → InputSnapshot.actionEvents
//   HUD push: view.hud.*                → intents.statePushes
//   Game feel: trail / hit flash / enemy death fade / low-hp pulse
//   Asset hot reload: poll hello_game/assets/ mtime → intents.jsToExecute
//   Inspector exports: gameplay + input + timetravel → intents.exportedInspectables
//                      (sub-window channel — never drawn in the game window)
//
// The history ring buffer keeps recording every frame as the invisible
// substrate that inspector sub-windows observe; the game itself never scrubs.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/observe/EventLog.hpp>
#include <mitiru/observe/Invariant.hpp>
#include <mitiru/observe/TimeTravelRecorder.hpp>

#if defined(_WIN32)
#include <process.h>  // _getpid (for EventLog::open)
#else
#include <unistd.h>   // getpid
#endif

namespace hello_game
{

inline int currentPid()
{
#if defined(_WIN32)
	return _getpid();
#else
	return ::getpid();
#endif
}

// ── Gameplay constants ─────────────────────────────────────────────────────

constexpr float kPlayerSpeed   = 260.0f;
constexpr float kEnemySpeed    = 95.0f;
constexpr float kPlayerSize    = 28.0f;
constexpr float kEnemySize     = 30.0f;
constexpr int   kMaxHp         = 100;
constexpr int   kHitDamage     = 10;
constexpr float kEnemyRespawn  = 2.0f;
constexpr float kSurviveTime   = 30.0f;
constexpr int   kEnemyCount    = 4;

constexpr float kDefaultScreenW = 1280.0f;
constexpr float kDefaultScreenH = 720.0f;
constexpr std::size_t kHistoryCap = 300;  // 5s @ 60fps

namespace vk
{
constexpr int Escape      = 0x1B;
constexpr int Left        = 0x25;
constexpr int Up          = 0x26;
constexpr int Right       = 0x27;
constexpr int Down        = 0x28;
constexpr int A           = 0x41;
constexpr int B           = 0x42;  // debug: hold to force invariant break (demo)
}  // namespace vk

// ── Data structures ───────────────────────────────────────────────────────

struct Vec2 { float x; float y; };

struct Enemy
{
	Vec2  pos        {0.0f, 0.0f};
	bool  alive      {true};
	float respawnIn  {0.0f};
	Vec2  deathPos   {0.0f, 0.0f};  // 死亡 fade 描画用に最後の位置を凍結
	float deathFadeT {0.0f};        // 0.30 → 0 で fade out
};

/// Per-frame replayable snapshot — small enough for ~5s of history at 60fps.
/// Recorded continuously as the substrate an inspector sub-window observes.
struct Snapshot
{
	Vec2               playerPos;
	std::vector<Enemy> enemies;
	int                hp        {0};
	float              remaining {0.0f};
	float              t         {0.0f};
};

struct KeyEvent
{
	std::string name;
	float       time;
};

/// All persistent state lives here. Host owns the pointer across reloads.
struct HelloGameMemory
{
	// Gameplay
	Vec2               player    {kDefaultScreenW * 0.5f, kDefaultScreenH * 0.5f};
	std::vector<Enemy> enemies;
	int                hp        {kMaxHp};
	float              remaining {kSurviveTime};
	bool               gameOver  {false};
	std::string        outcome;
	std::uint32_t      rngSeed   {1u};

	// ── Game feel ───────────────────────────────────────────────────
	// Player trail — last N positions, drawn with decaying alpha.
	std::deque<Vec2>   trail;
	static constexpr std::size_t kTrailMax = 10;
	// Hit flash: set on damage, decays. drawRect alpha 演出 + HUD flash 連動。
	float              hitFlashT {0.0f};
	int                hitCount  {0};  // monotonic counter → HUD push (1-shot anim trigger)
	int                lastHitCount {-1};

	// Resize tracking — recenter player + respawn enemies when window changes
	// shape significantly (demo polish: never see player pinned at edge)
	float lastScreenW {0.0f};
	float lastScreenH {0.0f};

	// Screen size (resolved on first draw)
	float screenW {kDefaultScreenW};
	float screenH {kDefaultScreenH};

	// Wall-clock total since boot (monotonic timestamps for inspector exports)
	float totalTime {0.0f};

	// Frame counter — the time axis for the event timeline (EventLog) and the
	// deterministic check point for invariants (ADR 0005).
	std::uint32_t frame {0};

	// ── Dual-readable debug substrate (invisible to the game window) ──────
	// EventLog: append-only JSONL at %TEMP%\mitiru_events_<pid>.jsonl. Sparse
	// gameplay milestones (hit / death / game_over / restart / invariant
	// violation) land here. Both the inspector AND an AI agent can read it.
	mitiru::observe::EventLog    eventLog;
	// Invariant set: declared once in on_init, checked every frame in update.
	// Violations go to eventLog (machine-readable) and recent() (window/inspector).
	mitiru::observe::InvariantSet invariants;
	// Debug toggle: hold the invariant-break key to force hp negative so the
	// red overlay + violation event can be demonstrated. Restored when released.
	bool                          forceInvariantBreak {false};

	// ── Inspector substrate (invisible to the game window) ─────────────
	// History ring buffer recorded every frame. The game never scrubs it;
	// inspector sub-windows observe it via the timetravel export. Keeping it
	// recording is what powers the time-travel inspector (5 軸 #2).
	mitiru::observe::TimeTravelRecorder<Snapshot> history{kHistoryCap};

	// Input monitor (for the input inspectable)
	std::deque<KeyEvent>     keyHistory;
	std::array<int, 256>     pressCounts{};

	// HUD push throttling
	int pushTick {0};

	// HUD diff cache — avoid re-pushing unchanged values
	int  lastHp         {-1};
	int  lastMaxHp      {-1};
	int  lastTimeInt    {-1};
	bool lastGameOver   {false};
	std::string lastOutcome;

	// Asset hot reload (CEF)
	std::filesystem::file_time_type lastAssetMtime{};
	bool                            assetMtimeInitialized {false};
	int                             assetPollTick {0};

	// Scratch buffers (reuse to avoid per-frame heap traffic)
	std::string scratchJson;
};

// ── World setup ────────────────────────────────────────────────────────

void seedEnemy(Enemy& enemy, std::mt19937& rng,
               std::uniform_int_distribution<int>&    sideDist,
               std::uniform_real_distribution<float>& rx,
               std::uniform_real_distribution<float>& ry,
               float screenW, float screenH)
{
	const int side = sideDist(rng);
	switch (side)
	{
	case 0:  enemy.pos = {rx(rng), 0.0f};        break;
	case 1:  enemy.pos = {rx(rng), screenH};     break;
	case 2:  enemy.pos = {0.0f,    ry(rng)};     break;
	default: enemy.pos = {screenW, ry(rng)};     break;
	}
	enemy.alive     = true;
	enemy.respawnIn = 0.0f;
}

void spawnEnemies(HelloGameMemory& mem)
{
	std::mt19937                          rng(0xC0FFEEu);
	std::uniform_int_distribution<int>    sideDist(0, 3);
	std::uniform_real_distribution<float> rx(0.0f, mem.screenW);
	std::uniform_real_distribution<float> ry(0.0f, mem.screenH);

	mem.enemies.assign(kEnemyCount, Enemy{});
	for (auto& enemy : mem.enemies)
	{
		seedEnemy(enemy, rng, sideDist, rx, ry, mem.screenW, mem.screenH);
	}
}

void resetWorld(HelloGameMemory& mem)
{
	mem.hp         = kMaxHp;
	mem.remaining  = kSurviveTime;
	mem.gameOver   = false;
	mem.outcome.clear();
	mem.player     = {mem.screenW * 0.5f, mem.screenH * 0.5f};
	spawnEnemies(mem);
	mem.history.clear();
	// Force HUD re-push by setting "last seen" to OPPOSITE of current state
	// so pushHudDelta() detects a diff and emits the new value. Setting them
	// to the same value as current would silently skip the push (bug:
	// restart モーダルが消えない).
	mem.lastHp        = -1;
	mem.lastMaxHp     = -1;
	mem.lastTimeInt   = -1;
	mem.lastGameOver  = true;             // force diff vs current (false)
	mem.lastOutcome   = "__stale__";      // force diff vs current ("")
	mem.hitCount      = 0;
	mem.lastHitCount  = -1;
}

// ── Intent helpers (DLL → host) ───────────────────────────────────────────

void pushStateInt(mitiru::module::FrameIntents* intents,
                  const char* key, int value)
{
	const int cap = static_cast<int>(sizeof(intents->statePushes) /
	                                 sizeof(intents->statePushes[0]));
	if (intents->statePushCount >= cap) { return; }
	auto& slot = intents->statePushes[intents->statePushCount++];
	std::memset(&slot, 0, sizeof(slot));
	std::strncpy(slot.key, key, sizeof(slot.key) - 1);
	slot.kind   = 1;
	slot.intVal = value;
}

void pushStateBool(mitiru::module::FrameIntents* intents,
                   const char* key, bool value)
{
	const int cap = static_cast<int>(sizeof(intents->statePushes) /
	                                 sizeof(intents->statePushes[0]));
	if (intents->statePushCount >= cap) { return; }
	auto& slot = intents->statePushes[intents->statePushCount++];
	std::memset(&slot, 0, sizeof(slot));
	std::strncpy(slot.key, key, sizeof(slot.key) - 1);
	slot.kind   = 3;
	slot.intVal = value ? 1 : 0;
}

void pushStateString(mitiru::module::FrameIntents* intents,
                     const char* key, const std::string& value)
{
	const int cap = static_cast<int>(sizeof(intents->statePushes) /
	                                 sizeof(intents->statePushes[0]));
	if (intents->statePushCount >= cap) { return; }
	auto& slot = intents->statePushes[intents->statePushCount++];
	std::memset(&slot, 0, sizeof(slot));
	std::strncpy(slot.key, key, sizeof(slot.key) - 1);
	slot.kind = 4;
	std::strncpy(slot.strVal, value.c_str(), sizeof(slot.strVal) - 1);
}

void exportInspectable(mitiru::module::FrameIntents* intents,
                       const char* name, const char* title,
                       const std::string& jsonStr)
{
	const int cap = static_cast<int>(sizeof(intents->exportedInspectables) /
	                                 sizeof(intents->exportedInspectables[0]));
	if (intents->exportedInspectableCount >= cap) { return; }
	auto& slot = intents->exportedInspectables[intents->exportedInspectableCount++];
	std::memset(&slot, 0, sizeof(slot));
	std::strncpy(slot.name,  name,  sizeof(slot.name)  - 1);
	std::strncpy(slot.title, title, sizeof(slot.title) - 1);
	const auto cap_json = sizeof(slot.json) - 1;
	const auto n = jsonStr.size() < cap_json ? jsonStr.size() : cap_json;
	if (n > 0) { std::memcpy(slot.json, jsonStr.data(), n); }
	slot.json[n] = '\0';
	slot.jsonLen = static_cast<std::int32_t>(n);
}

void requestJsExec(mitiru::module::FrameIntents* intents,
                   const std::string& code)
{
	const std::int32_t cap = static_cast<std::int32_t>(
		sizeof(intents->jsToExecute) / sizeof(intents->jsToExecute[0]));
	const std::int32_t maxLen = cap > 0 ? cap - 1 : 0;
	const std::int32_t n = std::min<std::int32_t>(
		static_cast<std::int32_t>(code.size()), maxLen);
	if (n > 0) { std::memcpy(intents->jsToExecute, code.data(), n); }
	intents->jsToExecute[n] = '\0';
	intents->jsToExecuteLen = n;
}

// ── HUD push (state diff → minimum intent traffic) ───────────────────────
//
// Only gameplay HUD values reach the game window: HP / SURVIVE timer /
// win-lose modal / hit-flash trigger. No debug/tool state is pushed here.
//
// The HP bar is composed C++-side (filled/empty block strings) so the HTML
// stays pure data-m-* with zero JavaScript: the scene binds view.hud.hpFill /
// view.hud.hpEmpty as text and view.hud.hpLow as a class. Presentation logic
// that used to live in scene JS now lives here, where the state already is.

constexpr int kHpBarWidth = 20;  // total block count in the HP bar

// Repeat a UTF-8 block glyph `count` times into a std::string.
std::string repeatGlyph(const char* glyph, int count)
{
	std::string out;
	out.reserve(static_cast<std::size_t>(count) * std::strlen(glyph));
	for (int i = 0; i < count; ++i) { out += glyph; }
	return out;
}

void pushHudDelta(HelloGameMemory& mem,
                  mitiru::module::FrameIntents* intents)
{
	if (mem.hp != mem.lastHp)
	{
		pushStateInt(intents, "view.hud.hp", mem.hp);
		mem.lastHp = mem.hp;

		// Recompose the block bar from the new HP (maxHp is constant kMaxHp).
		const float pct    = std::clamp(static_cast<float>(mem.hp) / kMaxHp, 0.0f, 1.0f);
		const int   filled = static_cast<int>(std::lround(pct * kHpBarWidth));
		pushStateString(intents, "view.hud.hpFill",  repeatGlyph("█", filled));
		pushStateString(intents, "view.hud.hpEmpty", repeatGlyph("░", kHpBarWidth - filled));
		pushStateBool(intents, "view.hud.hpLow", pct <= 0.35f);
	}
	if (mem.lastMaxHp != kMaxHp)
	{
		pushStateInt(intents, "view.hud.maxHp", kMaxHp);
		mem.lastMaxHp = kMaxHp;
	}
	const int t = static_cast<int>(std::ceil(mem.remaining));
	if (t != mem.lastTimeInt)
	{
		pushStateInt(intents, "view.hud.time", t);
		mem.lastTimeInt = t;
	}
	if (mem.gameOver != mem.lastGameOver)
	{
		pushStateBool(intents, "view.hud.gameOver", mem.gameOver);
		mem.lastGameOver = mem.gameOver;
	}
	if (mem.outcome != mem.lastOutcome)
	{
		pushStateString(intents, "view.hud.outcome", mem.outcome);
		mem.lastOutcome = mem.outcome;
	}
	if (mem.hitCount != mem.lastHitCount)
	{
		pushStateInt(intents, "view.hud.hitCount", mem.hitCount);
		mem.lastHitCount = mem.hitCount;
	}
}

// ── Inspector exports (DLL → host SharedSnapshot, sub-window channel) ─────

void exportGameplayInspectable(HelloGameMemory& mem,
                               mitiru::module::FrameIntents* intents)
{
	nlohmann::json j = {
		{"hp",         mem.hp},
		{"maxHp",      kMaxHp},
		{"remaining",  mem.remaining},
		{"player_x",   mem.player.x},
		{"player_y",   mem.player.y},
		{"gameOver",   mem.gameOver},
		{"outcome",    mem.outcome},
		{"enemyCount", static_cast<int>(mem.enemies.size())},
	};
	mem.scratchJson = j.dump();
	exportInspectable(intents, "gameplay", "Gameplay state", mem.scratchJson);
}

/// Time-travel inspectable: HP + X history series for the inspector sub-window
/// to scrub locally. The game does NOT scrub — it only publishes the raw ring
/// buffer contents. The inspector owns the scrub cursor on its own side.
void exportTimeTravelInspectable(HelloGameMemory& mem,
                                  mitiru::module::FrameIntents* intents)
{
	nlohmann::json hpHistory = nlohmann::json::array();
	nlohmann::json xHistory  = nlohmann::json::array();
	// oldest first (left edge of graph) → newest last (current).
	//
	// Downsample to at most kGraphSamples points so the serialized JSON stays
	// well under the FrameIntents inspectable buffer (3968 B). 300 raw frames
	// of hp+x would serialize to ~4 KB and overflow → truncated/invalid JSON
	// ("DLL produced invalid JSON" in the inspector). 96 samples is plenty for
	// a graph at typical inspector widths.
	constexpr std::size_t kGraphSamples = 96;
	const std::size_t n = mem.history.size();
	if (n > 0)
	{
		const std::size_t count = n < kGraphSamples ? n : kGraphSamples;
		for (std::size_t k = 0; k < count; ++k)
		{
			// Map sample k (0..count-1) → history index, oldest→newest.
			// idxFromOldest spans [0, n-1] evenly across `count` samples.
			const std::size_t idxFromOldest = (count == 1)
				? (n - 1)
				: (k * (n - 1)) / (count - 1);
			const std::size_t idx = (n - 1) - idxFromOldest; // at() is newest-first
			if (const auto* s = mem.history.at(idx))
			{
				hpHistory.push_back(s->hp);
				// 1 decimal keeps the JSON compact (no 6-sigfig float spam).
				xHistory.push_back(std::round(s->playerPos.x * 10.0f) / 10.0f);
			}
		}
	}
	nlohmann::json j = {
		{"capacity",  static_cast<int>(mem.history.size())},
		{"hpMax",     kMaxHp},
		{"hpHistory", hpHistory},
		{"xHistory",  xHistory},
	};
	mem.scratchJson = j.dump();
	exportInspectable(intents, "timetravel", "Time travel", mem.scratchJson);
}

void exportInputInspectable(HelloGameMemory& mem,
                            const mitiru::module::InputSnapshot* input,
                            mitiru::module::FrameIntents* intents)
{
	// Held keys (currently down this frame)
	nlohmann::json heldKeys = nlohmann::json::array();
	for (int vk = 1; vk < 256; ++vk)
	{
		if (input->keysDown[vk])
		{
			heldKeys.push_back("VK_" + std::to_string(vk));
		}
	}

	// Mouse state
	nlohmann::json mouseBtns = nlohmann::json::array();
	if (input->mouseButtonsDown[0]) mouseBtns.push_back("L");
	if (input->mouseButtonsDown[1]) mouseBtns.push_back("R");
	if (input->mouseButtonsDown[2]) mouseBtns.push_back("M");

	// Recent press history (rolling buffer in memory)
	nlohmann::json history = nlohmann::json::array();
	for (const auto& entry : mem.keyHistory)
	{
		history.push_back({{"name", entry.name}, {"t", entry.time}});
	}

	// Per-key press counts (only keys with count > 0 or currently held)
	nlohmann::json stats = nlohmann::json::array();
	for (int vk = 1; vk < 256; ++vk)
	{
		const int cnt = mem.pressCounts[vk];
		const bool held = input->keysDown[vk];
		if (cnt == 0 && !held) { continue; }
		stats.push_back({
			{"name",  "VK_" + std::to_string(vk)},
			{"count", cnt},
			{"held",  held},
		});
	}

	nlohmann::json j = {
		{"held",      heldKeys},
		{"mouseX",    input->mouseX},
		{"mouseY",    input->mouseY},
		{"mouseBtns", mouseBtns},
		{"history",   history},
		{"stats",     stats},
	};
	mem.scratchJson = j.dump();
	exportInspectable(intents, "input", "Input", mem.scratchJson);
}

// ── Action event handling (CEF → DLL) ────────────────────────────────────

void processActionEvents(HelloGameMemory& mem,
                         const mitiru::module::InputSnapshot* input)
{
	for (std::int32_t i = 0; i < input->actionEventCount; ++i)
	{
		const auto& ev = input->actionEvents[i];
		const std::string name{ev.name};
		if (name == "game.restart")
		{
			resetWorld(mem);
			mem.eventLog.emit(mem.frame, "restart", nlohmann::json::object());
		}
	}
}

// ── Input monitor maintenance (press history + counts) ───────────────────

void pollInputDebug(HelloGameMemory& mem,
                    const mitiru::module::InputSnapshot* input)
{
	for (int vk = 1; vk < 256; ++vk)
	{
		if (input->keysJustPressed[vk])
		{
			mem.keyHistory.push_front({"VK_" + std::to_string(vk), mem.totalTime});
			while (mem.keyHistory.size() > 16) { mem.keyHistory.pop_back(); }
			++mem.pressCounts[vk];
		}
	}
}

void captureSnapshot(HelloGameMemory& mem)
{
	Snapshot s;
	s.playerPos = mem.player;
	s.enemies   = mem.enemies;
	s.hp        = mem.hp;
	s.remaining = mem.remaining;
	s.t         = mem.totalTime;
	mem.history.push(std::move(s));
}

// ── Asset hot reload (DLL → CEF JS via intent) ───────────────────────────

void pollAssetHotReload(HelloGameMemory& mem,
                        mitiru::module::FrameIntents* intents)
{
	++mem.assetPollTick;
	if (mem.assetPollTick < 60) { return; }  // ~1s @ 60fps
	mem.assetPollTick = 0;

	// hello_game DLL is installed as `<host>/hello_game/hello_game.dll`,
	// so its assets live in `hello_game/assets/` relative to host cwd.
	std::error_code ec;
	std::filesystem::file_time_type newest{};
	bool any = false;
	for (auto it = std::filesystem::recursive_directory_iterator(
	         "hello_game/assets",
	         std::filesystem::directory_options::skip_permission_denied, ec);
	     !ec && it != std::filesystem::recursive_directory_iterator{};
	     it.increment(ec))
	{
		if (ec) { break; }
		if (!it->is_regular_file(ec)) { continue; }
		const auto ext = it->path().extension().string();
		if (ext != ".html" && ext != ".css" && ext != ".js" && ext != ".json")
		{
			continue;
		}
		const auto t = std::filesystem::last_write_time(it->path(), ec);
		if (ec) { continue; }
		if (!any || t > newest) { newest = t; any = true; }
	}
	if (!any) { return; }

	if (mem.assetMtimeInitialized && newest > mem.lastAssetMtime)
	{
		requestJsExec(intents, "setTimeout(function(){location.reload();}, 80);");
	}
	mem.lastAssetMtime          = newest;
	mem.assetMtimeInitialized   = true;
}

// ── Gameplay update ───────────────────────────────────────────────────

void movePlayer(HelloGameMemory& mem,
                const mitiru::module::InputSnapshot* input, float dt)
{
	// Mouse-native control: the player chases the cursor at kPlayerSpeed.
	// Keyboard movement was removed because the inspector sub-window is
	// mouse-driven — forcing the dev to swap hand position between the game
	// (keys) and the inspector (mouse) during a debug session is friction.
	// Chasing (rather than snapping) preserves the dodge/survival challenge:
	// you steer the player but can't teleport away from enemies.
	// Cold-start guard: before the first real mouse move, the engine reports
	// (0,0). Without this the player would dart to the top-left corner on
	// spawn. Treat a (0,0) reading as "no cursor yet" and hold position until
	// the player actually moves the mouse (the clamp means a genuine corner
	// target is unreachable anyway, so nothing is lost).
	if (input->mouseX <= 0.5f && input->mouseY <= 0.5f)
	{
		if (!mem.trail.empty()) { mem.trail.pop_back(); }
		return;
	}

	const float targetX = input->mouseX;
	const float targetY = input->mouseY;
	float dx = targetX - mem.player.x;
	float dy = targetY - mem.player.y;
	const float dist = std::sqrt(dx * dx + dy * dy);

	const float step = kPlayerSpeed * dt;
	bool moved = false;
	// Deadzone avoids jitter when the cursor sits on top of the player.
	if (dist > 2.0f)
	{
		if (dist <= step)
		{
			mem.player.x = targetX;   // close enough — land exactly on cursor
			mem.player.y = targetY;
		}
		else
		{
			mem.player.x += (dx / dist) * step;
			mem.player.y += (dy / dist) * step;
		}
		moved = true;
	}
	mem.player.x = std::clamp(mem.player.x,
	                          kPlayerSize * 0.5f, mem.screenW - kPlayerSize * 0.5f);
	mem.player.y = std::clamp(mem.player.y,
	                          kPlayerSize * 0.5f, mem.screenH - kPlayerSize * 0.5f);

	// Trail: push past positions while moving; decay when idle.
	if (moved)
	{
		mem.trail.push_front(mem.player);
		while (mem.trail.size() > HelloGameMemory::kTrailMax)
		{
			mem.trail.pop_back();
		}
	}
	else if (!mem.trail.empty())
	{
		mem.trail.pop_back();
	}
}

void tickGameFeelTimers(HelloGameMemory& mem, float dt)
{
	if (mem.hitFlashT > 0.0f)
	{
		mem.hitFlashT = std::max(0.0f, mem.hitFlashT - dt);
	}
	for (auto& enemy : mem.enemies)
	{
		if (enemy.deathFadeT > 0.0f)
		{
			enemy.deathFadeT = std::max(0.0f, enemy.deathFadeT - dt);
		}
	}
}

void applyHit(HelloGameMemory& mem, Enemy& enemy, int enemyIdx)
{
	mem.hp -= kHitDamage;
	enemy.alive     = false;
	enemy.respawnIn = kEnemyRespawn;
	enemy.deathPos   = enemy.pos;  // freeze for fade overlay
	enemy.deathFadeT = 0.30f;
	mem.hitFlashT    = 0.18f;       // ~180ms full-screen red wash
	++mem.hitCount;                 // monotonic counter for HUD pulse

	// Event timeline: the moment of damage (dual-readable substrate).
	mem.eventLog.emit(mem.frame, "hit", {
		{"dmg",       kHitDamage},
		{"hp_after",  mem.hp},
		{"enemy_idx", enemyIdx},
	});
	// The enemy that hit dies on contact — record where it fell.
	mem.eventLog.emit(mem.frame, "enemy_death", {
		{"x", std::round(enemy.deathPos.x * 10.0f) / 10.0f},
		{"y", std::round(enemy.deathPos.y * 10.0f) / 10.0f},
	});

	if (mem.hp <= 0)
	{
		mem.hp       = 0;
		mem.gameOver = true;
		mem.outcome  = "lose";
		mem.eventLog.emit(mem.frame, "game_over", {{"outcome", "lose"}});
	}
}

void moveEnemies(HelloGameMemory& mem, float dt)
{
	std::mt19937                          rng(static_cast<std::uint32_t>(mem.rngSeed++));
	std::uniform_int_distribution<int>    sideDist(0, 3);
	std::uniform_real_distribution<float> rx(0.0f, mem.screenW);
	std::uniform_real_distribution<float> ry(0.0f, mem.screenH);

	int enemyIdx = 0;
	for (auto& enemy : mem.enemies)
	{
		if (!enemy.alive)
		{
			enemy.respawnIn -= dt;
			if (enemy.respawnIn <= 0.0f)
			{
				seedEnemy(enemy, rng, sideDist, rx, ry, mem.screenW, mem.screenH);
			}
			++enemyIdx;
			continue;
		}
		const float dx = mem.player.x - enemy.pos.x;
		const float dy = mem.player.y - enemy.pos.y;
		const float d  = std::sqrt(dx * dx + dy * dy);
		if (d > 0.5f)
		{
			enemy.pos.x += (dx / d) * kEnemySpeed * dt;
			enemy.pos.y += (dy / d) * kEnemySpeed * dt;
		}
		const float hitRadius = (kPlayerSize + kEnemySize) * 0.5f * 0.9f;
		if (d < hitRadius) { applyHit(mem, enemy, enemyIdx); }
		++enemyIdx;
	}
}

void tickTimer(HelloGameMemory& mem, float dt)
{
	mem.remaining -= dt;
	if (mem.remaining <= 0.0f)
	{
		mem.remaining = 0.0f;
		mem.gameOver  = true;
		mem.outcome   = "win";
		mem.eventLog.emit(mem.frame, "game_over", {{"outcome", "win"}});
	}
}

// ── Module callback impls ────────────────────────────────────────────────

void hello_on_init(void* memory)
{
	if (memory == nullptr) { return; }
	auto& mem = *static_cast<HelloGameMemory*>(memory);
	mem.enemies.clear();  // world bootstraps on first update

	// Open the append-only event timeline (fresh per run). PID-scoped so the
	// inspector / an AI agent can find it via EventLog::pathForPid.
	if (!mem.eventLog.isOpen()) { mem.eventLog.open(currentPid()); }

	// Declare invariants once. predicates close over GameMemory (ADR 0005:
	// it is the single source of truth, so checks are deterministic). Declared
	// only if not already present (on_init may run again across hot reloads).
	if (mem.invariants.size() == 0)
	{
		mem.invariants.add(
			"hp_non_negative",
			[&mem] { return mem.hp >= 0; },
			[&mem] { return "hp=" + std::to_string(mem.hp); });
		mem.invariants.add(
			"player_in_bounds",
			[&mem] {
				return mem.player.x >= 0.0f && mem.player.x <= mem.screenW &&
				       mem.player.y >= 0.0f && mem.player.y <= mem.screenH;
			},
			[&mem] {
				return "player=(" + std::to_string(static_cast<int>(mem.player.x)) +
				       "," + std::to_string(static_cast<int>(mem.player.y)) + ")";
			});
		mem.invariants.add(
			"enemy_count_stable",
			[&mem] { return mem.enemies.size() == static_cast<std::size_t>(kEnemyCount); },
			[&mem] { return "count=" + std::to_string(mem.enemies.size()); });
	}
}

void hello_on_update(void* memory, float dt,
                     const mitiru::module::InputSnapshot* input,
                     mitiru::module::FrameIntents* intents)
{
	if (memory == nullptr || input == nullptr || intents == nullptr) { return; }
	auto& mem = *static_cast<HelloGameMemory*>(memory);

	mem.totalTime += dt;  // wall-clock
	++mem.frame;          // time axis for the event timeline + invariant checks

	if (mem.enemies.empty()) { spawnEnemies(mem); }

	// ESC → quit (the only meta-control the game window exposes).
	if (input->keysJustPressed[vk::Escape]) { intents->requestStop = 1; }

	// Debug-only: hold [B] to force an invariant violation (hp < 0) so the
	// red overlay + invariant_violation event can be demonstrated. Released =
	// restored to a sane value. This is a demo affordance, not gameplay.
	const bool breakNow = input->keysDown[vk::B];
	if (breakNow) { mem.hp = -10; }
	else if (mem.forceInvariantBreak && mem.hp < 0) { mem.hp = kMaxHp; }
	mem.forceInvariantBreak = breakNow;

	processActionEvents(mem, input);
	pollInputDebug(mem, input);
	pollAssetHotReload(mem, intents);

	// Check declared invariants every frame against GameMemory. Violations
	// land in the event timeline (machine-readable) and recent() (window).
	mem.invariants.check(mem.frame, mem.eventLog);

	// Gameplay is always live — the game never time-travels itself.
	const float gameplayDt = dt;

	// HUD + inspectable export every 6 frames. HUD reaches the game window;
	// inspectables feed sub-windows (never drawn here).
	if (++mem.pushTick >= 6)
	{
		mem.pushTick = 0;
		pushHudDelta(mem, intents);
		exportGameplayInspectable(mem, intents);
		exportInputInspectable(mem, input, intents);
		exportTimeTravelInspectable(mem, intents);
	}

	if (mem.gameOver) { return; }

	movePlayer(mem, input, gameplayDt);
	moveEnemies(mem, gameplayDt);
	tickGameFeelTimers(mem, gameplayDt);
	tickTimer(mem, gameplayDt);
	captureSnapshot(mem);
}

void hello_on_draw(void* memory, mitiru::Screen* screen)
{
	if (memory == nullptr || screen == nullptr) { return; }
	auto& mem = *static_cast<HelloGameMemory*>(memory);

	mem.screenW = static_cast<float>(screen->width());
	mem.screenH = static_cast<float>(screen->height());

	// Recenter on shape change — never let player be pinned at edge after
	// resize. Threshold avoids per-pixel WM_SIZE churn (which is already
	// deferred at engine level, but be defensive).
	const bool firstFrame = mem.lastScreenW <= 0.0f;
	const bool resized    = !firstFrame &&
		(std::abs(mem.screenW - mem.lastScreenW) > 4.0f ||
		 std::abs(mem.screenH - mem.lastScreenH) > 4.0f);
	if (firstFrame || resized)
	{
		mem.player = {mem.screenW * 0.5f, mem.screenH * 0.5f};
		mem.trail.clear();
		// re-seed enemies at edges of the NEW screen
		spawnEnemies(mem);
		mem.lastScreenW = mem.screenW;
		mem.lastScreenH = mem.screenH;
	}

	// Mitiru Saturn palette — silver gray + sober red + ink.
	// bg is engine-level (cfg.backgroundColor), gameplay rects use the
	// Saturn primary (ink player) + danger (Saturn red enemy) tokens.
	screen->clear(sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f});  // #c8c8c8 (silver)

	// ── Player trail (oldest first → darkest)
	if (!mem.trail.empty())
	{
		const std::size_t n = mem.trail.size();
		for (std::size_t i = 0; i < n; ++i)
		{
			const Vec2& p = mem.trail[i];
			// i=0 が最新。idx 大きいほど古い → alpha 低い
			const float t = 1.0f - static_cast<float>(i + 1) / static_cast<float>(n + 1);
			const float a = t * 0.30f;  // max 30% で控えめに
			const float scale = 0.55f + 0.30f * t;  // 古いほど小さく
			const float sz = kPlayerSize * scale;
			// Trail = player ink black with alpha decay (#101010).
			screen->drawRect(
				sgc::Rectf{p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz},
				sgc::Colorf{0.063f, 0.063f, 0.063f, a});
		}
	}

	// Player = ink black (#101010) — neutral, "operated subject" against silver bg.
	screen->drawRect(
		sgc::Rectf{mem.player.x - kPlayerSize * 0.5f,
		           mem.player.y - kPlayerSize * 0.5f,
		           kPlayerSize, kPlayerSize},
		sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});

	for (const auto& enemy : mem.enemies)
	{
		if (enemy.alive)
		{
			// Enemy = Saturn red (#c8002c) — single identity accent = danger.
			screen->drawRect(
				sgc::Rectf{enemy.pos.x - kEnemySize * 0.5f,
				           enemy.pos.y - kEnemySize * 0.5f,
				           kEnemySize, kEnemySize},
				sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f});
		}
		else if (enemy.deathFadeT > 0.0f)
		{
			// 死亡 fade — 0.30s で 1.0 → 0 alpha + 1.0 → 1.8 scale (爆発感)
			const float t = enemy.deathFadeT / 0.30f;  // 1 → 0
			const float a = t * 0.7f;
			const float sz = kEnemySize * (1.0f + (1.0f - t) * 0.8f);
			// Death fade = Saturn red with alpha decay.
			screen->drawRect(
				sgc::Rectf{enemy.deathPos.x - sz * 0.5f,
				           enemy.deathPos.y - sz * 0.5f,
				           sz, sz},
				sgc::Colorf{0.784f, 0.0f, 0.173f, a});
		}
	}

	// ── Hit flash full-screen overlay
	if (mem.hitFlashT > 0.0f)
	{
		const float a = std::min(0.32f, mem.hitFlashT * 1.8f);
		// Hit flash = Saturn red wash overlay.
		screen->drawRect(
			sgc::Rectf{0.0f, 0.0f, mem.screenW, mem.screenH},
			sgc::Colorf{0.784f, 0.0f, 0.173f, a});
	}

	// ── Invariant violation overlay (debug, EXCEPTION to pure-game rule) ──
	// Only drawn when an invariant is actually broken — it occupies no screen
	// space when the game is healthy (必要な時しか出ない). A thin top band in
	// Saturn red names the first broken invariant so the dev sees it without
	// opening the inspector. The same violation is in the event timeline.
	if (!mem.invariants.recent().empty())
	{
		const float bandH = 30.0f;
		// Saturn red band across the top edge.
		screen->drawRect(
			sgc::Rectf{0.0f, 0.0f, mem.screenW, bandH},
			sgc::Colorf{0.784f, 0.0f, 0.173f, 0.92f});

		const auto& v = mem.invariants.recent().front();
		std::string label = "INVARIANT VIOLATED: " + v.name;
		if (!v.detail.empty()) { label += "  (" + v.detail + ")"; }
		if (mem.invariants.recent().size() > 1)
		{
			label += "  +" + std::to_string(mem.invariants.recent().size() - 1);
		}
		// White-on-red text — readable against the band.
		screen->drawTextInRect(
			sgc::Rectf{10.0f, 5.0f, mem.screenW - 20.0f, bandH - 8.0f},
			label.c_str(),
			sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
			16.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);
	}
}

void hello_on_shutdown(void* memory)
{
	if (memory == nullptr) { return; }
	auto& mem = *static_cast<HelloGameMemory*>(memory);
	(void)mem;
}

}  // namespace hello_game

// ── DLL exports ──────────────────────────────────────────────────────────

extern "C"
{

#if defined(_WIN32)
#define HELLO_DLL_EXPORT __declspec(dllexport)
#else
#define HELLO_DLL_EXPORT __attribute__((visibility("default")))
#endif

HELLO_DLL_EXPORT
void mitiru_module_load(mitiru::module::ModuleApi* api, void** memory)
{
	if (api == nullptr || memory == nullptr) { return; }

	// First-time load: allocate persistent state. On reload, host hands us
	// the same pointer so gameplay state survives the code swap (ADR 0005).
	if (*memory == nullptr)
	{
		*memory = new hello_game::HelloGameMemory{};
	}

	api->version     = mitiru::module::kCurrentApiVersion;
	api->on_init     = &hello_game::hello_on_init;
	api->on_update   = &hello_game::hello_on_update;
	api->on_draw     = &hello_game::hello_on_draw;
	api->on_shutdown = &hello_game::hello_on_shutdown;
}

HELLO_DLL_EXPORT
void mitiru_module_unload(void* /*memory*/)
{
}

}  // extern "C"
