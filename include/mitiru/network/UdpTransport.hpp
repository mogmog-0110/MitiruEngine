#pragma once

/// @file UdpTransport.hpp
/// @brief UDP ソケットベースのネットワークトランスポート実装（クロスプラットフォーム）
/// @details SocketCompatを通じて Windows (Winsock2) および POSIX (Linux/macOS) で動作する
///          非ブロッキングUDPトランスポート。
///          サーバーモード（listen/bind）とクライアントモード（connect）の両方をサポート。
///          UDP は信頼性保証なし（再送・順序保証なし）。
///          INetworkTransportインターフェースに準拠する。
///
/// @code
/// // サーバー側（バインド）
/// mitiru::network::UdpTransport server;
/// server.listen(12345);
///
/// // クライアント側
/// mitiru::network::UdpTransport client;
/// client.connect("127.0.0.1", 12345);
///
/// // データ送受信
/// std::vector<uint8_t> data = {1, 2, 3};
/// client.send(1, data);
/// auto msgs = server.poll();
/// @endcode

#include <mitiru/network/INetworkTransport.hpp>
#include <mitiru/network/SocketCompat.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::network
{

/// @brief UDP ソケットベースのトランスポート実装（クロスプラットフォーム）
///
/// SocketCompatの非ブロッキングUDPソケットを使用して通信を行う。
/// listen()でサーバー（バインド）モード、connect()でクライアントモードとして動作する。
/// UDP は信頼性・順序保証なし。
/// poll()で受信メッセージとイベントを取得する。
class UdpTransport final : public INetworkTransport
{
public:
	/// @brief コンストラクタ（WSA初期化を行う / POSIX ではno-op）
	UdpTransport() = default;

	/// @brief デストラクタ（リソースを解放する）
	~UdpTransport() override
	{
		closeAll();
	}

	/// コピー・ムーブ禁止（ソケットリソース管理のため）
	UdpTransport(const UdpTransport&) = delete;
	UdpTransport& operator=(const UdpTransport&) = delete;
	UdpTransport(UdpTransport&&) = delete;
	UdpTransport& operator=(UdpTransport&&) = delete;

	/// @brief ソケットシステムが正常に初期化されたかを返す
	/// @return 初期化成功ならtrue（POSIX では常にtrue）
	[[nodiscard]]
	bool isInitialized() const noexcept
	{
		return m_wsaGuard.isInitialized();
	}

	/// @brief リッスン中の実際のポート番号を取得する
	///
	/// listen(0)でOS自動割り当てされたポートを取得する場合に使用。
	/// @return ポート番号（バインドしていない場合は0）
	[[nodiscard]]
	uint16_t getLocalPort() const noexcept
	{
		if (m_socket == INVALID_SOCK) return 0;
		sockaddr_in addr{};
#ifdef _WIN32
		int addrLen = sizeof(addr);
#else
		socklen_t addrLen = sizeof(addr);
#endif
		if (::getsockname(m_socket,
			reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0)
		{
			return ntohs(addr.sin_port);
		}
		return 0;
	}

	/// @brief 指定ポートにバインドしてサーバーモードで起動する
	/// @param port バインドポート番号（0でOS自動割り当て）
	/// @return 成功すればtrue
	bool listen(std::uint16_t port) override
	{
		if (!m_wsaGuard.isInitialized()) return false;
		if (m_socket != INVALID_SOCK) return false;

		m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_socket == INVALID_SOCK) return false;

		/// SO_REUSEADDRを設定（テスト時のポート再利用のため）
		int optVal = 1;
		::setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR,
			reinterpret_cast<const char*>(&optVal), sizeof(optVal));

		/// 非ブロッキングに設定
		if (!setNonBlocking(m_socket))
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCK;
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (::bind(m_socket,
			reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR)
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCK;
			return false;
		}

		m_serverMode = true;
		return true;
	}

	/// @brief 指定ホスト・ポートへのデフォルト送信先を設定する（クライアントモード）
	/// @param host ホスト名またはIPアドレス
	/// @param port ポート番号
	/// @return 成功すればtrue
	bool connect(std::string_view host, std::uint16_t port) override
	{
		if (!m_wsaGuard.isInitialized()) return false;
		if (m_socket != INVALID_SOCK) return false;

		m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_socket == INVALID_SOCK) return false;

		/// 非ブロッキングに設定
		if (!setNonBlocking(m_socket))
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCK;
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		const std::string hostStr(host);
		if (inet_pton(AF_INET, hostStr.c_str(), &addr.sin_addr) != 1)
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCK;
			return false;
		}

		/// UDPのconnect()はデフォルト送信先を設定するだけ（実際の接続は確立しない）
		if (::connect(m_socket,
			reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR)
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCK;
			return false;
		}

		/// クライアントモードでは固定ConnectionIdを使う
		const ConnectionId connId = kClientConnectionId;

		{
			std::scoped_lock lock(m_mutex);
			m_clientConnected = true;

			NetworkMessage msg;
			msg.sender = connId;
			msg.header.type = static_cast<uint32_t>(NetworkEvent::Connected);
			m_incoming.push(std::move(msg));
		}

		m_serverMode = false;
		return true;
	}

	/// @brief 指定接続を切断する（サーバーモードで特定クライアントを削除）
	/// @param id 切断する接続ID
	void disconnect(ConnectionId id) override
	{
		std::scoped_lock lock(m_mutex);

		if (!m_serverMode)
		{
			/// クライアントモード：ソケットを閉じる
			if (m_clientConnected && id == kClientConnectionId)
			{
				closeSocket(m_socket);
				m_socket = INVALID_SOCK;
				m_clientConnected = false;

				NetworkMessage msg;
				msg.sender = id;
				msg.header.type = static_cast<uint32_t>(
					NetworkEvent::Disconnected);
				m_incoming.push(std::move(msg));
			}
			return;
		}

		/// サーバーモード：接続マップから削除
		for (auto it = m_remoteClients.begin();
			it != m_remoteClients.end(); ++it)
		{
			if (it->second == id)
			{
				NetworkMessage msg;
				msg.sender = id;
				msg.header.type = static_cast<uint32_t>(
					NetworkEvent::Disconnected);
				m_incoming.push(std::move(msg));

				m_remoteClients.erase(it);
				return;
			}
		}
	}

	/// @brief データを送信する
	/// @param id 送信先接続ID（サーバーモードではクライアントID、クライアントモードでは無視）
	/// @param data 送信データ
	void send(ConnectionId id,
		const std::vector<std::uint8_t>& data) override
	{
		if (m_socket == INVALID_SOCK) return;

		std::scoped_lock lock(m_mutex);

		if (!m_serverMode)
		{
			/// クライアントモード：connect()で設定した送信先に送る
			if (!m_clientConnected) return;

			const int sent = ::send(m_socket,
				reinterpret_cast<const char*>(data.data()),
				static_cast<int>(data.size()), 0);

			if (sent != SOCK_ERR)
			{
				m_stats.bytesSent += static_cast<uint64_t>(sent);
				m_stats.packetsSent++;
			}
			return;
		}

		/// サーバーモード：接続IDに対応するリモートアドレスを検索してsendto
		for (const auto& [addrKey, connId] : m_remoteClients)
		{
			if (connId == id)
			{
				sockaddr_in dest{};
				addrKey.toSockaddr(dest);

				const int sent = ::sendto(m_socket,
					reinterpret_cast<const char*>(data.data()),
					static_cast<int>(data.size()), 0,
					reinterpret_cast<const sockaddr*>(&dest),
					sizeof(dest));

				if (sent != SOCK_ERR)
				{
					m_stats.bytesSent += static_cast<uint64_t>(sent);
					m_stats.packetsSent++;
				}
				return;
			}
		}
	}

	/// @brief 受信キューからメッセージを取得する
	/// @return 受信メッセージのベクタ
	[[nodiscard]]
	std::vector<NetworkMessage> poll() override
	{
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

		if (!m_serverMode)
		{
			return m_clientConnected && (id == kClientConnectionId);
		}

		for (const auto& [addrKey, connId] : m_remoteClients)
		{
			if (connId == id) return true;
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

	/// @brief サーバーモードかどうかを返す
	/// @return サーバーモードならtrue
	[[nodiscard]]
	bool isServerMode() const noexcept
	{
		return m_serverMode;
	}

private:
	/// @brief クライアントモードで使用する固定ConnectionId
	static constexpr ConnectionId kClientConnectionId = 1;

	/// @brief リモートアドレスキー（MapのKey用）
	struct AddrKey
	{
		uint32_t addr{0};   ///< ネットワークバイトオーダーのIPアドレス
		uint16_t port{0};   ///< ネットワークバイトオーダーのポート番号

		[[nodiscard]] bool operator<(const AddrKey& other) const noexcept
		{
			if (addr != other.addr) return addr < other.addr;
			return port < other.port;
		}

		[[nodiscard]] bool operator==(const AddrKey& other) const noexcept
		{
			return addr == other.addr && port == other.port;
		}

		/// @brief sockaddr_in からAddrKeyを生成する
		[[nodiscard]] static AddrKey fromSockaddr(const sockaddr_in& sa) noexcept
		{
			return AddrKey{sa.sin_addr.s_addr, sa.sin_port};
		}

		/// @brief AddrKey から sockaddr_in を生成する
		void toSockaddr(sockaddr_in& sa) const noexcept
		{
			sa = {};
			sa.sin_family = AF_INET;
			sa.sin_addr.s_addr = addr;
			sa.sin_port = port;
		}
	};

	/// @brief データを受信してキューに積む（非ブロッキング）
	void receiveData()
	{
		if (m_socket == INVALID_SOCK) return;

		char tempBuf[65535];

		while (true)
		{
			sockaddr_in senderAddr{};
#ifdef _WIN32
			int addrLen = sizeof(senderAddr);
#else
			socklen_t addrLen = sizeof(senderAddr);
#endif
			const int received = ::recvfrom(m_socket, tempBuf,
				static_cast<int>(sizeof(tempBuf)), 0,
				reinterpret_cast<sockaddr*>(&senderAddr), &addrLen);

			if (received == SOCK_ERR)
			{
				if (isWouldBlock(lastSocketError())) break;
				/// それ以外のエラーはループを抜ける
				break;
			}

			if (received <= 0) break;

			std::scoped_lock lock(m_mutex);

			if (m_serverMode)
			{
				/// サーバーモード：送信元アドレスから接続IDを取得（新規なら登録）
				const AddrKey key = AddrKey::fromSockaddr(senderAddr);
				auto it = m_remoteClients.find(key);

				ConnectionId connId{};
				if (it == m_remoteClients.end())
				{
					connId = ++m_nextConnectionId;
					m_remoteClients.emplace(key, connId);

					/// 新規クライアントの接続イベントを発行
					NetworkMessage connMsg;
					connMsg.sender = connId;
					connMsg.header.type = static_cast<uint32_t>(
						NetworkEvent::Connected);
					m_incoming.push(std::move(connMsg));
				}
				else
				{
					connId = it->second;
				}

				NetworkMessage msg;
				msg.sender = connId;
				msg.header.type = static_cast<uint32_t>(NetworkEvent::DataReceived);
				msg.payload.assign(tempBuf, tempBuf + received);
				m_incoming.push(std::move(msg));

				m_stats.bytesReceived += static_cast<uint64_t>(received);
				m_stats.packetsReceived++;
			}
			else
			{
				/// クライアントモード：固定ConnectionIdで受信
				NetworkMessage msg;
				msg.sender = kClientConnectionId;
				msg.header.type = static_cast<uint32_t>(NetworkEvent::DataReceived);
				msg.payload.assign(tempBuf, tempBuf + received);
				m_incoming.push(std::move(msg));

				m_stats.bytesReceived += static_cast<uint64_t>(received);
				m_stats.packetsReceived++;
			}
		}
	}

	/// @brief 全リソースを解放する
	void closeAll()
	{
		std::scoped_lock lock(m_mutex);

		if (m_socket != INVALID_SOCK)
		{
			closeSocket(m_socket);
			m_socket = INVALID_SOCK;
		}

		m_remoteClients.clear();
		m_clientConnected = false;

		while (!m_incoming.empty()) m_incoming.pop();
	}

	WsaGuard m_wsaGuard;                                       ///< WSA初期化ガード（POSIX ではno-op）
	SocketHandle m_socket{INVALID_SOCK};                       ///< UDPソケット
	std::map<AddrKey, ConnectionId> m_remoteClients;           ///< リモートクライアント管理（サーバーモード用）
	std::queue<NetworkMessage> m_incoming;                     ///< 受信キュー
	mutable std::mutex m_mutex;                                ///< スレッド安全用ミューテックス
	std::atomic<uint32_t> m_nextConnectionId{0};               ///< 次の接続ID
	NetworkStats m_stats;                                      ///< ネットワーク統計
	bool m_serverMode{false};                                  ///< サーバーモードフラグ
	bool m_clientConnected{false};                             ///< クライアント接続済みフラグ
};

} // namespace mitiru::network
