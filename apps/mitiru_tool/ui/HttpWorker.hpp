#pragma once
// ToolHttp の本物。ゲームの HTTP API (127.0.0.1:<port>) へ別スレッドで 1 本ずつ問い合わせ、応答を
// 次のフレームで返す。ゲームが --http-port 無しで動いていると接続できないが、窓は止めない。

#include "ToolPage.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace httplib
{
class Client;
}

namespace mitiru::tool
{

class HttpWorker final : public ToolHttp
{
public:
	explicit HttpWorker(int port);
	~HttpWorker() override;
	HttpWorker(const HttpWorker&) = delete;
	HttpWorker& operator=(const HttpWorker&) = delete;

	void send(HttpRequest request) override;
	void setPort(int port) override;
	[[nodiscard]] int port() const override;
	[[nodiscard]] std::vector<HttpResponse> takeResponses() override;

private:
	void run();
	[[nodiscard]] HttpResponse perform(const HttpRequest& request, int port);

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::deque<HttpRequest> m_queue;
	std::vector<HttpResponse> m_done;
	int m_port;
	bool m_stop = false;
	httplib::Client* m_active = nullptr;   ///< 問い合わせ中の接続 (閉じる時に切って待たずに抜ける)
	std::thread m_thread;
};

} // namespace mitiru::tool
