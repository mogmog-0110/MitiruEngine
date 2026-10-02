// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/observe/NumberAppend.hpp>
#include <mitiru/observe/Oracle.hpp>
#include <mitiru/observe/Reflect.hpp>
#include <mitiru/observe/ReflectDiff.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/Spawner.hpp>  // module::findSpawnOrigin (★1-1)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mitiru::module::detail
{
/// @brief 分岐エディタ「残す」直後に裏で回す決定論ゲート 1 件分の結果 (ADR 0035 O5)。
struct ReplayGateEntry
{
	std::string file;    ///< .mtrr のファイル名 (パスなし)
	bool        pass{};  ///< PASS/FAIL
	std::string reason;  ///< FAIL 理由 (--json verdict の "reason"。PASS なら空)
};

/// @brief ゲートの進行状態。commit を返した直後は running、別スレッドの完了後に pass/fail へ移る
/// (★1-9)。gate ディレクトリが無い / dllPath 未設定なら実行自体が起きないので idle のまま。
enum class ReplayGateStatus { Idle, Running, Pass, Fail };

/// @brief ゲートの共有状態。commit を返す HTTP スレッドと、ゲートを回す別スレッドの両方から
/// 触るので mutex で守る (thread_local だと別スレッドの結果が見えなかった)。
struct ReplayGateState
{
	std::mutex                mutex;
	ReplayGateStatus           status = ReplayGateStatus::Idle;
	std::vector<ReplayGateEntry> results;
	bool                       rerunRequested = false;  ///< 実行中に来た commit の分。終了後にもう 1 周する
};

inline ReplayGateState& replayGateState() noexcept
{
	static ReplayGateState state;
	return state;
}

/// @brief ゲートを Running にする。すでに実行中なら「終わったらもう 1 周」を予約して false。
/// commit 応答を返す前に呼ぶ (スレッド起動後に Running へ変えると、その間の GET /api/ai/state が前回結果を今回の結果として見せる)。
inline bool claimReplayGate() noexcept
{
	auto& state = replayGateState();
	std::lock_guard<std::mutex> lock(state.mutex);
	if (state.status == ReplayGateStatus::Running) { state.rerunRequested = true; return false; }
	state.status = ReplayGateStatus::Running;
	state.results.clear();
	state.rerunRequested = false;
	return true;
}

/// @brief 直近の commit で走らせたゲート結果。scene_view.html が緑/赤一覧に使う。
/// @details 値渡し (mutex を握っている間だけコピーする) にして呼び出し側の生存期間から独立させる。
inline std::vector<ReplayGateEntry> lastReplayGateResults()
{
	auto& state = replayGateState();
	std::lock_guard<std::mutex> lock(state.mutex);
	return state.results;
}

/// @brief 分岐中 (PUT はしたが commit/discard していない) フィールドの一覧 (4-3)。
/// @details `PUT /api/ai/state` が成功するたびに置き換え、`commit`/`discard` で空にする。
struct BranchState
{
	std::mutex               mutex;
	bool                     active = false;
	std::vector<std::string> fields;  ///< dotted 名。commit された順ではなく PUT body の列挙順
	/// @brief 3-5: この分岐が始まった frame 番号。非 active 中は無意味 (直近の値が残る)。
	std::uint64_t            branchedAtFrame = 0;
};

inline BranchState& branchState() noexcept
{
	static BranchState state;
	return state;
}
}  // namespace mitiru::module::detail

namespace mitiru::detail
{
/// @brief `docs/TODO_ENGINE_2026_09_16.md` O5 / ★1-9: 「残す」の後に決定論ゲート
/// (`tests/replay_golden/*.mtrr`、`MITIRU_REPLAY_GATE_DIR` で上書き可) を子プロセスで順に回す。
/// `Engine::addCommitListener` から別スレッドで呼ばれる前提で、進行状況を
/// `module::detail::replayGateState()` へ都度書き込む (running のあと 1 件ずつ結果を積み、
/// 最後に pass/fail を確定する)。子プロセスは自分自身 (`mitiru_host.exe`) を
/// `--headless --replay-test <f> --json` で再帰的に spawn する (`InspectorLauncher.hpp` の
/// GetModuleFileNameW と同じ自己パス解決)。gate ディレクトリが無ければ何もしない (未設定は
/// エラーではない)。実行中に次の commit が来た場合は二重起動を握りつぶし、running のまま
/// 待たせる。Windows 専用 (`_WIN32` 外は即 idle)。
inline void runReplayGateOnce(const std::filesystem::path& dllPath) noexcept
{
	namespace fs = std::filesystem;
	auto& state = module::detail::replayGateState();
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		state.status = module::detail::ReplayGateStatus::Running;
		state.results.clear();
	}

	const auto finish = [&state](module::detail::ReplayGateStatus s) {
		std::lock_guard<std::mutex> lock(state.mutex);
		state.status = s;
	};

	if (dllPath.empty()) { finish(module::detail::ReplayGateStatus::Idle); return; }
#ifdef _WIN32
	wchar_t exeBuf[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH) == 0)
	{ finish(module::detail::ReplayGateStatus::Idle); return; }
	const fs::path exePath{exeBuf};

	const char* envDir = std::getenv("MITIRU_REPLAY_GATE_DIR");
	const fs::path gateDir = (envDir != nullptr && *envDir != '\0')
		? fs::path{envDir} : fs::path{"tests/replay_golden"};

	std::error_code ec;
	if (!fs::exists(gateDir, ec) || !fs::is_directory(gateDir, ec))
	{ finish(module::detail::ReplayGateStatus::Idle); return; }

	wchar_t tmpDirBuf[MAX_PATH]{};
	::GetTempPathW(MAX_PATH, tmpDirBuf);

	for (const auto& entry : fs::directory_iterator(gateDir, ec))
	{
		if (ec) { break; }
		if (!entry.is_regular_file() || entry.path().extension() != ".mtrr") { continue; }

		wchar_t tmpFile[MAX_PATH]{};
		::GetTempFileNameW(tmpDirBuf, L"mrg", 0, tmpFile);

		std::wstring cmd = L"\"" + exePath.wstring() + L"\" \"" + dllPath.wstring()
			+ L"\" --headless --replay-test \"" + entry.path().wstring()
			+ L"\" --json > \"" + tmpFile + L"\" 2>&1";
		// cmd.exe の古い罠: コマンドが `"` で始まると `cmd /c` が先頭/末尾の引用符を勝手に
		// 剥がそうとして誤解釈する (`system()`/`_wsystem()` 共通の既知動作)。全体をもう 1 段
		// 引用符で包むのが標準の回避策 (中の `"..."` は保たれる)。
		const std::wstring wrapped = L"\"" + cmd + L"\"";
		const int rc = _wsystem(wrapped.c_str());
		(void)rc;

		std::string verdictLine;
		{
			std::ifstream in(tmpFile);
			std::string line;
			while (std::getline(in, line))
			{
				if (line.find("\"verdict\"") != std::string::npos) { verdictLine = line; }
			}
		}
		::DeleteFileW(tmpFile);

		module::detail::ReplayGateEntry out{};
		out.file = entry.path().filename().string();
		out.pass = false;
		out.reason = "no_verdict";
		if (!verdictLine.empty())
		{
			try
			{
				const auto v = nlohmann::json::parse(verdictLine);
				out.pass   = v.value("verdict", std::string{}) == "PASS";
				out.reason = out.pass ? std::string{} : v.value("reason", std::string{});
			}
			catch (...) { /* 壊れた行はそのまま no_verdict/FAIL 扱い */ }
		}
		{
			std::lock_guard<std::mutex> lock(state.mutex);
			state.results.push_back(std::move(out));
		}
	}

	{
		std::lock_guard<std::mutex> lock(state.mutex);
		const bool allPass = std::all_of(state.results.begin(), state.results.end(),
			[](const module::detail::ReplayGateEntry& e) { return e.pass; });
		state.status = state.results.empty() ? module::detail::ReplayGateStatus::Idle
			: (allPass ? module::detail::ReplayGateStatus::Pass : module::detail::ReplayGateStatus::Fail);
	}
#else
	(void)dllPath;
	finish(module::detail::ReplayGateStatus::Idle);
#endif
}

/// @brief `claimReplayGate()` 済みの状態で呼ぶ本体。実行中に予約された分 (rerunRequested) があれば
/// 最後の commit の状態に対してもう 1 周し、ゲート結果が常に最新の確定状態を指すようにする。
inline void runReplayGateAsync(std::filesystem::path dllPath) noexcept
{
	auto& state = module::detail::replayGateState();
	for (;;)
	{
		runReplayGateOnce(dllPath);
		std::lock_guard<std::mutex> lock(state.mutex);
		if (!state.rerunRequested) { return; }
		state.rerunRequested = false;
		state.status = module::detail::ReplayGateStatus::Running;  // 同じロック内で戻し、次の周が始まるまで確定済みの結果として見せない
		state.results.clear();
	}
}

/// @brief ★1-9: `GET /api/ai/state` に添える決定論ゲートの現在状態。running/pass/fail の
/// 3 値 + 個別の PASS/FAIL 一覧 (`runs`)。まだ 1 度も commit していなければ idle。
inline nlohmann::json replayGateStateJson()
{
	auto& state = module::detail::replayGateState();
	std::lock_guard<std::mutex> lock(state.mutex);
	nlohmann::json out;
	switch (state.status)
	{
		case module::detail::ReplayGateStatus::Running: out["status"] = "running"; break;
		case module::detail::ReplayGateStatus::Pass:    out["status"] = "pass";    break;
		case module::detail::ReplayGateStatus::Fail:    out["status"] = "fail";    break;
		case module::detail::ReplayGateStatus::Idle:
		default: out["status"] = "idle"; break;
	}
	nlohmann::json runs = nlohmann::json::array();
	for (const auto& e : state.results)
	{ runs.push_back(nlohmann::json{{"file", e.file}, {"pass", e.pass}, {"reason", e.reason}}); }
	out["runs"] = std::move(runs);
	return out;
}

/// @brief 4-3: `GET /api/ai/state` に添える「分岐中」の印。`PUT /api/ai/state` で試した
/// フィールドが commit/discard されるまで active のまま残る (scene_view / why_view の橙バー用)。
inline nlohmann::json branchStateJson()
{
	auto& state = module::detail::branchState();
	std::lock_guard<std::mutex> lock(state.mutex);
	nlohmann::json out;
	out["active"] = state.active;
	nlohmann::json fields = nlohmann::json::array();
	for (const auto& f : state.fields) { fields.push_back(f); }
	out["fields"] = std::move(fields);
	out["branchedAtFrame"] = state.branchedAtFrame;  // 3-5: mitiru why / candidates と同じ座標系 (frame 番号)
	return out;
}

/// @brief O4 分岐候補 (`POST /api/ai/candidates`) の 1 slot 分の応答を組み立てる。
/// @details 3-5: どの frame から分岐した、どの上書きの結果かを添える。C++ 側の state は
///          増えない (呼び出し側が既に持っている値を並べるだけ) ので、ここは純粋な JSON 組立。
inline nlohmann::json candidateEntryJson(std::size_t slot, std::uint64_t baseFrame,
                                          const nlohmann::json& overrides, int stepsRun,
                                          const std::string& stateJson)
{
	nlohmann::json entry;
	entry["slot"] = slot;
	entry["baseFrame"] = baseFrame;
	entry["fields"] = overrides.is_object() ? overrides : nlohmann::json::object();
	entry["stepsRun"] = stepsRun;
	try { entry["state"] = nlohmann::json::parse(stateJson); }
	catch (...) { entry["state"] = nlohmann::json::object(); }
	return entry;
}

/// @brief P10「1 フレームの解剖図」: InputSnapshot 生値を JSON へ (名前解決・軸のラベル付けは
/// frame_view.html 側の責務。C++ 側は signal-only で生値だけ渡す)。
inline nlohmann::json frameAnatomyInputJson(const mitiru::module::InputSnapshot& s)
{
	nlohmann::json keysDown = nlohmann::json::array();
	nlohmann::json justPressed = nlohmann::json::array();
	for (int vk = 0; vk < 256; ++vk)
	{
		if (s.keysDown[vk] != 0) { keysDown.push_back(vk); }
		if (s.keysJustPressed[vk] != 0) { justPressed.push_back(vk); }
	}
	nlohmann::json out;
	out["keysDown"] = std::move(keysDown);
	out["keysJustPressed"] = std::move(justPressed);
	out["mouseX"] = s.mouseX;
	out["mouseY"] = s.mouseY;
	out["mouseButtonsDown"] = {s.mouseButtonsDown[0] != 0, s.mouseButtonsDown[1] != 0, s.mouseButtonsDown[2] != 0,
	                           s.mouseXButtonsDown[0] != 0, s.mouseXButtonsDown[1] != 0};
	out["mouseWheel"] = {s.mouseWheel, s.mouseWheelH};
	nlohmann::json pads = nlohmann::json::array();
	for (const auto& p : s.gamepads)
	{
		pads.push_back(p.connected ? nlohmann::json{{"buttonsDown", p.buttonsDown}} : nlohmann::json(nullptr));
	}
	out["gamepads"] = std::move(pads);
	out["gamepadConnected"] = s.gamepadConnected != 0;
	out["gamepadButtonsDown"] = s.gamepadButtonsDown;
	out["gamepadAxes"] = {s.gamepadAxes[0], s.gamepadAxes[1], s.gamepadAxes[2],
	                       s.gamepadAxes[3], s.gamepadAxes[4], s.gamepadAxes[5]};
	out["textInput"] = std::string(s.textInput, s.textInputLen);
	out["effectiveDt"] = s.effectiveDt;
	out["paused"] = s.paused != 0;
	return out;
}
}  // namespace mitiru::detail

// ── HTTP server bridge のクラス外定義 ───────────────────────────

MITIRU_INLINE void mitiru::Engine::initHttpServer(int port, Game& game)
{
	m_httpServer = std::make_unique<server::EngineHttpServer>();

	server::EngineBridgeContext ctx;
	ctx.getFrameNumber = [this]() -> std::uint64_t { return frameNumber(); };
	ctx.getClock       = [this]() -> const Clock* { return clock(); };
	ctx.getScreen      = [this]() -> const Screen* { return screen(); };
	ctx.capture        = [this]() -> std::vector<std::uint8_t> { return capture(); };
	ctx.getSnapshot    = [this]() -> std::string { return snapshot(); };
	ctx.requestStop    = [this]() { requestStop(); };
	ctx.gameFlags      = &m_gameFlags;
	ctx.config         = &m_config;

	server::EngineCallbacks cb;
	server::initEngineHttpCallbacks(cb, ctx, game);

	// ★1-9: 決定論ゲートを commit listener として登録する。cb.aiCommit は確定 (bytes への
	// 書き込み) だけを済ませてすぐ応答を返し、ゲート自体は別スレッドへ渡す。
	// m_moduleHost->sourcePath() は値渡しでスレッドへ渡す (Engine 寿命と切り離すため)。
	addCommitListener([this](const char* const*, int) {
		if (!m_moduleHost) { return; }
		if (!module::detail::claimReplayGate()) { return; }  // 実行中: 終了後にもう 1 周が予約される
		std::thread(mitiru::detail::runReplayGateAsync, m_moduleHost->sourcePath()).detach();
	});

	// runtime コントロール: `mitiru_console` / 外部ツールから叩く。
	cb.runtimeTogglePause   = [this]() -> bool { setPauseKind(EngineConfig::kPauseKindDebug); togglePaused(); return isPaused(); };
	cb.runtimeIsPaused      = [this]() -> bool { return isPaused(); };
	cb.runtimeStep          = [this]() { stepOneFrame(); };
	cb.runtimeSetTimeScale  = [this](float s) { setTimeScale(s); };
	cb.runtimeGetTimeScale  = [this]() -> float { return timeScale(); };
	cb.runtimeResim = [this](std::uint32_t k) { return resimFromFramesAgo(k); };
	cb.runtimeToggleLofi    = [this]() -> bool { toggleLofi(); return isLofiEnabled(); };
	cb.runtimeIsLofiEnabled = [this]() -> bool { return isLofiEnabled(); };

	// AI Lens: reflected GameMemory を read / diff / what-if する AI 向け面。
	// game に MITIRU_REFLECT が無ければ reflectFieldCount==0 で "{}"/"[]" を返す。
	// C5: state/at/diff の 3 経路とも DOM 変換 (nlohmann) を素通りせず、直接文字列化
	// (ReflectJsonStringCache) または DOM 変換の dirty-hash キャッシュ (ReflectJsonCache) を
	// 経由する。GameMemory バイト列が前フレームと同一なら再変換自体を省略する。
	auto aiStateCache   = std::make_shared<observe::ReflectJsonStringCache>();
	auto aiStateAtCache = std::make_shared<observe::ReflectJsonStringCache>();
	auto aiDiffCacheA   = std::make_shared<observe::ReflectJsonCache>();
	auto aiDiffCacheB   = std::make_shared<observe::ReflectJsonCache>();

	cb.aiState = [this, aiStateCache]() -> std::string {
		std::string body = (m_moduleMemory == nullptr || m_moduleMemorySize == 0 ||
			m_moduleReflection.fieldCount() <= 0)
			? "{}"
			: aiStateCache->get(static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
				m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
				m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
		// oracleRingFor は engineKey (Engine* を void* にした値) 単位の registry なので、this をそのまま渡す。
		const std::string oracle = observe::oracleEventsJson(this);
		body.insert(body.size() - 1, (body.size() == 2 ? "" : ",") + std::string("\"oracle\":") + oracle);
		// ★1-9: 決定論ゲートの running/pass/fail、4-3: 分岐中フィールドの印。
		// この時点で body は必ず "{...}" 形式 (oracle を足した直後) なので comma は常に要る。
		body.insert(body.size() - 1, ",\"replayGate\":" + mitiru::detail::replayGateStateJson().dump());
		body.insert(body.size() - 1, ",\"branch\":" + mitiru::detail::branchStateJson().dump());
		return body;
	};
	cb.aiStateAt = [this, aiStateAtCache](int off) -> std::string {
		if (m_moduleReflection.fieldCount() <= 0 || off < 0) { return "{}"; }
		const std::uint8_t* p = m_moduleMemoryRing.at(static_cast<std::size_t>(off));
		if (p == nullptr) { return "{}"; }
		return aiStateAtCache->get(p, m_moduleMemorySize, m_moduleReflection.fieldsData(),
			m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
	};
	cb.aiStateDiff = [this, aiDiffCacheA, aiDiffCacheB](int from, int to) -> std::string {
		MITIRU_ZONE_NAMED("Engine::aiStateDiff");
		if (m_moduleReflection.fieldCount() <= 0) { return "[]"; }
		const std::uint8_t* a = m_moduleMemoryRing.at(static_cast<std::size_t>(from < 0 ? 0 : from));
		const std::uint8_t* b = m_moduleMemoryRing.at(static_cast<std::size_t>(to < 0 ? 0 : to));
		if (a == nullptr || b == nullptr) { return "[]"; }
		const auto& ja = aiDiffCacheA->get(a, m_moduleMemorySize, m_moduleReflection.fieldsData(),
			m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
		const auto& jb = aiDiffCacheB->get(b, m_moduleMemorySize, m_moduleReflection.fieldsData(),
			m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
		return observe::reflectDiff(ja, jb).dump();
	};
	cb.aiRingSize = [this]() -> int { return static_cast<int>(m_moduleMemoryRing.size()); };
	cb.aiBranch = [this](const std::string& keysCsv, int frames) -> std::string {
		MITIRU_ZONE_NAMED("Engine::aiBranch");
		if (m_moduleReflection.fieldCount() <= 0 || frames <= 0) { return "{}"; }
		const auto vkOf = [](std::string_view n) -> int {
			if (n == "Left")  { return 0x25; } if (n == "Up")    { return 0x26; }
			if (n == "Right") { return 0x27; } if (n == "Down")  { return 0x28; }
			if (n == "Space") { return 0x20; } if (n == "Enter") { return 0x0D; }
			if (n.size() == 1) {
				char c = n[0];
				if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 32); }
				if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { return static_cast<int>(c); }
			}
			return -1;
		};
		// C7: stringstream/getline (トークン毎に string 確保) の代わりに、固定長配列 +
		// string_view で走査する (aiBranch は AI polling から高頻度に叩かれうる)。
		// 同時押し上限は MITIRU_FOR_EACH の keysDown 一括表現に合わせて 32。
		constexpr std::size_t     kMaxKeys = 32;
		std::array<int, kMaxKeys> vks{};
		std::size_t               vkCount = 0;
		const std::string_view    csv(keysCsv);
		for (std::size_t pos = 0; pos <= csv.size();)
		{
			const std::size_t comma = csv.find(',', pos);
			const std::size_t tokEnd = (comma == std::string_view::npos) ? csv.size() : comma;
			const std::string_view tok = csv.substr(pos, tokEnd - pos);
			const std::size_t a = tok.find_first_not_of(" \t");
			if (a != std::string_view::npos)
			{
				const std::size_t b = tok.find_last_not_of(" \t");
				const int vk = vkOf(tok.substr(a, b - a + 1));
				if (vk >= 0 && vkCount < kMaxKeys) { vks[vkCount++] = vk; }
			}
			if (comma == std::string_view::npos) { break; }
			pos = comma + 1;
		}
		std::vector<module::InputSnapshot> seq(static_cast<std::size_t>(frames));
		for (std::size_t i = 0; i < seq.size(); ++i)
		{
			std::memset(&seq[i], 0, sizeof(module::InputSnapshot));
			seq[i].rngSeed = m_config.randomSeed;
			// 台本合成の契約 (v21): effectiveDt は合成側が明示する (0 = 進まない)。
			// 論理解像度も live と同じ値を供給する (layout 依存ロジックの what-if 一致)。
			seq[i].effectiveDt = Engine::kFixedDt;
			if (m_moduleInputSnapshot)
			{
				seq[i].logicalW = m_moduleInputSnapshot->logicalW;
				seq[i].logicalH = m_moduleInputSnapshot->logicalH;
			}
			for (std::size_t k = 0; k < vkCount; ++k)
			{
				const int vk = vks[k];
				if (vk >= 0 && vk < 256) { seq[i].keysDown[vk] = 1; if (i == 0) { seq[i].keysJustPressed[vk] = 1; } }
			}
		}
		return branchModuleMemory(seq.data(), frames);
	};

	// inspector からの書き戻し (3-3): live へ直接書かず、aiBranch と同じ「複製 → 書き換え →
	// reflectToJson → live は復元」の分岐にする。決定論と rewind ring をおかしくしない。
	cb.aiStatePut = [this](const std::string& fieldsJson, int& statusOut) -> std::string {
		MITIRU_ZONE_NAMED("Engine::aiStatePut");
		statusOut = 200;
		if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleReflection.fieldCount() <= 0)
		{ statusOut = 503; return R"({"error":"reflection not wired"})"; }

		nlohmann::json req;
		try { req = nlohmann::json::parse(fieldsJson); }
		catch (...) { statusOut = 400; return R"({"error":"invalid JSON body"})"; }
		if (!req.is_object()) { statusOut = 400; return R"({"error":"body must be a JSON object"})"; }

		auto* bytes = static_cast<std::uint8_t*>(m_moduleMemory);
		// C8: GameMemory 全体 (数百 KB〜数 MB) をコピーせず、書き換える各 field の
		// byte span だけを退避する。reflectFieldByteSpan は reflectWriteField と同じ
		// 経路解決を read-only で行う (見つからない/型不一致な path は書き込み側で弾かれる)。
		struct SavedSpan { std::uint32_t offset; std::vector<std::uint8_t> data; };
		std::vector<SavedSpan> saved;
		saved.reserve(req.size());
		for (auto it = req.begin(); it != req.end(); ++it)
		{
			std::uint32_t off = 0, sz = 0;
			if (observe::reflectFieldByteSpan(m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
				m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(), m_moduleMemorySize,
				it.key(), off, sz))
			{
				saved.push_back(SavedSpan{off, std::vector<std::uint8_t>(bytes + off, bytes + off + sz)});
			}
		}
		const auto restore = [&]() {
			for (const auto& s : saved) { std::memcpy(bytes + s.offset, s.data.data(), s.data.size()); }
		};

		for (auto it = req.begin(); it != req.end(); ++it)
		{
			std::string err;
			if (!observe::reflectWriteField(bytes, m_moduleMemorySize, m_moduleReflection.fieldsData(),
				m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(),
				it.key(), it.value(), err))
			{
				restore();  // 途中まで書いた分も戻す
				statusOut = 400;
				return "{\"error\":\"" + observe::jsonEscape(err) + "\"}";
			}
		}

		const nlohmann::json state = observe::reflectToJson(bytes, m_moduleMemorySize,
			m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
			m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
		restore();  // 分岐操作なので live には残さない

		// 4-3: この PUT で試したフィールドを「分岐中」として記録する。commit/discard で消える
		// までの間、scene_view / why_view の橙バーがこの一覧を見て「今どれが正史でないか」を出す。
		{
			auto& branch = module::detail::branchState();
			std::lock_guard<std::mutex> lock(branch.mutex);
			// 3-5: 分岐の起点 frame は「非 active → active」の遷移でだけ確定する。同じ分岐に
			// 対して PUT を重ねても (橙バーの中身を変えても) 起点は動かさない。
			if (!branch.active) { branch.branchedAtFrame = frameNumber(); }
			branch.active = true;
			branch.fields.clear();
			for (auto it = req.begin(); it != req.end(); ++it) { branch.fields.push_back(it.key()); }
		}

		return state.dump();
	};

	// ADR 0035「残す」: aiStatePut と同じ書き換え経路だが restore() を呼ばない。
	// 書いた値がそのまま live GameMemory に残るので、以後の draw/録画はこの状態を正史として進む。
	cb.aiCommit = [this](const std::string& fieldsJson, int& statusOut) -> std::string {
		MITIRU_ZONE_NAMED("Engine::aiCommit");
		statusOut = 200;
		// 候補は commit 前の状態から分岐したものなので、確定した時点で全部意味を失う
		for (std::size_t slot = 0; slot < kMaxCandidateBranches; ++slot) { clearCandidateBranch(slot); }
		if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleReflection.fieldCount() <= 0)
		{ statusOut = 503; return R"({"error":"reflection not wired"})"; }

		nlohmann::json req;
		try { req = nlohmann::json::parse(fieldsJson); }
		catch (...) { statusOut = 400; return R"({"error":"invalid JSON body"})"; }
		if (!req.is_object()) { statusOut = 400; return R"({"error":"body must be a JSON object"})"; }

		auto* bytes = static_cast<std::uint8_t*>(m_moduleMemory);
		for (auto it = req.begin(); it != req.end(); ++it)
		{
			std::string err;
			if (!observe::reflectWriteField(bytes, m_moduleMemorySize, m_moduleReflection.fieldsData(),
				m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(),
				it.key(), it.value(), err))
			{
				// 途中まで書いた分は残す (commit は部分成功もありうる操作。aiStatePut の
				// 全か無かとは異なり、確定用途なので書けた分から先に進める)。
				statusOut = 400;
				return "{\"error\":\"" + observe::jsonEscape(err) + "\"}";
			}
		}

		nlohmann::json state = observe::reflectToJson(bytes, m_moduleMemorySize,
			m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
			m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());

		// 進行データを外から書き換えたので、場面を DLL 内に持つ game (ADR 0040) は組み立て直す。
		if (m_moduleApi.on_rebuild != nullptr)
		{
			try
			{
				guardModuleCallback("on_rebuild", [&] {
					m_moduleApi.on_rebuild(m_moduleMemory, module::kModuleRebuildRestore);
				});
			}
			catch (...) { debug::warnOnce("rebuild.threw", "on_rebuild が例外を投げました (場面の組み立て直しに失敗)"); }
		}

		// ★1-1: commit した field が属する struct に `SpawnOrigin` が埋まっていれば (game 側が
		// spawnFromJson で spawn した object)、応答へ添える。「残す」がどの配置ファイルの何番目へ
		// 書き戻せばよいかを、host に専用配線を足さず reflect 経由でそのまま読めるようにする
		// (実際に JSON へ書くのは game 側の writeBackFieldsToJson opt-in のまま、host は届けるだけ)。
		for (auto it = req.begin(); it != req.end(); ++it)
		{
			if (const auto* origin = module::findSpawnOrigin(state, it.key()); origin != nullptr)
			{
				state["origin"] = *origin;
				break;
			}
		}

		// 4-3: 確定したので「分岐中」の印を消す。
		{
			auto& branch = module::detail::branchState();
			std::lock_guard<std::mutex> lock(branch.mutex);
			branch.active = false;
			branch.fields.clear();
		}

		// ★1-9: O5 の決定論ゲートは commit listener (このファイル上部の addCommitListener) へ
		// 移した。ここでは fields を渡して起動するだけで、ゲート自体の完了は待たない。
		// 応答には "pending" を返し、実際の running/pass/fail は GET /api/ai/state 側で見る。
		std::vector<std::string> fieldNames;
		fieldNames.reserve(req.size());
		for (auto it = req.begin(); it != req.end(); ++it) { fieldNames.push_back(it.key()); }
		std::vector<const char*> fieldPtrs;
		fieldPtrs.reserve(fieldNames.size());
		for (const auto& f : fieldNames) { fieldPtrs.push_back(f.c_str()); }
		dispatchCommit(fieldPtrs.data(), static_cast<int>(fieldPtrs.size()));
		state["gate"] = "pending";

		return state.dump();
	};

	// ADR 0035「捨てる」: 何もしない。live GameMemory は最初から未変更なので、現在値をそのまま返す。
	cb.aiDiscard = [this]() -> std::string {
		// 4-3: 「捨てる」は分岐の終端なので、分岐中フィールドの一覧を空にして印を消す。候補ゴーストも同じ。
		for (std::size_t slot = 0; slot < kMaxCandidateBranches; ++slot) { clearCandidateBranch(slot); }
		{
			auto& branch = module::detail::branchState();
			std::lock_guard<std::mutex> lock(branch.mutex);
			branch.active = false;
			branch.fields.clear();
		}
		if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleReflection.fieldCount() <= 0)
		{ return "{}"; }
		return observe::reflectToJson(static_cast<const std::uint8_t*>(m_moduleMemory), m_moduleMemorySize,
			m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
			m_moduleReflection.schemasData(), m_moduleReflection.schemaCount()).dump();
	};

	// O6 なぜビュー (/api/ai/why?field=<dotted path>): そのフィールドを最後に書いた
	// phase (game 側の mitiru_why_blame_at opt-in、queryModuleWriteBlame 経由) + ring から
	// 8 フレーム分の値の推移。opt-in 未対応 game は blameSupported=false になる (ブリーフの
	// 契約どおり、フレームと値の推移だけ返す)。
	cb.aiTypes = [this]() -> std::string {
		if (m_spawnerTypesJson.empty())
		{
			return R"json({"supported":false,"types":[],"hint":"game に MITIRU_SPAWNER_TYPES_EXPORT() がありません (型が無いのではなく未対応)"})json";
		}
		return m_spawnerTypesJson;
	};

	cb.aiWhy = [this](const std::string& fieldPath) -> std::string {
		MITIRU_ZONE_NAMED("Engine::aiWhy");
		if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleReflection.fieldCount() <= 0)
		{ return R"({"error":"reflection not wired"})"; }

		std::uint32_t off = 0, sz = 0;
		if (!observe::reflectFieldByteSpan(m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
			m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(), m_moduleMemorySize, fieldPath, off, sz))
		{ return "{\"error\":\"unknown field: " + observe::jsonEscape(fieldPath) + "\"}"; }

		// reflectToJson は field 名でネストする (dotted path "outer.inner" → state["outer"]["inner"])。
		const auto pick = [&fieldPath](const nlohmann::json& state) -> nlohmann::json {
			const nlohmann::json* cur = &state;
			std::size_t pos = 0;
			while (true)
			{
				const auto dot = fieldPath.find('.', pos);
				const auto key = fieldPath.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
				if (!cur->is_object() || !cur->contains(key)) { return nullptr; }
				cur = &(*cur)[key];
				if (dot == std::string::npos) { break; }
				pos = dot + 1;
			}
			return *cur;
		};

		nlohmann::json history = nlohmann::json::array();
		constexpr std::size_t kHistoryFrames = 8;
		const std::size_t ringN = std::min<std::size_t>(kHistoryFrames, m_moduleMemoryRing.size());
		for (std::size_t back = 0; back < ringN; ++back)
		{
			const std::uint8_t* p = m_moduleMemoryRing.at(back);
			if (p == nullptr) { break; }
			const nlohmann::json state = observe::reflectToJson(p, m_moduleMemorySize,
				m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount(),
				m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
			nlohmann::json entry;
			entry["framesAgo"] = back;
			entry["value"] = pick(state);
			history.push_back(std::move(entry));
		}

		const char* blame = queryModuleWriteBlame(off);
		const char* everWrote = queryModuleEverWrote(off);
		nlohmann::json everWrittenBy = nlohmann::json::array();
		if (everWrote != nullptr)
		{
			// game 側はカンマ区切りの静的文字列を返す (mitiru_why_everwrote_at)。
			std::string cur;
			for (const char* p = everWrote; ; ++p)
			{
				if (*p == ',' || *p == '\0')
				{
					if (!cur.empty()) { everWrittenBy.push_back(cur); }
					cur.clear();
					if (*p == '\0') { break; }
					continue;
				}
				cur.push_back(*p);
			}
		}
		nlohmann::json out;
		out["field"] = fieldPath;
		out["frame"] = frameNumber();
		out["blameSupported"] = (blame != nullptr);
		out["blame"] = (blame != nullptr) ? nlohmann::json(blame) : nlohmann::json(nullptr);
		out["everWrittenBy"] = std::move(everWrittenBy);  // 「なぜ変わらないのか」— 書きうる phase の集合 (4-4)
		out["history"] = std::move(history);
		return out.dump();
	};

	// O4 分岐候補 (/api/ai/candidates): 複数案 ({"overrides": {...}}) を同じ入力列で並走させ、
	// それぞれの K フレーム後の state を返す。ghost 描画 (drawCandidateBranches) はここでは
	// 呼ばない (host の描画ループから毎フレーム呼ぶもの、HTTP ハンドラの責務ではない)。
	cb.aiCandidates = [this](const std::string& bodyJson) -> std::string {
		MITIRU_ZONE_NAMED("Engine::aiCandidates");
		if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleReflection.fieldCount() <= 0)
		{ return R"({"error":"reflection not wired"})"; }

		nlohmann::json req;
		try { req = nlohmann::json::parse(bodyJson); }
		catch (...) { return R"({"error":"invalid JSON body"})"; }
		if (!req.is_object() || !req.contains("variants") || !req["variants"].is_array())
		{ return R"({"error":"body must be a JSON object with a \"variants\" array"})"; }

		int frames = req.value("frames", 30);
		if (frames < 1)   { frames = 1; }
		if (frames > 600) { frames = 600; }  // aiBranch と同じ上限 (10 秒 @60fps)

		// aiBranch と同じ keys→InputSnapshot 変換 (同じ入力で K フレーム進める、ADR 0035)。
		const auto vkOf = [](std::string_view n) -> int {
			if (n == "Left")  { return 0x25; } if (n == "Up")    { return 0x26; }
			if (n == "Right") { return 0x27; } if (n == "Down")  { return 0x28; }
			if (n == "Space") { return 0x20; } if (n == "Enter") { return 0x0D; }
			if (n.size() == 1) {
				char c = n[0];
				if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 32); }
				if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { return static_cast<int>(c); }
			}
			return -1;
		};
		const std::string keysCsv = req.value("keys", std::string{});
		constexpr std::size_t     kMaxKeys = 32;
		std::array<int, kMaxKeys> vks{};
		std::size_t               vkCount = 0;
		const std::string_view    csv(keysCsv);
		for (std::size_t pos = 0; pos <= csv.size();)
		{
			const std::size_t comma = csv.find(',', pos);
			const std::size_t tokEnd = (comma == std::string_view::npos) ? csv.size() : comma;
			const std::string_view tok = csv.substr(pos, tokEnd - pos);
			const std::size_t a = tok.find_first_not_of(" \t");
			if (a != std::string_view::npos)
			{
				const std::size_t b = tok.find_last_not_of(" \t");
				const int vk = vkOf(tok.substr(a, b - a + 1));
				if (vk >= 0 && vkCount < kMaxKeys) { vks[vkCount++] = vk; }
			}
			if (comma == std::string_view::npos) { break; }
			pos = comma + 1;
		}
		std::vector<module::InputSnapshot> seq(static_cast<std::size_t>(frames));
		for (std::size_t i = 0; i < seq.size(); ++i)
		{
			std::memset(&seq[i], 0, sizeof(module::InputSnapshot));
			seq[i].rngSeed = m_config.randomSeed;
			seq[i].effectiveDt = Engine::kFixedDt;
			if (m_moduleInputSnapshot)
			{
				seq[i].logicalW = m_moduleInputSnapshot->logicalW;
				seq[i].logicalH = m_moduleInputSnapshot->logicalH;
			}
			for (std::size_t k = 0; k < vkCount; ++k)
			{
				const int vk = vks[k];
				if (vk >= 0 && vk < 256) { seq[i].keysDown[vk] = 1; if (i == 0) { seq[i].keysJustPressed[vk] = 1; } }
			}
		}

		// 3-5: 「どのフレームから分岐したか」を応答に持たせる。GameMemory は 1 個しかないので
		// 全 slot が同じ基準フレームから分岐する (Engine の state は増えない、既にある frameNumber()
		// を返すだけ)。
		const std::uint64_t baseFrame = frameNumber();
		const auto& variants = req["variants"];
		const std::size_t n = std::min<std::size_t>(variants.size(), kMaxCandidateBranches);
		nlohmann::json results = nlohmann::json::array();
		for (std::size_t slot = 0; slot < n; ++slot)
		{
			const auto& v = variants[slot];
			const bool hasOverrides = v.is_object() && v.contains("overrides") && v["overrides"].is_object();
			const std::string overridesJson = hasOverrides ? v["overrides"].dump() : std::string{};
			const std::string stateJson = stepCandidateBranch(slot, overridesJson, seq.data(), frames);
			results.push_back(mitiru::detail::candidateEntryJson(slot, baseFrame,
				hasOverrides ? v["overrides"] : nlohmann::json::object(), frames, stateJson));
		}
		// 前回より少ない件数で呼ばれたとき、余った slot のゴーストが前回の案のまま残らないようにする
		for (std::size_t slot = n; slot < kMaxCandidateBranches; ++slot) { clearCandidateBranch(slot); }
		// 4 件を超える variants は知らせないまま切り捨てたりせず、上限を明示する (candidates.slot.limit)。
		if (variants.size() > kMaxCandidateBranches)
		{
			debug::warnOnceFix("candidates.slot.limit",
				"POST /api/ai/candidates: variants が " + std::to_string(variants.size())
					+ " 件送られたが上限 " + std::to_string(kMaxCandidateBranches) + " を超えた分は無視した。",
				"分岐候補ゴーストは slot 0.." + std::to_string(kMaxCandidateBranches - 1) + " の固定数しか持たない。",
				"4 案以上を試すときは複数回に分けて呼ぶ (前の案は次呼び出しで上書きされる)。");
		}

		nlohmann::json out;
		out["frames"] = frames;
		out["results"] = std::move(results);
		return out.dump();
	};

	// screenshot の実寸 (論理 Screen ≠ window のゲームで stride ズレを防ぐ)。
	cb.captureDims = [this]() -> std::pair<int, int> {
		return {captureWidth(), captureHeight()};
	};

	// AI 音観測 (/api/ai/audio): 適用済み SoundIntent の固定リングを JSON で返す。
	cb.audioLogJson = [this](int max) -> std::string {
		MITIRU_ZONE_NAMED("Engine::audioLogJson");
		const std::size_t m = max > 0 ? static_cast<std::size_t>(max) : 64;
		std::string out;
		out.reserve(32);
		out += "{\"total\":";
		observe::appendNumber(out, m_audioLog.totalCount());
		out += ",\"events\":";
		out += m_audioLog.toJson(m);
		out += "}";
		return out;
	};

	// P10「1 フレームの解剖図」(/api/frame/anatomy?frame=N、ADR 0035 応用): 入力 → 書かれた
	// フィールド (blame 付き) → 描画コマンド → 音、を 1 レスポンスにまとめる。why (O6) の
	// diff+blame 経路、drawLog (/api/ai/frame)、audioLog (/api/ai/audio) の組み合わせで新しい
	// 状態は持たない。N=0 が直近フレーム、N が InputRing/GameMemoryRing の保持数を超えたら
	// input/writes はその分だけ欠ける (ring 範囲外)。
	cb.frameAnatomy = [this](int framesAgo) -> std::string {
		MITIRU_ZONE_NAMED("Engine::frameAnatomy");
		if (framesAgo < 0) { framesAgo = 0; }
		const auto back = static_cast<std::size_t>(framesAgo);

		nlohmann::json out;
		const bool inRing = back < m_moduleMemoryRing.size() && frameNumber() >= static_cast<std::uint64_t>(framesAgo);
		if (inRing) { out["frame"] = frameNumber() - static_cast<std::uint64_t>(framesAgo); }
		else        { out["frame"] = nullptr; out["outOfRange"] = true; }  // 現在フレームで代用すると別のフレームの解剖図に見える
		out["framesAgo"] = framesAgo;
		out["ringSize"] = static_cast<int>(m_moduleMemoryRing.size());

		// (1) 入力
		if (const std::uint8_t* raw = m_moduleInputRing.at(back))
		{ out["input"] = detail::frameAnatomyInputJson(*reinterpret_cast<const module::InputSnapshot*>(raw)); }
		else { out["input"] = nullptr; }

		// (2)(3) update の分岐 (blame) + 書かれたフィールド (diff): このフレームの 1 つ前 →
		// このフレームの reflectDiff に、変化した field ごとの blame を添える (aiWhy と同じ経路)。
		nlohmann::json writes = nlohmann::json::array();
		if (m_moduleReflection.fieldCount() > 0)
		{
			const std::uint8_t* prev = m_moduleMemoryRing.at(back + 1);
			const std::uint8_t* cur  = m_moduleMemoryRing.at(back);
			if (prev != nullptr && cur != nullptr)
			{
				const auto jPrev = observe::reflectToJson(prev, m_moduleMemorySize, m_moduleReflection.fieldsData(),
					m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
				const auto jCur = observe::reflectToJson(cur, m_moduleMemorySize, m_moduleReflection.fieldsData(),
					m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount());
				for (auto d : observe::reflectDiff(jPrev, jCur))
				{
					std::uint32_t off = 0, sz = 0;
					const bool resolved = observe::reflectFieldByteSpan(m_moduleReflection.fieldsData(),
						m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(),
						m_moduleMemorySize, d.value("path", std::string{}), off, sz);
					const char* blame = resolved ? queryModuleWriteBlame(off) : nullptr;
					d["blameSupported"] = (blame != nullptr);
					d["blame"] = (blame != nullptr) ? nlohmann::json(blame) : nlohmann::json(nullptr);
					writes.push_back(std::move(d));
				}
			}
		}
		out["writes"] = std::move(writes);

		// (4) 描画コマンド: Screen の draw log をそのまま乗せる (/api/ai/frame と同じ記録)。
		// beginObject の sourceId タグはこの層に届かない (scene_view.html 専用のピッキング経路が
		// 別に持つ、ADR 0035 O2)。ここは「このフレームに何がどこへ描かれたか」の時系列一覧。
		nlohmann::json draws = nlohmann::json::array();
		if (m_screen)
		{
			m_screen->setDrawLogEnabled(true);
			for (const auto& e : m_screen->drawLog())
			{
				nlohmann::json c;
				c["call"] = e.call; c["x"] = e.x; c["y"] = e.y; c["w"] = e.w; c["h"] = e.h;
				if (e.text[0] != '\0') { c["text"] = e.text; }
				draws.push_back(std::move(c));
			}
		}
		out["draw"] = std::move(draws);

		// (5) 音: 直近フレームの SoundIntent ログ (/api/ai/audio と同じ記録)。
		nlohmann::json sound;
		sound["total"] = static_cast<std::uint64_t>(m_audioLog.totalCount());
		try { sound["events"] = nlohmann::json::parse(m_audioLog.toJson(64)); }
		catch (...) { sound["events"] = nlohmann::json::array(); }
		out["sound"] = std::move(sound);

		return out.dump();
	};

	// AI フレーム観測 (/api/ai/frame): Screen の draw log を on/off + JSON 直列化。
	cb.drawLogEnable = [this](bool on) { if (m_screen) { m_screen->setDrawLogEnabled(on); } };
	cb.drawLogJson = [this]() -> std::string {
		MITIRU_ZONE_NAMED("Engine::drawLogJson");
		if (m_screen == nullptr) { return "[]"; }
		const auto& log = m_screen->drawLog();
		std::string out;
		out.reserve(log.size() * 64 + 2);  // to_string 連結の再確保を避ける概算 (C6)
		out += '[';
		bool first = true;
		for (const auto& e : log)
		{
			if (!first) { out += ','; }
			first = false;
			out += "{\"call\":\"";
			out += e.call;
			out += "\",\"x\":";  observe::appendNumber(out, e.x);
			out += ",\"y\":";    observe::appendNumber(out, e.y);
			out += ",\"w\":";    observe::appendNumber(out, e.w);
			out += ",\"h\":";    observe::appendNumber(out, e.h);
			if (e.text[0] != '\0') { out += ",\"text\":\""; out += observe::jsonEscape(e.text); out += '"'; }
			out += '}';
		}
		out += ']';
		return out;
	};

	m_httpServer->setCallbacks(cb);
	m_httpServer->setInputInjector(&m_inputInjector);
	m_httpServer->setFlags(&m_gameFlags);
	m_httpServer->setConfig(&m_config);

	if (!m_httpServer->init(port))
	{
		// 失敗を知らせずに済ませると、--console / MITIRU_AI が応答の無いまま polling を続ける (H-10、R-01/R-02)。
		std::fprintf(stderr,
			"[ai] HTTP API 起動失敗: 127.0.0.1:%d を listen できません。"
			"port 衝突の可能性 — /api/* は無効です。\n",
			port);
		m_httpServer.reset();
	}
	else
	{
		// listen 開始の合図 (AI / 自動化が polling をやめて叩き始められる、R-02)。
		std::fprintf(stderr,
			"[ai] HTTP API listening on 127.0.0.1:%d "
			"(/api/status, /api/ai/state, /api/ai/diff, /api/ai/branch, /api/ai/why, /api/ai/candidates, "
			"/api/frame/anatomy)\n",
			m_httpServer->port());
	}
}
