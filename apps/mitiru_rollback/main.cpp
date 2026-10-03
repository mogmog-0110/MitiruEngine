// mitiru_rollback: ゲーム DLL を 2 つ読み込み、同じプロセスの loopback 回線 (遅延つき) でロールバック対戦させる
// コンソールツール。両者の入力には、seed から作る擬似乱数によるボタン連打を使い、終了後に
//   - 2 人の GameMemory がバイト単位で同じか、窓口 (MITIRU_SIDE_STATE) の hash も同じか
//   - 確定した入力をロールバック無しで 3 つ目の DLL に流した結果とも同じか
// を確かめる。食い違う場合は、GameMemory と窓口の外に状態を持っている (= オンライン対戦に載らない) ということ。
// --authority は巻き戻しの代わりに host 権威 (AuthorityBench.hpp) で対戦させ、回線の量を測る。
// 引数の一覧は usage() が出す。一致なら 0、食い違いは 1、引数や読み込みの誤りは 2 を返す。

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SideStateHost.hpp>
#include <mitiru/network/RollbackLoopback.hpp>
#include <mitiru/network/RollbackSession.hpp>

#include "AuthorityBench.hpp"

using namespace mitiru::network::rollback;
namespace module = mitiru::module;

namespace
{

struct Options
{
	std::string dll;
	int frames = 600;
	int latency = 4;
	int delay = 2;
	std::uint32_t seed = 1;
	bool bench = false;   ///< 対戦はせず、1 フレームの進め・保存・戻しの時間を測る
	bool authority = false;
	authority_bench::Settings net;   ///< --authority の状態の回数と回線の悪さ
};

/// ゲーム DLL 1 つ分。ModuleHost が DLL を temp へ写して読み込むため、同じ DLL を何度読み込んでも static は別々
struct Game
{
	module::ModuleHost host;
	std::unique_ptr<module::ModuleApi> api = std::make_unique<module::ModuleApi>();
	void* memory = nullptr;
	module::SideStateHost sides;   ///< DLL の窓口。巻き戻すときに GameMemory と一緒に戻す

	~Game()
	{
		if (memory == nullptr) return;
		if (auto unload = host.unloadFn()) unload(memory);
	}
};

void usage()
{
	std::fprintf(stderr, "usage: mitiru_rollback <game.dll> [--frames N] [--latency ticks] [--delay frames] [--seed S] [--bench]\n"
	                     "       mitiru_rollback <game.dll> --authority [--rate Hz] [--link L,J,P] [--frames N] [--seed S]\n");
}

bool parseNet(const std::string& k, const char* value, Options& o)
{
	if (k == "--rate") o.net.rate = std::atoi(value);
	else if (k == "--link")
	{
		const auto link = mitiru::network::parseLinkConditions(value);
		if (!link) return false;
		o.net.link = *link;
	}
	else return false;
	return true;
}

bool parse(int argc, char** argv, Options& o)
{
	for (int i = 1; i < argc; ++i)
	{
		const std::string k = argv[i];
		const bool hasValue = i + 1 < argc;
		if ((k == "--rate" || k == "--link") && hasValue)
		{
			if (!parseNet(k, argv[++i], o)) return false;
		}
		else if (k == "--authority") o.authority = true;
		else if (k == "--frames" && hasValue) o.frames = std::atoi(argv[++i]);
		else if (k == "--latency" && hasValue) o.latency = std::atoi(argv[++i]);
		else if (k == "--delay" && hasValue) o.delay = std::atoi(argv[++i]);
		else if (k == "--seed" && hasValue) o.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
		else if (k == "--bench") o.bench = true;
		else if (!k.empty() && k[0] != '-' && o.dll.empty()) o.dll = k;
		else return false;
	}
	o.net.frames = o.frames;
	const bool rateOk = o.net.rate >= 1 && o.net.rate <= 60 && 60 % o.net.rate == 0;
	// --authority は終わりの 60 フレーム前の状態を突き合わせる
	return !o.dll.empty() && o.frames > (o.authority ? 60 : 0) && o.latency >= 0 && o.delay >= 0 && rateOk;
}

/// DLL の窓口の表を取り込む。名前の重複や関数の欠けがあると、その窓口は戻らず食い違うので読み込みを断る
bool bindSides(Game& g)
{
	const auto fn = g.host.sideStatesFn();
	if (fn == nullptr) return true;
	std::array<module::SideStateChannel, module::kMaxSideStateChannels> table{};
	const std::int32_t n = (std::min)(fn(table.data(), module::kMaxSideStateChannels), module::kMaxSideStateChannels);
	const std::string dropped = g.sides.bind(table.data(), n);
	if (dropped.empty()) return true;
	std::fprintf(stderr, "mitiru_rollback: 窓口の申告が正しくない: %s\n", dropped.c_str());
	return false;
}

/// 窓口の hash だけの image (2 つの DLL の窓口の状態を比べる用)
std::vector<std::uint8_t> sideHashes(Game& g)
{
	std::vector<std::uint8_t> out;
	std::string why;
	if (!g.sides.empty() && !g.sides.capture(g.memory, false, out, &why))
	{
		std::fprintf(stderr, "mitiru_rollback: 窓口の状態を取れない: %s\n", why.c_str());
		out.clear();
	}
	return out;
}

bool load(Game& g, const std::string& path)
{
	if (!g.host.load(path))
	{
		std::fprintf(stderr, "mitiru_rollback: 読めない: %s\n", g.host.lastError().c_str());
		return false;
	}
	g.api->version = module::kWireApiVersion;
	g.host.loadFn()(g.api.get(), &g.memory);
	if (g.api->version != module::kWireApiVersion)
	{
		std::fprintf(stderr, "mitiru_rollback: DLL の ABI が違う (このツールと同じ engine でビルドし直す)\n");
		return false;
	}
	if (g.api->on_init != nullptr) g.api->on_init(g.memory);
	return bindSides(g);
}

/// 数フレームごとに連打するボタンが変わる。seed と player ごとに異なる列
PadInput bot(std::uint32_t seed, int player, int frame)
{
	std::uint32_t h = (static_cast<std::uint32_t>(frame / 6) + seed * 977u) * 2654435761u + static_cast<std::uint32_t>(player) * 40503u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return PadInput{h & 0x1FFu};
}

using InputLog = std::vector<std::array<PadInput, kMaxPlayers>>;

/// 記録した入力を 3 つ目の DLL へロールバック無しで流す
void replay(Game& ref, const InputLog& log, int frames)
{
	const RollbackConfig cfg;
	auto intents = std::make_unique<module::FrameIntents>();
	std::array<PadInput, kMaxPlayers> prev{};
	for (int f = 0; f < frames; ++f)
	{
		const auto& now = log[static_cast<std::size_t>(f)];
		module::InputSnapshot snap;
		composeSnapshot(snap, std::span<const PadInput>(now.data(), 2), std::span<const PadInput>(prev.data(), 2),
			cfg.keymaps, cfg.snapshot);
		intents->reset();
		ref.api->on_update(ref.memory, cfg.snapshot.dt, &snap, intents.get());
		prev = now;
	}
}

struct MatchResult
{
	RollbackStats a, b;
	InputLog log;
	std::uint64_t packets = 0;
};

[[nodiscard]] bool play(const Options& o, Game (&games)[2], MatchResult& out)
{
	RollbackLoopback net(o.latency);
	std::unique_ptr<RollbackPeer> peers[2];
	const GekkoNetAddress remotes[2] = {net.address(0), net.address(1)};
	for (int p = 0; p < 2; ++p)
	{
		RollbackConfig cfg;
		cfg.localPlayer = p;
		cfg.inputDelay = o.delay;
		peers[p] = std::make_unique<RollbackPeer>(*games[p].api, games[p].memory, cfg, net.adapter(p), remotes, &games[p].sides);
		if (peers[p]->error() != nullptr)
		{
			std::fprintf(stderr, "mitiru_rollback: %s\n", peers[p]->error());
			return false;
		}
	}
	peers[0]->setInputLog(&out.log);
	// 入力を止めた後も処理を続け、最後の予測が当たって 2 人のフレームが揃うまで待つ
	const int tail = 2 * (o.latency + o.delay) + 30;
	for (int t = 0; t < o.frames + tail || (peers[0]->stats().frame != peers[1]->stats().frame && t < o.frames + tail * 4); ++t)
	{
		for (int p = 0; p < 2; ++p) peers[p]->tick(t < o.frames ? bot(o.seed, p, t) : PadInput{});
		net.tick();
	}
	for (int p = 0; p < 2; ++p)
	{
		if (peers[p]->error() == nullptr) continue;
		std::fprintf(stderr, "mitiru_rollback: %c が止まった: %s\n", 'A' + p, peers[p]->error());
		return false;
	}
	out.a = peers[0]->stats();
	out.b = peers[1]->stats();
	out.packets = net.packetsSent();
	return true;
}

using Clock = std::chrono::steady_clock;

double msBetween(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

double percentile(std::vector<double> v, double q)
{
	if (v.empty()) return 0.0;
	std::sort(v.begin(), v.end());
	return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
}

struct BenchSamples
{
	std::vector<double> update, save, load;
	std::size_t sideBytes = 0;
};

/// 保存した GameMemory と窓口の image へ戻す (RollbackPeer の Load と同じ順)
bool restoreState(Game& g, const std::vector<std::uint8_t>& state, const std::vector<std::uint8_t>& side)
{
	std::memcpy(g.memory, state.data(), state.size());
	if (g.api->on_rebuild != nullptr) g.api->on_rebuild(g.memory, module::kModuleRebuildRestore);
	std::string why;
	if (g.sides.empty() || g.sides.restore(g.memory, side.data(), side.size(), &why)) return true;
	std::fprintf(stderr, "mitiru_rollback: 窓口を戻せない: %s\n", why.c_str());
	return false;
}

/// 毎フレーム 保存 → on_update、8 フレームごとに戻して同じ入力で進め直す。巻き戻し 1 フレームの費用は 進め + 保存
bool sampleCosts(const Options& o, Game& g, BenchSamples& s)
{
	const RollbackConfig cfg;
	auto intents = std::make_unique<module::FrameIntents>();
	auto snap = std::make_unique<module::InputSnapshot>();
	std::vector<std::uint8_t> state(g.api->memorySize);
	std::vector<std::uint8_t> side;
	std::array<PadInput, kMaxPlayers> prev{};
	std::string why;
	for (int f = 0; f < o.frames; ++f)
	{
		const std::array<PadInput, kMaxPlayers> now{bot(o.seed, 0, f), bot(o.seed, 1, f)};
		composeSnapshot(*snap, std::span<const PadInput>(now.data(), 2), std::span<const PadInput>(prev.data(), 2), cfg.keymaps,
			cfg.snapshot);
		const auto t0 = Clock::now();
		std::memcpy(state.data(), g.memory, state.size());
		if (!g.sides.empty() && !g.sides.capture(g.memory, true, side, &why)) return false;
		const auto t1 = Clock::now();
		intents->reset();
		g.api->on_update(g.memory, cfg.snapshot.dt, snap.get(), intents.get());
		const auto t2 = Clock::now();
		s.save.push_back(msBetween(t0, t1));
		s.update.push_back(msBetween(t1, t2));
		s.sideBytes = (std::max)(s.sideBytes, side.size());
		if (f % 8 != 7) { prev = now; continue; }
		const auto t3 = Clock::now();
		if (!restoreState(g, state, side)) return false;
		s.load.push_back(msBetween(t3, Clock::now()));
		intents->reset();
		g.api->on_update(g.memory, cfg.snapshot.dt, snap.get(), intents.get());
		prev = now;
	}
	return true;
}

/// 進め・保存・戻しの時間と、60Hz の半分 (残りは描画) で何フレーム巻き戻せるかを出す
int bench(const Options& o, Game& g)
{
	BenchSamples s;
	if (!sampleCosts(o, g, s)) return 2;
	const double update = percentile(s.update, 0.5);
	const double save = percentile(s.save, 0.5);
	const double load = percentile(s.load, 0.5);
	const double budget = 1000.0 / 60.0 / 2.0;
	const double perFrame = percentile(s.update, 0.95) + percentile(s.save, 0.95);
	const int affordable = perFrame > 0.0 ? static_cast<int>((budget - load) / perFrame) : 0;
	std::printf("frames %d  GameMemory %u B  side %zu B\n", o.frames, g.api->memorySize, s.sideBytes);
	std::printf("update p50 %.3f ms  p95 %.3f  max %.3f\n", update, percentile(s.update, 0.95), percentile(s.update, 1.0));
	std::printf("save   p50 %.3f ms  p95 %.3f\n", save, percentile(s.save, 0.95));
	std::printf("load   p50 %.3f ms  p95 %.3f\n", load, percentile(s.load, 0.95));
	std::printf("rollback frames in %.2f ms (p95): %d\n", budget, affordable);
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	Options o;
	if (!parse(argc, argv, o)) { usage(); return 2; }
	if (o.bench)
	{
		Game g;
		return load(g, o.dll) ? bench(o, g) : 2;
	}
	if (o.authority)
	{
		Game host, client;
		if (!load(host, o.dll) || !load(client, o.dll)) return 2;
		return authority_bench::run(o.net, host, client, o.seed, &bot);
	}
	Game games[2];
	Game ref;
	if (!load(games[0], o.dll) || !load(games[1], o.dll) || !load(ref, o.dll)) return 2;

	MatchResult r;
	if (!play(o, games, r)) return 2;
	const int frames = r.a.frame + 1;
	std::printf("frames %d  rollbacks %d  resimulated %d  packets %llu  desyncs %d/%d\n", frames, r.a.loads,
		r.a.resimulated, static_cast<unsigned long long>(r.packets), r.a.desyncs, r.b.desyncs);
	if (!r.a.started || r.a.frame != r.b.frame)
	{
		std::fprintf(stderr, "mitiru_rollback: 2 人のフレームが揃わない (A=%d B=%d)\n", r.a.frame, r.b.frame);
		return 1;
	}
	replay(ref, r.log, frames);
	const std::size_t size = games[0].api->memorySize;
	const bool peersMatch = std::memcmp(games[0].memory, games[1].memory, size) == 0;
	const bool refMatch = std::memcmp(games[0].memory, ref.memory, size) == 0;
	std::printf("GameMemory %zu bytes  A==B %s  A==replay %s\n", size, peersMatch ? "yes" : "NO", refMatch ? "yes" : "NO");
	const auto sides = sideHashes(games[0]);
	const bool sidesMatch = games[0].sides.empty() || (!sides.empty() && sides == sideHashes(games[1]) && sides == sideHashes(ref));
	if (!games[0].sides.empty()) std::printf("side state %d channels  A==B==replay %s\n", games[0].sides.count(), sidesMatch ? "yes" : "NO");
	const bool ok = peersMatch && refMatch && sidesMatch && r.a.desyncs == 0 && r.b.desyncs == 0;
	std::printf("%s\n", ok ? "OK" : "DESYNC (GameMemory の外に状態があるか、入力以外で結果が変わる)");
	return ok ? 0 : 1;
}
