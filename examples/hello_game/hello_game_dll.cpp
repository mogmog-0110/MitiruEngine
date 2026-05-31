// hello_game — Game-as-DLL 移植 (ADR 0005、純粋なサバイバルゲーム)
//
// ADR 0005 (Host-Game C-only signal flow) の参照実装。ゲーム窓は「純粋なゲーム」
// で、HP / SURVIVE タイマー / 勝敗モーダル / リスタートのみ。debug・tool 系
// (タイムトラベル / record-replay / pause / time scale / snapshot / palette /
// screenshot) はゲーム窓から排除済み — inspector sub-window / CLI の責務だから。
//
// 機能:
//   gameplay:  移動 / 敵 / HP / 30 秒生存 / 勝敗
//   終了:      ESC                       → hud.quit()
//   リスタート: CEF ボタン (game.restart) → in.action()
//   HUD push:  view.hud.*                → hud.set()
//   game feel: trail / 被弾フラッシュ / 敵死亡フェード / 低 HP パルス
//   asset hot reload: hello_game/assets/ の mtime を polling → hud.runJs()
//   inspector export: gameplay + input + timetravel → hud.watch()
//                     (sub-window 専用チャネル。ゲーム窓には描画しない)
//
// history ring buffer は毎フレーム記録し続け、inspector sub-window が観測する
// invisible な substrate になる。ゲーム本体は scrub しない。

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

#include <mitiru/module/Game.hpp>
#include <mitiru/observe/EventLog.hpp>
#include <mitiru/observe/Invariant.hpp>
#include <mitiru/observe/TimeTravelRecorder.hpp>
#include <mitiru/observe/TimeTravelMarkers.hpp>

#if defined(_WIN32)
#include <process.h>  // _getpid (EventLog::open 用)
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

// ── gameplay 定数 ───────────────────────────────────────────────────────────

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
constexpr std::size_t kHistoryCap = 300;  // 5 秒 @ 60fps

// ── データ構造 ─────────────────────────────────────────────────────────────

struct Vec2 { float x; float y; };

struct Enemy
{
	Vec2  pos        {0.0f, 0.0f};
	bool  alive      {true};
	float respawnIn  {0.0f};
	Vec2  deathPos   {0.0f, 0.0f};  // 死亡 fade 描画用に最後の位置を凍結
	float deathFadeT {0.0f};        // 0.30 → 0 で fade out
};

/// 毎フレームの replay 可能な snapshot — 60fps で約 5 秒分の履歴に収まる軽さ。
/// inspector sub-window が観測する substrate として継続記録される。
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

/// 永続 state は全てここに置く。reload を跨いで host が pointer を保持する。
struct HelloGameMemory
{
	// gameplay
	Vec2               player    {kDefaultScreenW * 0.5f, kDefaultScreenH * 0.5f};
	std::vector<Enemy> enemies;
	int                hp        {kMaxHp};
	float              remaining {kSurviveTime};
	bool               gameOver  {false};
	std::string        outcome;
	std::uint32_t      rngSeed   {1u};

	// ── game feel ─────────────────────────────────────────────────────
	// player trail — 直近 N 位置、alpha を減衰させて描画。
	std::deque<Vec2>   trail;
	static constexpr std::size_t kTrailMax = 10;
	// 被弾フラッシュ: 被弾で立ち減衰。drawRect alpha 演出 + HUD flash 連動。
	float              hitFlashT {0.0f};
	int                hitCount  {0};  // 単調増加カウンタ → HUD push (1-shot anim trigger)
	int                lastHitCount {-1};

	// resize 追跡 — 窓の形状が大きく変わったら player を中央へ戻し敵を再配置
	// (demo polish: player が端に貼り付くのを防ぐ)
	float lastScreenW {0.0f};
	float lastScreenH {0.0f};

	// 画面サイズ (初回 draw で確定)
	float screenW {kDefaultScreenW};
	float screenH {kDefaultScreenH};

	// 起動からの実時間累計 (inspector export 用の単調 timestamp)
	float totalTime {0.0f};

	// フレームカウンタ — event timeline (EventLog) の時間軸であり、
	// invariant の deterministic check point でもある (ADR 0005)。
	std::uint32_t frame {0};

	// ── dual-readable な debug substrate (game 窓には不可視) ──────────────
	// EventLog: %TEMP%\mitiru_events_<pid>.jsonl への append-only JSONL。疎な
	// gameplay マイルストン (hit / death / game_over / restart / invariant
	// 違反) がここに着地。inspector と AI agent の両方が読める。
	mitiru::observe::EventLog    eventLog;
	// Invariant set: on_init で 1 度宣言、update で毎フレーム check。
	// 違反は eventLog (機械可読) と recent() (window/inspector) へ。
	mitiru::observe::InvariantSet invariants;
	// debug トグル: invariant-break キー押しっぱなしで hp を負に強制し、赤
	// overlay + 違反 event を demo できる。離せば元に戻す。
	bool                          forceInvariantBreak {false};

	// ── inspector substrate (game 窓には不可視) ────────────────────────
	// 毎フレーム記録する history ring buffer。game は scrub しない;
	// inspector sub-window が timetravel export 経由で観測する。記録し続ける
	// こと自体が time-travel inspector (5 軸 #2) を支える。
	mitiru::observe::TimeTravelRecorder<Snapshot> history{kHistoryCap};

	// input monitor (input inspectable 用)
	std::deque<KeyEvent>     keyHistory;
	std::array<int, 256>     pressCounts{};

	// HUD push のスロットリング
	int pushTick {0};

	// HUD diff キャッシュ — 変化のない値の再 push を避ける
	int  lastHp         {-1};
	int  lastMaxHp      {-1};
	int  lastTimeInt    {-1};
	bool lastGameOver   {false};
	std::string lastOutcome;

	// asset hot reload (CEF)
	std::filesystem::file_time_type lastAssetMtime{};
	bool                            assetMtimeInitialized {false};
	int                             assetPollTick {0};

	// scratch buffer (毎フレームの heap traffic を避けるため使い回す)
	std::string scratchJson;

	// エンジンが呼ぶ入口 (実装はヘルパ定義の後)。
	void init();
	void update(mitiru::Input in, mitiru::Hud hud, float dt);
	void draw(mitiru::Screen& screen);
};

// ── world セットアップ ──────────────────────────────────────────────────

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
	// "last seen" を現 state の逆にして HUD 再 push を強制し、pushHudDelta() に
	// diff を検出させ新値を emit させる。現値と同じにすると push が黙って skip
	// される (bug: restart モーダルが消えない)。
	mem.lastHp        = -1;
	mem.lastMaxHp     = -1;
	mem.lastTimeInt   = -1;
	mem.lastGameOver  = true;             // 現値 (false) との diff を強制
	mem.lastOutcome   = "__stale__";      // 現値 ("") との diff を強制
	mem.hitCount      = 0;
	mem.lastHitCount  = -1;
}

// ── HUD push (state diff → 最小 intent traffic) ───────────────────────────
//
// game 窓に届くのは gameplay HUD 値のみ: HP / SURVIVE タイマー / 勝敗モーダル /
// hit-flash トリガー。debug/tool state はここで push しない。
//
// HP bar は C++ 側で組む (filled/empty ブロック文字列) ので HTML は JavaScript
// ゼロの純 data-m-* のまま: scene は view.hud.hpFill / view.hud.hpEmpty を text、
// view.hud.hpLow を class として bind する。かつて scene JS にあった表示ロジック
// を、state が既にあるこちら側へ移した。

constexpr int kHpBarWidth = 20;  // HP bar の総ブロック数

// UTF-8 ブロック glyph を `count` 回繰り返して std::string にする。
std::string repeatGlyph(const char* glyph, int count)
{
	std::string out;
	out.reserve(static_cast<std::size_t>(count) * std::strlen(glyph));
	for (int i = 0; i < count; ++i) { out += glyph; }
	return out;
}

void pushHudDelta(HelloGameMemory& mem, mitiru::Hud hud)
{
	if (mem.hp != mem.lastHp)
	{
		hud.set("view.hud.hp", mem.hp);
		mem.lastHp = mem.hp;

		// 新 HP からブロック bar を再構成 (maxHp は定数 kMaxHp)。
		const float pct    = std::clamp(static_cast<float>(mem.hp) / kMaxHp, 0.0f, 1.0f);
		const int   filled = static_cast<int>(std::lround(pct * kHpBarWidth));
		hud.set("view.hud.hpFill",  repeatGlyph("█", filled).c_str());
		hud.set("view.hud.hpEmpty", repeatGlyph("░", kHpBarWidth - filled).c_str());
		hud.set("view.hud.hpLow", pct <= 0.35f);
	}
	if (mem.lastMaxHp != kMaxHp)
	{
		hud.set("view.hud.maxHp", kMaxHp);
		mem.lastMaxHp = kMaxHp;
	}
	const int t = static_cast<int>(std::ceil(mem.remaining));
	if (t != mem.lastTimeInt)
	{
		hud.set("view.hud.time", t);
		mem.lastTimeInt = t;
	}
	if (mem.gameOver != mem.lastGameOver)
	{
		hud.set("view.hud.gameOver", mem.gameOver);
		mem.lastGameOver = mem.gameOver;
	}
	if (mem.outcome != mem.lastOutcome)
	{
		hud.set("view.hud.outcome", mem.outcome.c_str());
		mem.lastOutcome = mem.outcome;
	}
	if (mem.hitCount != mem.lastHitCount)
	{
		// 本物の被弾 (-1 からの reset エッジではない) は hit SE 再生 (ADR 0008)。
		if (mem.hitCount > mem.lastHitCount && mem.lastHitCount >= 0)
		{
			hud.play("hit");
		}
		hud.set("view.hud.hitCount", mem.hitCount);
		mem.lastHitCount = mem.hitCount;
	}
}

// ── inspector export (DLL → host SharedSnapshot、sub-window チャネル) ──────

void exportGameplayInspectable(HelloGameMemory& mem, mitiru::Hud hud)
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
	hud.watch("gameplay", "Gameplay state", mem.scratchJson.c_str());
}

/// time-travel inspectable: inspector sub-window がローカルに scrub するための
/// HP + X 履歴系列。game は scrub しない — raw な ring buffer 内容を publish する
/// だけ。scrub カーソルは inspector が自分側で保持する。
void exportTimeTravelInspectable(HelloGameMemory& mem, mitiru::Hud hud)
{
	nlohmann::json hpHistory = nlohmann::json::array();
	nlohmann::json xHistory  = nlohmann::json::array();
	std::vector<double> hpSeries;  // marker 抽出用 — hpHistory と同一 index 空間
	// 古い順 (graph 左端) から新しい順 (末尾=現在) へ。serialize 後の JSON が
	// FrameIntents inspectable buffer (3968 B) に収まるよう最大 kGraphSamples 点へ
	// ダウンサンプルする (300 raw frame 分は大きすぎる)。inspector 幅には 96 で十分。
	constexpr std::size_t kGraphSamples = 96;
	const std::size_t n = mem.history.size();
	if (n > 0)
	{
		const std::size_t count = n < kGraphSamples ? n : kGraphSamples;
		hpSeries.reserve(count);
		for (std::size_t k = 0; k < count; ++k)
		{
			// サンプル k (0..count-1) を history index (古い順) へマップ。
			// idxFromOldest は `count` 個のサンプルで [0, n-1] を均等に張る。
			const std::size_t idxFromOldest = (count == 1)
				? (n - 1)
				: (k * (n - 1)) / (count - 1);
			const std::size_t idx = (n - 1) - idxFromOldest; // at() は新しい順
			if (const auto* s = mem.history.at(idx))
			{
				hpHistory.push_back(s->hp);
				// 小数 1 桁で JSON をコンパクトに (6 桁 float の spam を防ぐ)。
				xHistory.push_back(std::round(s->playerPos.x * 10.0f) / 10.0f);
				hpSeries.push_back(static_cast<double>(s->hp));
			}
		}
	}

	// HP 系列から「節目」を抽出: 値変化 (被弾 / 回復) と danger 閾値跨ぎ。
	// downsample 済み hpHistory と同じ系列で計算するので marker の offsetFromNewest が
	// そのまま graph の bar index に対応する (full ring との index ズレを構造で排除)。
	nlohmann::json markers = nlohmann::json::array();
	if (hpSeries.size() >= 2)
	{
		mitiru::observe::TimeTravelRecorder<double> markerRing(hpSeries.size());
		for (const double v : hpSeries) { markerRing.push(v); }
		mitiru::observe::MarkerOpts opts;
		opts.wantEdges    = true;
		opts.epsilon      = 0.5;  // 整数 HP: 1 以上の変化だけ edge に
		opts.hasThreshold = true;
		opts.threshold    = static_cast<double>(kMaxHp) * 0.35;  // danger ライン
		opts.maxMarkers   = 24;
		const auto ms = mitiru::observe::extractMarkers(
			markerRing, [](double v) { return v; }, opts);
		for (const auto& m : ms)
		{
			markers.push_back({
				{"o", m.offsetFromNewest},
				{"v", m.value},
				{"k", static_cast<int>(m.kind)},
			});
		}
	}

	nlohmann::json j = {
		{"capacity",  static_cast<int>(mem.history.size())},
		{"hpMax",     kMaxHp},
		{"hpHistory", hpHistory},
		{"xHistory",  xHistory},
		{"markers",   markers},
	};
	mem.scratchJson = j.dump();
	hud.watch("timetravel", "Time travel", mem.scratchJson.c_str());
}

void exportInputInspectable(HelloGameMemory& mem, mitiru::Input in, mitiru::Hud hud)
{
	// 押下中のキー (このフレームで down)
	nlohmann::json heldKeys = nlohmann::json::array();
	for (int vk = 1; vk < 256; ++vk)
	{
		if (in.raw()->keysDown[vk])
		{
			heldKeys.push_back("VK_" + std::to_string(vk));
		}
	}

	// マウス state
	nlohmann::json mouseBtns = nlohmann::json::array();
	if (in.raw()->mouseButtonsDown[0]) mouseBtns.push_back("L");
	if (in.raw()->mouseButtonsDown[1]) mouseBtns.push_back("R");
	if (in.raw()->mouseButtonsDown[2]) mouseBtns.push_back("M");

	// 直近の press 履歴 (memory 上の rolling buffer)
	nlohmann::json history = nlohmann::json::array();
	for (const auto& entry : mem.keyHistory)
	{
		history.push_back({{"name", entry.name}, {"t", entry.time}});
	}

	// キー別 press 回数 (count > 0 か押下中のキーのみ)
	nlohmann::json stats = nlohmann::json::array();
	for (int vk = 1; vk < 256; ++vk)
	{
		const int cnt = mem.pressCounts[vk];
		const bool held = in.raw()->keysDown[vk];
		if (cnt == 0 && !held) { continue; }
		stats.push_back({
			{"name",  "VK_" + std::to_string(vk)},
			{"count", cnt},
			{"held",  held},
		});
	}

	nlohmann::json j = {
		{"held",      heldKeys},
		{"mouseX",    in.mouseX()},
		{"mouseY",    in.mouseY()},
		{"mouseBtns", mouseBtns},
		{"history",   history},
		{"stats",     stats},
	};
	mem.scratchJson = j.dump();
	hud.watch("input", "Input", mem.scratchJson.c_str());
}

// ── input monitor の維持 (press 履歴 + 回数) ──────────────────────────────

void pollInputDebug(HelloGameMemory& mem, mitiru::Input in)
{
	for (int vk = 1; vk < 256; ++vk)
	{
		if (in.raw()->keysJustPressed[vk])
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

// ── asset hot reload (DLL → intent 経由で CEF JS) ─────────────────────────

void pollAssetHotReload(HelloGameMemory& mem, mitiru::Hud hud)
{
	++mem.assetPollTick;
	if (mem.assetPollTick < 60) { return; }  // 約 1 秒 @ 60fps
	mem.assetPollTick = 0;

	// hello_game DLL は `<host>/hello_game/hello_game.dll` として配置されるので、
	// asset は host cwd 相対の `hello_game/assets/` にある。
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
		hud.runJs("setTimeout(function(){location.reload();}, 80);");
	}
	mem.lastAssetMtime          = newest;
	mem.assetMtimeInitialized   = true;
}

// ── gameplay update ─────────────────────────────────────────────────────

void movePlayer(HelloGameMemory& mem, mitiru::Input in, float dt)
{
	// マウス native 操作: player は kPlayerSpeed でカーソルを追う。inspector
	// sub-window がマウス駆動なので、debug 中に game (キー) と inspector (マウス)
	// で手を持ち替える摩擦を避けるべくキーボード移動は廃止した。
	// snap でなく追従にすることで回避/生存の難易度を保つ: player を操舵できるが
	// 敵から瞬間移動はできない。
	// cold-start guard: 最初の実マウス移動前は engine が (0,0) を報告する。これが
	// 無いと spawn 時に player が左上隅へ突進する。(0,0) は「まだカーソル無し」と
	// 扱い、実際にマウスを動かすまで位置を保持 (clamp により真の隅 target は
	// どのみち到達不能なので失うものは無い)。
	if (in.mouseX() <= 0.5f && in.mouseY() <= 0.5f)
	{
		if (!mem.trail.empty()) { mem.trail.pop_back(); }
		return;
	}

	const float targetX = in.mouseX();
	const float targetY = in.mouseY();
	float dx = targetX - mem.player.x;
	float dy = targetY - mem.player.y;
	const float dist = std::sqrt(dx * dx + dy * dy);

	const float step = kPlayerSpeed * dt;
	bool moved = false;
	// deadzone はカーソルが player 真上に乗ったときの jitter を防ぐ。
	if (dist > 2.0f)
	{
		if (dist <= step)
		{
			mem.player.x = targetX;   // 十分近い — カーソルにぴったり着地
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

	// trail: 移動中は過去位置を push、停止中は減衰させる。
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
	enemy.deathPos   = enemy.pos;  // fade overlay 用に凍結
	enemy.deathFadeT = 0.30f;
	mem.hitFlashT    = 0.18f;       // 約 180ms の全画面赤 wash
	++mem.hitCount;                 // HUD パルス用の単調カウンタ

	// event timeline: 被弾の瞬間 (dual-readable な substrate)。
	mem.eventLog.emit(mem.frame, "hit", {
		{"dmg",       kHitDamage},
		{"hp_after",  mem.hp},
		{"enemy_idx", enemyIdx},
	});
	// 当たった敵は接触で死ぬ — 倒れた位置を記録する。
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

// ── game ロジック実装 (free helper) ───────────────────────────────────────

void initGame(HelloGameMemory& mem)
{
	mem.enemies.clear();  // world は初回 update で立ち上がる

	// append-only な event timeline を open (run ごとに新規)。PID スコープなので
	// inspector / AI agent が EventLog::pathForPid 経由で見つけられる。
	if (!mem.eventLog.isOpen()) { mem.eventLog.open(currentPid()); }

	// invariant を 1 度だけ宣言。predicate は GameMemory を閉じ込める (ADR 0005:
	// それが single source of truth なので check は deterministic)。未登録の時だけ
	// 宣言する (on_init は hot reload を跨いで再実行されうる)。
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

void stepGame(HelloGameMemory& mem, mitiru::Input in, mitiru::Hud hud, float dt)
{
	mem.totalTime += dt;  // 実時間
	++mem.frame;          // event timeline + invariant check の時間軸

	if (mem.enemies.empty()) { spawnEnemies(mem); }

	// ESC で終了 (game 窓が公開する唯一の meta-control)。
	if (in.pressed(mitiru::Key::Escape)) { hud.quit(); }

	// debug 専用: [B] 押しっぱなしで invariant 違反 (hp < 0) を強制し、赤 overlay
	// + invariant_violation event を demo できる。離せば正常値へ復帰。これは demo
	// 用の仕掛けであり gameplay ではない。
	const bool breakNow = in.down(mitiru::Key::B);
	if (breakNow) { mem.hp = -10; }
	else if (mem.forceInvariantBreak && mem.hp < 0) { mem.hp = kMaxHp; }
	mem.forceInvariantBreak = breakNow;

	// リスタート (CEF ボタン game.restart)。
	if (in.action("game.restart"))
	{
		resetWorld(mem);
		mem.eventLog.emit(mem.frame, "restart", nlohmann::json::object());
	}
	pollInputDebug(mem, in);
	pollAssetHotReload(mem, hud);

	// 宣言済み invariant を毎フレーム GameMemory に対して check。違反は event
	// timeline (機械可読) と recent() (window) に着地する。
	mem.invariants.check(mem.frame, mem.eventLog);

	// gameplay は常に live — game は自身を time-travel させない。
	const float gameplayDt = dt;

	// HUD + inspectable を 6 フレームごとに export。HUD は game 窓へ届く;
	// inspectable は sub-window へ供給される (ここでは描画しない)。
	if (++mem.pushTick >= 6)
	{
		mem.pushTick = 0;
		pushHudDelta(mem, hud);
		exportGameplayInspectable(mem, hud);
		exportInputInspectable(mem, in, hud);
		exportTimeTravelInspectable(mem, hud);
	}

	if (mem.gameOver) { return; }

	movePlayer(mem, in, gameplayDt);
	moveEnemies(mem, gameplayDt);
	tickGameFeelTimers(mem, gameplayDt);
	tickTimer(mem, gameplayDt);
	captureSnapshot(mem);
}

void drawGame(HelloGameMemory& mem, mitiru::Screen& screen)
{
	mem.screenW = static_cast<float>(screen.width());
	mem.screenH = static_cast<float>(screen.height());

	// 形状変化で中央へ戻す — resize 後に player が端へ貼り付くのを防ぐ。閾値で
	// per-pixel な WM_SIZE churn を避ける (engine 側で既に deferred だが防御的に)。
	const bool firstFrame = mem.lastScreenW <= 0.0f;
	const bool resized    = !firstFrame &&
		(std::abs(mem.screenW - mem.lastScreenW) > 4.0f ||
		 std::abs(mem.screenH - mem.lastScreenH) > 4.0f);
	if (firstFrame || resized)
	{
		mem.player = {mem.screenW * 0.5f, mem.screenH * 0.5f};
		mem.trail.clear();
		// 新しい画面の端に敵を再配置
		spawnEnemies(mem);
		mem.lastScreenW = mem.screenW;
		mem.lastScreenH = mem.screenH;
	}

	// Mitiru Saturn palette — silver gray + sober red + ink。
	// bg は engine-level (cfg.backgroundColor)、gameplay rect は Saturn primary
	// (ink player) + danger (Saturn red enemy) token を使う。
	screen.clear(sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f});  // #c8c8c8 (silver)

	// ── player trail (古い順 = 最も暗い)
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
			// trail = player の ink black に alpha 減衰 (#101010)。
			screen.drawRect(
				sgc::Rectf{p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz},
				sgc::Colorf{0.063f, 0.063f, 0.063f, a});
		}
	}

	// player = ink black (#101010) — silver bg に対し中立な「操作対象」。
	screen.drawRect(
		sgc::Rectf{mem.player.x - kPlayerSize * 0.5f,
		           mem.player.y - kPlayerSize * 0.5f,
		           kPlayerSize, kPlayerSize},
		sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});

	for (const auto& enemy : mem.enemies)
	{
		if (enemy.alive)
		{
			// enemy = Saturn red (#c8002c) — 唯一の identity accent = danger。
			screen.drawRect(
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
			// 死亡 fade = Saturn red に alpha 減衰。
			screen.drawRect(
				sgc::Rectf{enemy.deathPos.x - sz * 0.5f,
				           enemy.deathPos.y - sz * 0.5f,
				           sz, sz},
				sgc::Colorf{0.784f, 0.0f, 0.173f, a});
		}
	}

	// ── 被弾フラッシュの全画面 overlay
	if (mem.hitFlashT > 0.0f)
	{
		const float a = std::min(0.32f, mem.hitFlashT * 1.8f);
		// 被弾フラッシュ = Saturn red の wash overlay。
		screen.drawRect(
			sgc::Rectf{0.0f, 0.0f, mem.screenW, mem.screenH},
			sgc::Colorf{0.784f, 0.0f, 0.173f, a});
	}

	// ── invariant 違反 overlay (debug、pure-game ルールの例外) ──
	// invariant が実際に壊れた時だけ描画 — game が健全な間は画面を占めない
	// (必要な時しか出ない)。Saturn red の細い上部 band が最初に壊れた invariant
	// 名を出すので、dev は inspector を開かずとも気付ける。同じ違反は event
	// timeline にもある。
	if (!mem.invariants.recent().empty())
	{
		const float bandH = 30.0f;
		// 上端に Saturn red の band。
		screen.drawRect(
			sgc::Rectf{0.0f, 0.0f, mem.screenW, bandH},
			sgc::Colorf{0.784f, 0.0f, 0.173f, 0.92f});

		const auto& v = mem.invariants.recent().front();
		std::string label = "INVARIANT VIOLATED: " + v.name;
		if (!v.detail.empty()) { label += "  (" + v.detail + ")"; }
		if (mem.invariants.recent().size() > 1)
		{
			label += "  +" + std::to_string(mem.invariants.recent().size() - 1);
		}
		// 赤地に白文字 — band に対して読める。
		screen.drawTextInRect(
			sgc::Rectf{10.0f, 5.0f, mem.screenW - 20.0f, bandH - 8.0f},
			label.c_str(),
			sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
			16.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);
	}
}

// ── 入口メソッドの実装 (ヘルパ定義の後) ─────────────────────────────────────
void HelloGameMemory::init() { initGame(*this); }
void HelloGameMemory::update(mitiru::Input in, mitiru::Hud hud, float dt) { stepGame(*this, in, hud, dt); }
void HelloGameMemory::draw(mitiru::Screen& screen) { drawGame(*this, screen); }

}  // namespace hello_game

// これ 1 行で DLL の入口が出来る。HelloGameMemory は std::vector/deque/string/EventLog を
// 持つ非 flat POD なので registerGame は is_trivially_copyable で memorySize 申告を自動 skip し、
// host は memorySize=0 として観測 view.* JSON を記録する (ADR 0013)。
MITIRU_GAME(hello_game::HelloGameMemory)
