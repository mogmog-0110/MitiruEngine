#pragma once

/// @file HttpServer.hpp
/// @brief 軽量HTTP/1.0サーバー（Winsock/POSIX対応）

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

namespace mitiru::observe
{

/// @brief ソケットハンドル型の抽象化
#ifdef _WIN32
using SocketHandle = SOCKET;
static constexpr SocketHandle INVALID_SOCK = INVALID_SOCKET;
#else
using SocketHandle = int;
static constexpr SocketHandle INVALID_SOCK = -1;
#endif

/// @brief HTTPリクエスト
struct HttpRequest
{
	std::string method;                             ///< HTTPメソッド
	std::string path;                               ///< リクエストパス（クエリ文字列含む）
	std::map<std::string, std::string> headers;     ///< ヘッダー
	std::string body;                               ///< ボディ
};

/// @brief HTTPレスポンス
struct HttpResponse
{
	int statusCode = 200;                           ///< ステータスコード
	std::string contentType = "text/plain";         ///< Content-Type
	std::string body;                               ///< ボディ
	std::map<std::string, std::string> headers;     ///< 追加ヘッダー
};

/// @brief リクエストハンドラ型
using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

/// @brief ルートキー（メソッド + パス）
struct RouteKey
{
	std::string method;  ///< HTTPメソッド
	std::string path;    ///< パスパターン
	[[nodiscard]] bool operator<(const RouteKey& other) const noexcept
	{
		return (method != other.method) ? method < other.method : path < other.path;
	}
};

/// @brief 軽量HTTPサーバー（バックグラウンドスレッド、HTTP/1.0、keep-aliveなし）
class HttpServer
{
public:
	HttpServer() = default;
	~HttpServer() { stop(); }
	HttpServer(const HttpServer&) = delete;
	HttpServer& operator=(const HttpServer&) = delete;
	HttpServer(HttpServer&&) = delete;
	HttpServer& operator=(HttpServer&&) = delete;

	/// @brief ルートハンドラを登録する
	void registerRoute(const std::string& method, const std::string& path,
	                   RequestHandler handler)
	{
		const std::lock_guard<std::mutex> lock(m_routeMutex);
		m_routes[RouteKey{method, path}] = std::move(handler);
	}

	/// @brief サーバーを開始する
	/// @param port リッスンポート番号
	/// @return 開始成功なら true
	bool start(std::uint16_t port)
	{
		if (m_running.load()) { return false; }
		if (!initSocket(port)) { return false; }
		m_running.store(true);
		m_thread = std::thread([this]() { acceptLoop(); });
		return true;
	}

	/// @brief サーバーを停止する
	void stop() noexcept
	{
		m_running.store(false);
		if (m_listenSocket != INVALID_SOCK)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = INVALID_SOCK;
		}
		if (m_thread.joinable()) { m_thread.join(); }
	}

	/// @brief サーバーが稼働中か判定する
	[[nodiscard]] bool isRunning() const noexcept { return m_running.load(); }

private:
	/// @brief ソケットを初期化してリッスンを開始する
	bool initSocket(std::uint16_t port)
	{
#ifdef _WIN32
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { return false; }
		m_wsaInitialized = true;
#endif
		m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSocket == INVALID_SOCK) { return false; }

		int opt = 1;
#ifdef _WIN32
		setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
		           reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
		setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
		{
			closeSocket(m_listenSocket); m_listenSocket = INVALID_SOCK; return false;
		}
		if (listen(m_listenSocket, 8) < 0)
		{
			closeSocket(m_listenSocket); m_listenSocket = INVALID_SOCK; return false;
		}
		return true;
	}

	/// @brief 接続受付ループ（バックグラウンドスレッドで実行）
	void acceptLoop()
	{
		while (m_running.load())
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(m_listenSocket, &readSet);
			timeval timeout{}; timeout.tv_sec = 0; timeout.tv_usec = 100000;

			if (select(static_cast<int>(m_listenSocket) + 1,
			           &readSet, nullptr, nullptr, &timeout) <= 0)
			{
				continue;
			}
			const SocketHandle client = accept(m_listenSocket, nullptr, nullptr);
			if (client == INVALID_SOCK) { continue; }
			handleConnection(client);
		}
#ifdef _WIN32
		if (m_wsaInitialized) { WSACleanup(); m_wsaInitialized = false; }
#endif
	}

	/// @brief 1つの接続を処理する
	void handleConnection(SocketHandle clientSocket)
	{
		std::string rawRequest(8192, '\0');
		const auto bytesRead = recv(clientSocket, rawRequest.data(),
		                            static_cast<int>(rawRequest.size()) - 1, 0);
		if (bytesRead <= 0) { closeSocket(clientSocket); return; }
		rawRequest.resize(static_cast<std::size_t>(bytesRead));

		const auto response = dispatch(parseRequest(rawRequest));
		const auto rawResponse = buildResponse(response);
		send(clientSocket, rawResponse.data(),
		     static_cast<int>(rawResponse.size()), 0);
		closeSocket(clientSocket);
	}

	/// @brief 生のHTTPリクエストを解析する
	[[nodiscard]] static HttpRequest parseRequest(const std::string& raw)
	{
		HttpRequest req;
		const auto lineEnd = raw.find("\r\n");
		if (lineEnd == std::string::npos) { return req; }

		const auto requestLine = std::string_view(raw).substr(0, lineEnd);
		const auto sp1 = requestLine.find(' ');
		if (sp1 == std::string_view::npos) { return req; }
		req.method = std::string(requestLine.substr(0, sp1));
		const auto sp2 = requestLine.find(' ', sp1 + 1);
		req.path = (sp2 == std::string_view::npos)
			? std::string(requestLine.substr(sp1 + 1))
			: std::string(requestLine.substr(sp1 + 1, sp2 - sp1 - 1));

		/// ヘッダーを解析する
		auto pos = lineEnd + 2;
		while (pos < raw.size())
		{
			const auto end = raw.find("\r\n", pos);
			if (end == std::string::npos || end == pos) { pos = (end == std::string::npos) ? raw.size() : end + 2; break; }
			const auto line = std::string_view(raw).substr(pos, end - pos);
			const auto col = line.find(':');
			if (col != std::string_view::npos)
			{
				auto val = line.substr(col + 1);
				if (!val.empty() && val[0] == ' ') { val = val.substr(1); }
				req.headers[std::string(line.substr(0, col))] = std::string(val);
			}
			pos = end + 2;
		}
		if (pos < raw.size()) { req.body = raw.substr(pos); }
		return req;
	}

	/// @brief リクエストを適切なハンドラにディスパッチする
	[[nodiscard]] HttpResponse dispatch(const HttpRequest& request)
	{
		auto routePath = request.path;
		const auto qpos = routePath.find('?');
		if (qpos != std::string::npos) { routePath = routePath.substr(0, qpos); }

		const std::lock_guard<std::mutex> lock(m_routeMutex);
		const auto it = m_routes.find(RouteKey{request.method, routePath});
		if (it != m_routes.end()) { return it->second(request); }

		HttpResponse resp;
		resp.statusCode = 404;
		resp.contentType = "application/json";
		resp.body = R"json({"error":"not found"})json";
		return resp;
	}

	/// @brief HTTPレスポンスをバイト列に変換する
	[[nodiscard]] static std::string buildResponse(const HttpResponse& response)
	{
		std::string r;
		r.reserve(256 + response.body.size());
		r += "HTTP/1.0 " + std::to_string(response.statusCode) + " ";
		r += statusText(response.statusCode);
		r += "\r\nContent-Type: " + response.contentType;
		r += "\r\nContent-Length: " + std::to_string(response.body.size());
		r += "\r\nConnection: close";
		r += "\r\nAccess-Control-Allow-Origin: *";
		r += "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS";
		r += "\r\nAccess-Control-Allow-Headers: Content-Type\r\n";
		for (const auto& [key, value] : response.headers)
		{
			r += key + ": " + value + "\r\n";
		}
		r += "\r\n";
		r += response.body;
		return r;
	}

	/// @brief ステータスコードに対応するテキストを返す
	[[nodiscard]] static const char* statusText(int code) noexcept
	{
		switch (code)
		{
		case 200: return "OK";       case 400: return "Bad Request";
		case 404: return "Not Found"; case 500: return "Internal Server Error";
		default:  return "Unknown";
		}
	}

	/// @brief ソケットを閉じる
	static void closeSocket(SocketHandle sock) noexcept
	{
#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
	}

	SocketHandle m_listenSocket = INVALID_SOCK;    ///< リッスンソケット
	std::atomic<bool> m_running{false};             ///< 稼働フラグ
	std::thread m_thread;                           ///< 受付スレッド
	std::mutex m_routeMutex;                        ///< ルートテーブルのミューテックス
	std::map<RouteKey, RequestHandler> m_routes;    ///< ルートハンドラテーブル
#ifdef _WIN32
	bool m_wsaInitialized = false;                  ///< WSA初期化フラグ
#endif
};

} // namespace mitiru::observe
