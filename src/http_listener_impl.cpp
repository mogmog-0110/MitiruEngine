/// @file http_listener_impl.cpp
/// @brief HttpListener の実装。cpp-httplib (と windows.h) はこの翻訳単位にだけ入れる

#include <mitiru/server/HttpListener.hpp>
#include <mitiru/observe/QueryParser.hpp>

#include <httplib/httplib.h>

#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace mitiru::server
{
namespace
{

// ハンドラはどうせ serve() で 1 本に並ぶので、ワーカーは接続を抱えておける数だけあればよい。
constexpr std::size_t kWorkerThreads = 4;
constexpr std::size_t kMaxBodyBytes = 65536;
constexpr time_t kReadTimeoutSec = 2;

struct PendingRequest
{
	HttpRequest request;
	std::promise<HttpResponse> response;
};

[[nodiscard]] HttpRequest toRequest(const httplib::Request& from)
{
	HttpRequest req;
	req.method = from.method;
	req.rawPath = from.target;
	req.path = observe::extractPath(from.target);
	req.params = observe::parseQuery(from.target);
	for (const auto& [name, value] : from.headers) { req.headers[name] = value; }
	req.body = from.body;
	return req;
}

void writeResponse(const HttpResponse& from, httplib::Response& to)
{
	to.status = from.status;
	to.set_content(reinterpret_cast<const char*>(from.body.data()), from.body.size(), from.contentType);
}

[[nodiscard]] HttpResponse errorResponse(int status, const char* message)
{
	HttpResponse resp;
	resp.status = status;
	resp.setBody(std::string(R"({"error":")") + message + "\"}");
	return resp;
}

} // namespace

struct HttpListener::Impl
{
	httplib::Server server;
	std::thread thread;
	std::mutex mutex;
	std::deque<std::shared_ptr<PendingRequest>> queue;
	bool accepting = true;
	int port = 0;

	void handle(const httplib::Request& from, httplib::Response& to)
	{
		auto pending = std::make_shared<PendingRequest>();
		pending->request = toRequest(from);
		auto future = pending->response.get_future();
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!accepting)
			{
				writeResponse(errorResponse(503, "server shutting down"), to);
				return;
			}
			queue.push_back(pending);
		}
		writeResponse(future.get(), to);
	}

	void rejectPending()
	{
		std::lock_guard<std::mutex> lock(mutex);
		accepting = false;
		for (auto& pending : queue) { pending->response.set_value(errorResponse(503, "server shutting down")); }
		queue.clear();
	}

	[[nodiscard]] std::shared_ptr<PendingRequest> takeNext()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (queue.empty()) { return nullptr; }
		auto next = std::move(queue.front());
		queue.pop_front();
		return next;
	}

	void configure()
	{
		server.new_task_queue = [] { return new httplib::ThreadPool(kWorkerThreads); };
		server.set_payload_max_length(kMaxBodyBytes);
		server.set_read_timeout(kReadTimeoutSec, 0);
		server.set_default_headers({
			{"Access-Control-Allow-Origin", "*"},
			{"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
			{"Access-Control-Allow-Headers", "Content-Type"},
		});
		const auto forward = [this](const httplib::Request& from, httplib::Response& to) { handle(from, to); };
		server.Get(".*", forward);
		server.Post(".*", forward);
		server.Put(".*", forward);
		server.Patch(".*", forward);
		server.Delete(".*", forward);
		server.Options(".*", forward);
	}
};

HttpListener::HttpListener() = default;

HttpListener::~HttpListener() { stop(); }

bool HttpListener::start(int port)
{
	if (m_impl) { return false; }
	auto impl = std::make_unique<Impl>();
	impl->configure();
	if (port == 0) { impl->port = impl->server.bind_to_any_port("127.0.0.1"); }
	else if (impl->server.bind_to_port("127.0.0.1", port)) { impl->port = port; }
	if (impl->port <= 0) { return false; }

	impl->thread = std::thread([server = &impl->server] { server->listen_after_bind(); });
	impl->server.wait_until_ready();
	m_impl = std::move(impl);
	return true;
}

void HttpListener::stop() noexcept
{
	if (!m_impl) { return; }
	m_impl->rejectPending();
	m_impl->server.stop();
	if (m_impl->thread.joinable()) { m_impl->thread.join(); }
	m_impl.reset();
}

bool HttpListener::isRunning() const noexcept { return m_impl != nullptr; }

int HttpListener::port() const noexcept { return m_impl ? m_impl->port : 0; }

void HttpListener::serve(int maxRequests, const HttpHandler& handler)
{
	if (!m_impl) { return; }
	for (int i = 0; i < maxRequests; ++i)
	{
		const auto next = m_impl->takeNext();
		if (!next) { return; }
		HttpResponse resp;
		try
		{
			handler(next->request, resp);
		}
		catch (...)
		{
			next->response.set_value(errorResponse(500, "handler threw"));
			throw;
		}
		next->response.set_value(std::move(resp));
	}
}

} // namespace mitiru::server
