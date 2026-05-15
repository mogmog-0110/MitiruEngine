#pragma once

/// @file SocketCompat.hpp
/// @brief ソケットAPI差異のクロスプラットフォーム抽象化
/// @details Windows (Winsock2) と POSIX (Berkeley sockets) の差異を吸収する。
///          TcpTransport・UdpTransport等のネットワークバックエンドで使用する。

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
	#include <cerrno>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <sys/ioctl.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

namespace mitiru::network
{

/// @brief ソケットハンドル型（Win: SOCKET / POSIX: int）
#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

/// @brief 無効ソケット定数
#ifdef _WIN32
inline constexpr SocketHandle INVALID_SOCK = INVALID_SOCKET;
#else
inline constexpr SocketHandle INVALID_SOCK = -1;
#endif

/// @brief ソケット操作エラー定数（send/recv/bind等の失敗戻り値）
#ifdef _WIN32
inline constexpr int SOCK_ERR = SOCKET_ERROR;
#else
inline constexpr int SOCK_ERR = -1;
#endif

/// @brief 非ブロッキング操作でデータが無い場合のエラーコード
#ifdef _WIN32
inline constexpr int WOULD_BLOCK_ERR = WSAEWOULDBLOCK;
#else
/// POSIX では EWOULDBLOCK と EAGAIN は同義の場合が多いが両方チェックする
inline constexpr int WOULD_BLOCK_ERR = EWOULDBLOCK;
#endif

/// @brief 最後のソケットエラーコードを取得する
/// @return Win: WSAGetLastError() / POSIX: errno
[[nodiscard]] inline int lastSocketError() noexcept
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

/// @brief 非ブロッキングエラーかどうかを判定する
/// @details POSIX では EWOULDBLOCK と EAGAIN の両方を許容する。
/// @param err lastSocketError() の戻り値
/// @return ブロッキング相当のエラーなら true
[[nodiscard]] inline bool isWouldBlock(int err) noexcept
{
#ifdef _WIN32
	return err == WSAEWOULDBLOCK;
#else
	return err == EWOULDBLOCK || err == EAGAIN;
#endif
}

/// @brief ソケットを閉じる
/// @param sock 閉じるソケットハンドル
inline void closeSocket(SocketHandle sock) noexcept
{
#ifdef _WIN32
	::closesocket(sock);
#else
	::close(sock);
#endif
}

/// @brief ソケットを非ブロッキングモードに設定する
/// @param sock 対象ソケット
/// @return 成功すれば true
[[nodiscard]] inline bool setNonBlocking(SocketHandle sock) noexcept
{
#ifdef _WIN32
	u_long mode = 1;
	return (::ioctlsocket(sock, FIONBIO, &mode) == 0);
#else
	const int flags = ::fcntl(sock, F_GETFL, 0);
	if (flags == -1) return false;
	return (::fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0);
#endif
}

/// @brief WSA初期化/後始末をRAIIで管理するガード
/// @details Windows ではコンストラクタで WSAStartup、デストラクタで WSACleanup を呼ぶ。
///          POSIX ではコンスト・デストとも何もしない（no-op）。
class WsaGuard final
{
public:
#ifdef _WIN32
	/// @brief コンストラクタ（WSAStartupを呼ぶ）
	WsaGuard() noexcept
	{
		WSADATA wsaData{};
		m_initialized = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
	}

	/// @brief デストラクタ（WSACleanupを呼ぶ）
	~WsaGuard() noexcept
	{
		if (m_initialized)
		{
			::WSACleanup();
		}
	}

	/// @brief 初期化が成功したかどうかを返す
	/// @return 初期化成功なら true
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

private:
	bool m_initialized{false};
#else
	/// @brief POSIX: 初期化不要（常に成功）
	WsaGuard() noexcept = default;
	~WsaGuard() noexcept = default;
	[[nodiscard]] bool isInitialized() const noexcept { return true; }
#endif

public:
	/// コピー・ムーブ禁止
	WsaGuard(const WsaGuard&) = delete;
	WsaGuard& operator=(const WsaGuard&) = delete;
	WsaGuard(WsaGuard&&) = delete;
	WsaGuard& operator=(WsaGuard&&) = delete;
};

} // namespace mitiru::network
