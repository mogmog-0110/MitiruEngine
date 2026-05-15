#pragma once

/// @file TcpTransport.hpp
/// @brief TCP ソケットベースのネットワークトランスポート実装（クロスプラットフォーム）
/// @details SocketCompatを通じて Windows (Winsock2) および POSIX (Linux/macOS) で動作する
///          非ブロッキングTCPトランスポート。
///          サーバーモード（listen/accept）とクライアントモード（connect）の両方をサポート。
///          INetworkTransportインターフェースに準拠する。
///
/// @code
/// // サーバー側
/// mitiru::network::TcpTransport server;
/// server.listen(12345);
///
/// // クライアント側
/// mitiru::network::TcpTransport client;
/// client.connect("127.0.0.1", 12345);
///
/// // データ送受信
/// std::vector<uint8_t> data = {1, 2, 3};
/// client.send(connId, data);
/// auto msgs = server.poll();
/// @endcode

#include <mitiru/network/INetworkTransport.hpp>
#include <mitiru/network/SocketCompat.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::network
{

/// @brief TCP ソケットベースのトランスポート実装（クロスプラットフォーム）
///
/// SocketCompatの非ブロッキングソケットを使用してTCP通信を行う。
/// listen()でサーバーモード、connect()でクライアントモードとして動作する。
/// poll()で受信メッセージとイベントを取得する。
class TcpTransport final : public INetworkTransport
{
public:
	/// @brief コンストラクタ（WSA初期化を行う / POSIX ではno-op）
	TcpTransport() = default;

	/// @brief デストラクタ（リソースを解放する）
	~TcpTransport() override
	{
		closeAll();
	}

	/// コピー・ムーブ禁止（ソケットリソース管理のため）
	TcpTransport(const TcpTransport&) = delete;
	TcpTransport& operator=(const TcpTransport&) = delete;
	TcpTransport(TcpTransport&&) = delete;
	TcpTransport& operator=(TcpTransport&&) = delete;

	/// @brief ソケットシステムが正常に初期化されたかを返す
	/// @return 初期化成功ならtrue（POSIX では常にtrue）
	[[nodiscard]]
	bool isWsaInitialized() const noexcept
	{
		return m_wsaGuard.isInitialized();
	}

	/// @brief リッスン中の実際のポート番号を取得する
	///
	/// listen(0)でOS自動割り当てされたポートを取得する場合に使用。
	/// @return ポート番号（リッスンしていない場合は0）
	[[nodiscard]]
	uint16_t getLocalPort() const noexcept
	{
		if (m_listenSocket == INVALID_SOCK) return 0;
		sockaddr_in addr{};
#ifdef _WIN32
		int addrLen = sizeof(addr);
#else
		socklen_t addrLen = sizeof(addr);
#endif
		if (::getsockname(m_listenSocket,
			reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0)
		{
			return ntohs(addr.sin_port);
		}
		return m_port;
	}

	/// @brief 指定ポートでリッスンを開始する
	/// @param port リッスンポート番号（0でOS自動割り当て）
	/// @return 成功すればtrue
	bool listen(std::uint16_t port) override
	{
		if (!m_wsaGuard.isInitialized()) return false;
		if (m_listenSocket != INVALID_SOCK) return false;

		m_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSocket == INVALID_SOCK) return false;

		/// SO_REUSEADDRを設定（テスト時のポート再利用のため）
		int optVal = 1;
		::setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
			reinterpret_cast<const char*>(&optVal), sizeof(optVal));

		/// 非ブロッキングに設定
		if (!setNonBlocking(m_listenSocket))
		{
			closeSocket(m_listenSocket);
			m_listenSocket = INVALID_SOCK;
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (::bind(m_listenSocket,
			reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = INVALID_SOCK;
			return false;
		}

		if (::listen(m_listenSocket, SOMAXCONN) == SOCK_ERR)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = INVALID_SOCK;
			return false;
		}

		m_listening = true;
		m_port = port;
		return true;
	}

	/// @brief 指定ホスト・ポートに接続する
	/// @param host ホスト名またはIPアドレス
	/// @param port ポート番号
	/// @return 成功すればtrue
	bool connect(std::string_view host, std::uint16_t port) override
	{
		if (!m_wsaGuard.isInitialized()) return false;

		SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCK) return false;

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		/// ホスト名を解決
		const std::string hostStr(host);
		if (inet_pton(AF_INET, hostStr.c_str(), &addr.sin_addr) != 1)
		{
			closeSocket(sock);
			return false;
		}

		/// ブロッキングモードで接続（完了後に非ブロッキングにする）
		if (::connect(sock,
			reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR)
		{
			closeSocket(sock);
			return false;
		}

		/// 非ブロッキングに設定
		if (!setNonBlocking(sock))
		{
			closeSocket(sock);
			return false;
		}

		const ConnectionId connId = ++m_nextConnectionId;

		{
			std::scoped_lock lock(m_mutex);
			m_connections.push_back({connId, sock, {}});

			NetworkMessage msg;
			msg.sender = connId;
			msg.header.type = static_cast<uint32_t>(NetworkEvent::Connected);
			m_incoming.push(std::move(msg));
		}

		return true;
	}

	/// @brief 指定接続を切断する
	/// @param id 切断する接続ID
	void disconnect(ConnectionId id) override
	{
		std::scoped_lock lock(m_mutex);
		for (auto it = m_connections.begin(); it != m_connections.end(); ++it)
		{
			if (it->connectionId == id)
			{
				closeSocket(it->socket);

				NetworkMessage msg;
				msg.sender = id;
				msg.header.type = static_cast<uint32_t>(
					NetworkEvent::Disconnected);
				m_incoming.push(std::move(msg));

				m_connections.erase(it);
				return;
			}
		}
	}

	/// @brief データを送信する（サイズプレフィクス付き）
	/// @param id 送信先接続ID
	/// @param data 送信データ
	void send(ConnectionId id,
		const std::vector<std::uint8_t>& data) override
	{
		std::scoped_lock lock(m_mutex);
		SocketHandle sock = INVALID_SOCK;

		for (const auto& conn : m_connections)
		{
			if (conn.connectionId == id)
			{
				sock = conn.socket;
				break;
			}
		}

		if (sock == INVALID_SOCK) return;

		/// パケットヘッダー送信（サイズプレフィクス）
		PacketHeader header;
		header.size = static_cast<uint32_t>(data.size());

		::send(sock, reinterpret_cast<const char*>(&header),
			static_cast<int>(sizeof(header)), 0);

		/// ペイロード送信
		if (data.empty()) return;

		int totalSent = 0;
		const int dataSize = static_cast<int>(data.size());
		while (totalSent < dataSize)
		{
			const int sent = ::send(sock,
				reinterpret_cast<const char*>(data.data() + totalSent),
				dataSize - totalSent, 0);
			if (sent == SOCK_ERR) break;
			totalSent += sent;
		}

		m_stats.bytesSent += data.size() + sizeof(PacketHeader);
		m_stats.packetsSent++;
	}

	/// @brief 受信キューからメッセージを取得する
	/// @return 受信メッセージのベクタ
	[[nodiscard]]
	std::vector<NetworkMessage> poll() override
	{
		/// 新しい接続をacceptする
		acceptNewConnections();

		/// 既存接続からデータを受信する
		receiveData();

		std::scoped_lock lock(m_mutex);
		std::vector<NetworkMessage> messages;
		while (!m_incoming.empty())
		{
			messages.push_back(std::move(m_incoming.front()));
			m_incoming.pop();
		}
		return messages;
	}

	/// @brief 指定接続がアクティブか判定する
	/// @param id 接続ID
	/// @return 接続中ならtrue
	[[nodiscard]]
	bool isConnected(ConnectionId id) const override
	{
		std::scoped_lock lock(m_mutex);
		for (const auto& conn : m_connections)
		{
			if (conn.connectionId == id)
			{
				return true;
			}
		}
		return false;
	}

	/// @brief ネットワーク統計を取得する
	/// @return 現在の統計情報
	[[nodiscard]]
	NetworkStats stats() const override
	{
		std::scoped_lock lock(m_mutex);
		return m_stats;
	}

	/// @brief リッスン中かどうかを返す
	/// @return リッスン中ならtrue
	[[nodiscard]]
	bool isListening() const noexcept
	{
		return m_listening;
	}

private:
	/// @brief 接続情報
	struct ConnectionInfo
	{
		ConnectionId connectionId{INVALID_CONNECTION}; ///< 接続ID
		SocketHandle socket{INVALID_SOCK};              ///< ソケット
		std::vector<uint8_t> recvBuffer;                ///< 受信バッファ
	};

	/// @brief 新しい接続を受け入れる
	void acceptNewConnections()
	{
		if (m_listenSocket == INVALID_SOCK) return;

		while (true)
		{
			sockaddr_in clientAddr{};
#ifdef _WIN32
			int addrLen = sizeof(clientAddr);
#else
			socklen_t addrLen = sizeof(clientAddr);
#endif
			SocketHandle clientSock = ::accept(m_listenSocket,
				reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);

			if (clientSock == INVALID_SOCK) break;

			(void)setNonBlocking(clientSock);

			const ConnectionId connId = ++m_nextConnectionId;

			std::scoped_lock lock(m_mutex);
			m_connections.push_back({connId, clientSock, {}});

			NetworkMessage msg;
			msg.sender = connId;
			msg.header.type = static_cast<uint32_t>(
				NetworkEvent::Connected);
			m_incoming.push(std::move(msg));
		}
	}

	/// @brief 既存接続からデータを受信する
	void receiveData()
	{
		std::scoped_lock lock(m_mutex);

		for (auto it = m_connections.begin();
			it != m_connections.end(); )
		{
			char tempBuf[4096];
			const int received = ::recv(
				it->socket, tempBuf, sizeof(tempBuf), 0);

			if (received > 0)
			{
				/// 受信バッファに追加
				it->recvBuffer.insert(it->recvBuffer.end(),
					tempBuf, tempBuf + received);

				/// フレームを処理する（不正パケット時は切断）
				if (!processFrames(*it))
				{
					NetworkMessage msg;
					msg.sender = it->connectionId;
					msg.header.type = static_cast<uint32_t>(
						NetworkEvent::Error);
					m_incoming.push(std::move(msg));
					closeSocket(it->socket);
					it = m_connections.erase(it);
					continue;
				}
				++it;
			}
			else if (received == 0)
			{
				/// 相手側が切断
				NetworkMessage msg;
				msg.sender = it->connectionId;
				msg.header.type = static_cast<uint32_t>(
					NetworkEvent::Disconnected);
				m_incoming.push(std::move(msg));

				closeSocket(it->socket);
				it = m_connections.erase(it);
			}
			else
			{
				const int err = lastSocketError();
				if (isWouldBlock(err))
				{
					/// データなし（非ブロッキングモードでは正常）
					++it;
				}
				else
				{
					/// エラー発生
					NetworkMessage msg;
					msg.sender = it->connectionId;
					msg.header.type = static_cast<uint32_t>(
						NetworkEvent::Error);
					m_incoming.push(std::move(msg));

					closeSocket(it->socket);
					it = m_connections.erase(it);
				}
			}
		}
	}

	/// @brief 最大パケットサイズ（1MB）
	static constexpr uint32_t kMaxPacketSize = 1024 * 1024;

	/// @brief 受信バッファからフレームを抽出する
	/// @param conn 接続情報
	/// @return パケットサイズ超過の場合 false（接続を切断すべき）
	[[nodiscard]] bool processFrames(ConnectionInfo& conn)
	{
		while (conn.recvBuffer.size() >= sizeof(PacketHeader))
		{
			PacketHeader header;
			std::memcpy(&header, conn.recvBuffer.data(), sizeof(header));

			/// パケットサイズ検証（メモリ枯渇攻撃対策）
			if (header.size > kMaxPacketSize)
			{
				return false;
			}

			const std::size_t totalSize =
				sizeof(PacketHeader) + header.size;
			if (conn.recvBuffer.size() < totalSize) break;

			/// ペイロードを抽出
			std::vector<uint8_t> payload(
				conn.recvBuffer.begin() + sizeof(PacketHeader),
				conn.recvBuffer.begin() + totalSize);

			/// 処理済みデータをバッファから除去
			conn.recvBuffer.erase(conn.recvBuffer.begin(),
				conn.recvBuffer.begin() + totalSize);

			NetworkMessage netMsg;
			netMsg.sender = conn.connectionId;
			netMsg.header = header;
			netMsg.payload = std::move(payload);
			m_incoming.push(std::move(netMsg));

			m_stats.bytesReceived += totalSize;
			m_stats.packetsReceived++;
		}
		return true;
	}

	/// @brief 全リソースを解放する
	void closeAll()
	{
		std::scoped_lock lock(m_mutex);

		for (auto& conn : m_connections)
		{
			closeSocket(conn.socket);
		}
		m_connections.clear();

		if (m_listenSocket != INVALID_SOCK)
		{
			closeSocket(m_listenSocket);
			m_listenSocket = INVALID_SOCK;
		}

		m_listening = false;

		while (!m_incoming.empty()) m_incoming.pop();
	}

	WsaGuard m_wsaGuard;                                   ///< WSA初期化ガード（POSIX ではno-op）
	SocketHandle m_listenSocket{INVALID_SOCK};             ///< リッスンソケット
	std::vector<ConnectionInfo> m_connections;             ///< 接続リスト
	std::queue<NetworkMessage> m_incoming;                 ///< 受信キュー
	mutable std::mutex m_mutex;                            ///< スレッド安全用ミューテックス
	std::atomic<uint32_t> m_nextConnectionId{0};           ///< 次の接続ID
	uint16_t m_port{0};                                    ///< リッスンポート
	bool m_listening{false};                               ///< リッスン中フラグ
	NetworkStats m_stats;                                  ///< ネットワーク統計
};

} // namespace mitiru::network
