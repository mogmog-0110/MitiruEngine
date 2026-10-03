#include "ToolSession.hpp"

#include <mitiru/observe/ScrubControlChannel.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>

#include <cstdlib>
#include <fstream>

namespace mitiru::tool
{

namespace
{

constexpr double kSnapshotInterval = 1.0 / 30.0;

/// 監視中のゲーム (host pid) 宛に scrub command を書く。host が ScrubControlReader で読み、
/// GameMemory を過去の bytes へ戻す (戻すのは host の仕事)。
class ScrubSignals final : public ToolSignals
{
public:
	explicit ScrubSignals(int pid) : m_writer(pid) {}
	void scrub(int offsetFromNewest) override { m_writer.write({ { "scrubTo", offsetFromNewest }, { "seq", ++m_seq } }); }
	void resume() override { m_writer.write({ { "resume", 1 }, { "seq", ++m_seq } }); }

private:
	observe::ScrubControlWriter m_writer;
	long m_seq = 0;
};

} // namespace

std::optional<Snapshot> SnapshotSource::poll()
{
	std::error_code ec;
	if (!std::filesystem::exists(m_path, ec)) { return std::nullopt; }
	const auto mt = std::filesystem::last_write_time(m_path, ec);
	if (ec || (m_haveWrite && mt == m_lastWrite)) { return std::nullopt; }
	std::ifstream in(m_path, std::ios::binary);
	if (!in) { return std::nullopt; }
	// 書き換えの途中 (rename の前後) を読むと壊れた JSON になる。次の回に読み直す。
	Snapshot j = Snapshot::parse(in, nullptr, false);
	if (j.is_discarded()) { return std::nullopt; }
	m_lastWrite = mt;
	m_haveWrite = true;
	return j;
}

bool ToolSession::start(ID3D12Device* device, ID3D12CommandQueue* queue, const ToolOptions& options,
                        int width, int height, float dpRatio, std::string& error)
{
	if (!m_ui.start(device, queue, options.document, width, height, dpRatio, error)) { return false; }
	PageContext ctx;
	ctx.view = &m_ui;
	ctx.query = options.query;
	ctx.mtrrPath = options.mtrr;
	if (pageRequestsScrub(options.page) && options.pid) { m_signals = std::make_unique<ScrubSignals>(*options.pid); }
	if (options.pid) { m_beacon.emplace(observe::sharedSnapshotPathForPid(*options.pid)); }
	if (pageUsesHttp(options.page)) { m_http = std::make_unique<HttpWorker>(options.httpPort); }
	ctx.signals = m_signals.get();
	ctx.http = m_http.get();
	m_page = makePage(options.page, ctx);
	if (m_page->wantsSnapshot())
	{
		if (options.file) { m_source.emplace(*options.file); }
		else if (options.pid) { m_source.emplace(observe::sharedSnapshotPathForPid(*options.pid)); }
	}
	m_page->start();
	m_ui.setPage(m_page.get());
	return true;
}

void ToolSession::pollSnapshot(double now)
{
	if (!m_source || now < m_nextPoll) { return; }
	m_nextPoll = now + kSnapshotInterval;
	if (std::optional<Snapshot> snap = m_source->poll())
	{
		m_everRead = true;
		m_page->onSnapshot(*snap, m_everRead);
	}
}

void ToolSession::frame(double now, const ToolPointer& pointer, std::span<const platform::Win32KeyMessage> keys)
{
	if (m_beacon) { m_beacon->touch(); }
	pollSnapshot(now);
	if (m_http)
	{
		for (const HttpResponse& res : m_http->takeResponses()) { m_page->onHttp(res); }
	}
	m_page->tick(now);
	m_ui.reloadIfChanged();
	m_ui.processInput(pointer, keys);
	m_ui.update(now);
}

std::filesystem::path findPageDocument(const std::filesystem::path& exeDir, const std::string& page)
{
	std::error_code ec;
	const std::filesystem::path bundled = exeDir / "assets" / (page + ".rml");
	if (std::filesystem::exists(bundled, ec)) { return bundled; }
	// hud.open() でゲーム独自のページ名が来た時は、game DLL の assets/ (host が MITIRU_ASSET_ROOT に置く) を探す。
	if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && root[0] != '\0')
	{
		const std::filesystem::path game = std::filesystem::path(root) / "assets" / (page + ".rml");
		if (std::filesystem::exists(game, ec)) { return game; }
	}
	return {};
}

} // namespace mitiru::tool
