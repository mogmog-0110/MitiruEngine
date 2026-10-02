#include "HttpWorker.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include <httplib/httplib.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace mitiru::tool
{

HttpWorker::HttpWorker(int port)
	: m_port(port)
	, m_thread([this] { run(); })
{
}

HttpWorker::~HttpWorker()
{
	{
		std::lock_guard lock(m_mutex);
		m_stop = true;
		// 候補の並走は数十秒かかることがある。窓を閉じたら読みの途中でも切る。
		if (m_active != nullptr) { m_active->stop(); }
	}
	m_wake.notify_all();
	m_thread.join();
}

void HttpWorker::send(HttpRequest request)
{
	{
		std::lock_guard lock(m_mutex);
		m_queue.push_back(std::move(request));
	}
	m_wake.notify_one();
}

void HttpWorker::setPort(int port)
{
	std::lock_guard lock(m_mutex);
	m_port = port;
}

int HttpWorker::port() const
{
	std::lock_guard lock(m_mutex);
	return m_port;
}

std::vector<HttpResponse> HttpWorker::takeResponses()
{
	std::lock_guard lock(m_mutex);
	return std::exchange(m_done, {});
}

void HttpWorker::run()
{
	while (true)
	{
		HttpRequest request;
		int port = 0;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
			if (m_stop) { return; }
			request = std::move(m_queue.front());
			m_queue.pop_front();
			port = m_port;
		}
		HttpResponse response = perform(request, port);
		std::lock_guard lock(m_mutex);
		m_done.push_back(std::move(response));
	}
}

HttpResponse HttpWorker::perform(const HttpRequest& request, int port)
{
	HttpResponse out;
	out.tag = request.tag;
	httplib::Client client("127.0.0.1", port);
	client.set_connection_timeout(0, 300000);
	// 候補の並走 (/api/ai/candidates) はゲームを数十フレーム回すので、読みの待ちは長めに取る。
	client.set_read_timeout(20, 0);
	{
		std::lock_guard lock(m_mutex);
		if (m_stop) { return out; }
		m_active = &client;
	}
	httplib::Result result;
	if (request.method == "PUT")       { result = client.Put(request.path, request.body, "application/json"); }
	else if (request.method == "POST") { result = client.Post(request.path, request.body, "application/json"); }
	else                               { result = client.Get(request.path); }
	{
		std::lock_guard lock(m_mutex);
		m_active = nullptr;
	}
	if (result)
	{
		out.status = result->status;
		out.body = result->body;
	}
	return out;
}

} // namespace mitiru::tool
