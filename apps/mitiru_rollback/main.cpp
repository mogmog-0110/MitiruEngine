// mitiru_rollback: ゲーム DLL を 2 つ読み込み、同じプロセスの loopback 回線 (遅延つき) でロールバック対戦させる
// コンソールツール。両者の入力には、seed から作る擬似乱数によるボタン連打を使い、終了後に
//   - 2 人の GameMemory がバイト単位で同じか
//   - 確定した入力をロールバック無しで 3 つ目の DLL に流した結果とも同じか
// を確かめる。食い違う場合は、GameMemory の外に状態を持っている (= オンライン対戦に載らない) ということ。
// 引数の一覧は usage() が出す。一致なら 0、食い違いは 1、引数や読み込みの誤りは 2 を返す。

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/network/RollbackLoopback.hpp>
#include <mitiru/network/RollbackSession.hpp>

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
};

/// ゲーム DLL 1 つ分。ModuleHost が DLL を temp へ写して読み込むため、同じ DLL を何度読み込んでも static は別々
struct Game
{
	module::ModuleHost host;
	std::unique_ptr<module::ModuleApi> api = std::make_unique<module::ModuleApi>();
	void* memory = nullptr;

	~Game()
	{
		if (memory == nullptr) return;
		if (auto unload = host.unloadFn()) unload(memory);
	}
};

void usage()
{
	std::fprintf(stderr, "usage: mitiru_rollback <game.dll> [--frames N] [--latency ticks] [--delay frames] [--seed S]\n");
}

bool parse(int argc, char** argv, Options& o)
{
	for (int i = 1; i < argc; ++i)
	{
		const std::string k = argv[i];
		const bool hasValue = i + 1 < argc;
		if (k == "--frames" && hasValue) o.frames = std::atoi(argv[++i]);
		else if (k == "--latency" && hasValue) o.latency = std::atoi(argv[++i]);
		else if (k == "--delay" && hasValue) o.delay = std::atoi(argv[++i]);
		else if (k == "--seed" && hasValue) o.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
		else if (!k.empty() && k[0] != '-' && o.dll.empty()) o.dll = k;
		else return false;
	}
	return !o.dll.empty() && o.frames > 0 && o.latency >= 0 && o.delay >= 0;
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
	return true;
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
		peers[p] = std::make_unique<RollbackPeer>(*games[p].api, games[p].memory, cfg, net.adapter(p), remotes);
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
	out.a = peers[0]->stats();
	out.b = peers[1]->stats();
	out.packets = net.packetsSent();
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	Options o;
	if (!parse(argc, argv, o)) { usage(); return 2; }
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
	const bool ok = peersMatch && refMatch && r.a.desyncs == 0 && r.b.desyncs == 0;
	std::printf("%s\n", ok ? "OK" : "DESYNC (GameMemory の外に状態があるか、入力以外で結果が変わる)");
	return ok ? 0 : 1;
}
