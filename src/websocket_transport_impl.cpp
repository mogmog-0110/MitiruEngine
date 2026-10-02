/// @file websocket_transport_impl.cpp
/// @brief WebSocketTransport の実装。cpp-httplib (と windows.h) はこの翻訳単位にだけ入れる

#include <mitiru/network/WebSocketTransport.hpp>

#include <httplib/httplib.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace mitiru::network
{
namespace
{

constexpr const char* kLoopback = "127.0.0.1";
constexpr time_t kHandshakeTimeoutSec = 5;
// 受信を短く区切り、破棄の要求にこの間隔で気づく。
constexpr std::chrono::milliseconds kReadSlice{100};
constexpr std::size_t kMaxServerThreads = 64;

/// @brief 1 本の接続への送信口。WebSocket の持ち主のスレッドは抜ける前に detach() する
class Link
{
public:
	explicit Link(httplib::ws::WebSocket* ws) : m_ws(ws) {}
	explicit Link(httplib::ws::WebSocketClient* client) : m_client(client) {}

	bool send(const std::vector<std::uint8_t>& data)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto* bytes = reinterpret_cast<const char*>(data.data());
		if (m_ws) { return m_ws->send(bytes, data.size()); }
		if (m_client) { return m_client->send(bytes, data.size()); }
		return false;
	}

	void close()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_ws) { m_ws->close(); }
		if (m_client) { m_client->close(); }
	}

	void detach()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_ws = nullptr;
		m_client = nullptr;
	}

private:
	std::mutex m_mutex;
	httplib::ws::WebSocket* m_ws = nullptr;
	httplib::ws::WebSocketClient* m_client = nullptr;
};

struct ClientConnection
{
	std::unique_ptr<httplib::ws::WebSocketClient> client;
	std::thread reader;
};

} // namespace

struct WebSocketTransport::Impl
{
	mutable std::mutex mutex;
	std::vector<NetworkMessage> incoming;
	NetworkStats stats;
	std::map<ConnectionId, std::shared_ptr<Link>> peers;
	std::map<ConnectionId, ClientConnection> clients; ///< 呼び出し元のスレッドだけが触る
	std::atomic<ConnectionId> nextId{0};
	std::atomic<bool> stopping{false};
	std::unique_ptr<httplib::Server> server;
	std::thread serverThread;
	std::uint16_t port = 0;

	void enqueueEvent(ConnectionId id, NetworkEvent evt)
	{
		NetworkMessage msg;
		msg.sender = id;
		msg.header.type = static_cast<std::uint32_t>(evt);
		incoming.push_back(std::move(msg));
	}

	[[nodiscard]] ConnectionId open(std::shared_ptr<Link> link)
	{
		const ConnectionId id = ++nextId;
		std::lock_guard<std::mutex> lock(mutex);
		peers.emplace(id, std::move(link));
		enqueueEvent(id, NetworkEvent::Connected);
		return id;
	}

	void onData(ConnectionId id, const std::string& bytes)
	{
		NetworkMessage msg;
		msg.sender = id;
		msg.header.type = static_cast<std::uint32_t>(NetworkEvent::DataReceived);
		msg.header.size = static_cast<std::uint32_t>(bytes.size());
		msg.payload.assign(bytes.begin(), bytes.end());
		std::lock_guard<std::mutex> lock(mutex);
		incoming.push_back(std::move(msg));
		stats.bytesReceived += bytes.size();
		stats.packetsReceived++;
	}

	void onClosed(ConnectionId id)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (peers.erase(id) != 0) { enqueueEvent(id, NetworkEvent::Disconnected); }
	}

	[[nodiscard]] std::shared_ptr<Link> find(ConnectionId id) const
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto it = peers.find(id);
		return it == peers.end() ? nullptr : it->second;
	}

	/// @brief 接続が閉じるか破棄が始まるまで受信を続ける (接続ごとのスレッドで動く)
	template <class ReadFn>
	void pump(ConnectionId id, ReadFn read)
	{
		std::string msg;
		while (!stopping.load())
		{
			const auto result = read(msg);
			if (result == httplib::ws::Timeout) { continue; }
			if (result == httplib::ws::Fail) { break; }
			onData(id, msg);
		}
		onClosed(id);
	}

	void serve(httplib::ws::WebSocket& ws)
	{
		auto link = std::make_shared<Link>(&ws);
		ws.set_read_timeout(kReadSlice);
		const ConnectionId id = open(link);
		pump(id, [&ws](std::string& msg) { return ws.read(msg); });
		link->detach();
	}

	void joinClient(ConnectionId id)
	{
		const auto it = clients.find(id);
		if (it == clients.end()) { return; }
		if (it->second.reader.joinable()) { it->second.reader.join(); }
		clients.erase(it);
	}

	void shutdown()
	{
		stopping.store(true);
		std::map<ConnectionId, std::shared_ptr<Link>> links;
		{
			std::lock_guard<std::mutex> lock(mutex);
			links = peers;
		}
		for (auto& [id, link] : links) { link->close(); }
		for (auto& [id, conn] : clients) { if (conn.reader.joinable()) { conn.reader.join(); } }
		clients.clear();
		if (server) { server->stop(); }
		if (serverThread.joinable()) { serverThread.join(); }
		server.reset();
	}
};

WebSocketTransport::WebSocketTransport() : m_impl(std::make_unique<Impl>()) {}

WebSocketTransport::~WebSocketTransport() { m_impl->shutdown(); }

bool WebSocketTransport::listen(std::uint16_t port)
{
	auto& impl = *m_impl;
	if (impl.server) { return false; }
	auto server = std::make_unique<httplib::Server>();
	server->new_task_queue = [] { return new httplib::ThreadPool(1, kMaxServerThreads); };
	server->WebSocket(".*", [&impl](const httplib::Request&, httplib::ws::WebSocket& ws) { impl.serve(ws); });
	const int bound = (port == 0) ? server->bind_to_any_port(kLoopback)
		: (server->bind_to_port(kLoopback, port) ? port : -1);
	if (bound <= 0) { return false; }

	impl.port = static_cast<std::uint16_t>(bound);
	impl.serverThread = std::thread([s = server.get()] { s->listen_after_bind(); });
	server->wait_until_ready();
	impl.server = std::move(server);
	return true;
}

bool WebSocketTransport::connect(std::string_view host, std::uint16_t port)
{
	auto client = std::make_unique<httplib::ws::WebSocketClient>(
		"ws://" + std::string(host) + ":" + std::to_string(port) + "/");
	client->set_connection_timeout(kHandshakeTimeoutSec, 0);
	client->set_read_timeout(kHandshakeTimeoutSec, 0);
	if (!client->is_valid() || !client->connect()) { return false; }
	client->set_read_timeout(kReadSlice);

	auto link = std::make_shared<Link>(client.get());
	const ConnectionId id = m_impl->open(link);
	auto* raw = client.get();
	std::thread reader([impl = m_impl.get(), id, raw, link]
	{
		impl->pump(id, [raw](std::string& msg) { return raw->read(msg); });
		link->detach();
	});
	m_impl->clients.emplace(id, ClientConnection{std::move(client), std::move(reader)});
	return true;
}

void WebSocketTransport::disconnect(ConnectionId id)
{
	std::shared_ptr<Link> link;
	{
		std::lock_guard<std::mutex> lock(m_impl->mutex);
		const auto it = m_impl->peers.find(id);
		if (it == m_impl->peers.end()) { return; }
		link = std::move(it->second);
		m_impl->peers.erase(it);
		m_impl->enqueueEvent(id, NetworkEvent::Disconnected);
	}
	link->close();
	m_impl->joinClient(id);
}

void WebSocketTransport::send(ConnectionId id, const std::vector<std::uint8_t>& data)
{
	const auto link = m_impl->find(id);
	if (!link || !link->send(data)) { return; }
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	m_impl->stats.bytesSent += data.size();
	m_impl->stats.packetsSent++;
}

std::vector<NetworkMessage> WebSocketTransport::poll()
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	return std::exchange(m_impl->incoming, {});
}

bool WebSocketTransport::isConnected(ConnectionId id) const
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	return m_impl->peers.count(id) != 0;
}

NetworkStats WebSocketTransport::stats() const
{
	std::lock_guard<std::mutex> lock(m_impl->mutex);
	return m_impl->stats;
}

std::uint16_t WebSocketTransport::getLocalPort() const noexcept
{
	return m_impl->server ? m_impl->port : 0;
}

} // namespace mitiru::network
