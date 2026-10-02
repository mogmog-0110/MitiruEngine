#pragma once

/// @file HttpListener.hpp
/// @brief 127.0.0.1 だけで待ち受ける HTTP/1.1 の口 (cpp-httplib、実装は src/http_listener_impl.cpp)
/// @details 受信と解析はワーカースレッドで行い、ハンドラは serve() を呼んだスレッドでだけ動かす。
///          ハンドラはエンジンの状態を読み書きするので、ゲームループの外で走らせない。

#include <mitiru/server/HttpProtocol.hpp>

#include <functional>
#include <memory>

namespace mitiru::server
{

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpListener
{
public:
	HttpListener();
	~HttpListener();

	HttpListener(const HttpListener&) = delete;
	HttpListener& operator=(const HttpListener&) = delete;
	HttpListener(HttpListener&&) = delete;
	HttpListener& operator=(HttpListener&&) = delete;

	/// @param port 0 なら空いているポートを OS に選ばせる (選ばれた番号は port() で読む)
	[[nodiscard]] bool start(int port);

	/// @brief 待ち受けを止める。応答待ちのリクエストには 503 を返す
	void stop() noexcept;

	[[nodiscard]] bool isRunning() const noexcept;
	[[nodiscard]] int port() const noexcept;

	/// @brief 届いているリクエストを最大 maxRequests 件、呼び出し元のスレッドで handler に渡す
	void serve(int maxRequests, const HttpHandler& handler);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace mitiru::server
