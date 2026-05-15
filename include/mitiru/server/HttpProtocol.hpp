#pragma once

/// @file HttpProtocol.hpp
/// @brief HTTP リクエスト/レスポンス構造体 + パース/ビルド + ソケットユーティリティ

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

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
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

namespace mitiru::server
{

// ── ソケットハンドル抽象化 ──────────────────────────────

#ifdef _WIN32
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

// ── HTTPリクエスト/レスポンス ──────────────────────────────

/// @brief HTTPリクエスト
struct HttpRequest
{
	std::string method;                             ///< HTTPメソッド
	std::string path;                               ///< リクエストパス（クエリ文字列除去済み）
	std::string rawPath;                            ///< 生のパス（クエリ文字列含む）
	std::string body;                               ///< リクエストボディ
	std::map<std::string, std::string> params;      ///< クエリパラメータ
	std::map<std::string, std::string> headers;     ///< HTTPヘッダー
};

/// @brief HTTPレスポンス
struct HttpResponse
{
	int status = 200;                               ///< ステータスコード
	std::string contentType = "application/json";   ///< Content-Type
	std::vector<std::uint8_t> body;                 ///< レスポンスボディ（バイナリ対応）

	/// @brief 文字列ボディを設定する
	void setBody(const std::string& text)
	{
		body.assign(text.begin(), text.end());
	}
};

// ── ソケットユーティリティ ──────────────────────────────

/// @brief ソケットをノンブロッキングに設定する
inline void setNonBlocking(SocketHandle sock)
{
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket(sock, FIONBIO, &mode);
#else
	const int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

/// @brief ソケットを閉じる
inline void closeSocket(SocketHandle sock) noexcept
{
#ifdef _WIN32
	closesocket(sock);
#else
	close(sock);
#endif
}

/// @brief 受信タイムアウトを設定する
inline void setRecvTimeout(SocketHandle sock, int timeoutMs)
{
#ifdef _WIN32
	DWORD tv = static_cast<DWORD>(timeoutMs);
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
	           reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
	timeval tv{};
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// ── HTTPパース/ビルド ──────────────────────────────

/// @brief ステータスコードに対応するテキストを返す
[[nodiscard]] inline const char* statusText(int code) noexcept
{
	switch (code)
	{
	case 200: return "OK";
	case 204: return "No Content";
	case 400: return "Bad Request";
	case 404: return "Not Found";
	case 500: return "Internal Server Error";
	default:  return "Unknown";
	}
}

/// @brief HTTPレスポンスをバイト列に変換する
[[nodiscard]] inline std::vector<std::uint8_t> buildResponse(const HttpResponse& resp)
{
	std::string header;
	header.reserve(256);
	header += "HTTP/1.0 " + std::to_string(resp.status) + " ";
	header += statusText(resp.status);
	header += "\r\nContent-Type: " + resp.contentType;
	header += "\r\nContent-Length: " + std::to_string(resp.body.size());
	header += "\r\nConnection: close";
	header += "\r\nAccess-Control-Allow-Origin: *";
	header += "\r\nAccess-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS";
	header += "\r\nAccess-Control-Allow-Headers: Content-Type";
	header += "\r\n\r\n";

	std::vector<std::uint8_t> result;
	result.reserve(header.size() + resp.body.size());
	result.insert(result.end(), header.begin(), header.end());
	result.insert(result.end(), resp.body.begin(), resp.body.end());
	return result;
}

} // namespace mitiru::server
