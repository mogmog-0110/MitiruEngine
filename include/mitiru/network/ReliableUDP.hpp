#pragma once

/// @file ReliableUDP.hpp
/// @brief GameNetworkingSocketsブリッジ（信頼性付きUDPトランスポート）
/// @details GameNetworkingSockets (GNS) が利用可能な場合は実装に委譲し、
///          利用不可の場合はNullスタブで動作する。IReliableTransportインターフェースにより
///          トランスポート層を差し替え可能にする。
///
/// @code
/// auto transport = mitiru::network::createReliableTransport();
/// transport->setOnConnected([](ConnectionId id) {
///     // 接続完了時の処理
/// });
/// transport->listen(27015);
/// // 毎フレーム:
/// transport->poll();
/// auto packets = transport->receive();
/// @endcode

#include <mitiru/network/NetworkTypes.hpp>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::network
{

/// @brief 接続の状態
enum class ConnectionStatus : std::uint8_t
{
	Connecting = 0,  ///< 接続試行中
	Connected,       ///< 接続確立済み
	Disconnected,    ///< 切断済み
	Failed,          ///< 接続失敗
};

/// @brief 受信パケット
struct ReceivedPacket
{
	ConnectionId connectionId = INVALID_CONNECTION; ///< 送信元の接続ID
	std::vector<std::uint8_t> data;                 ///< ペイロードデータ
	std::uint32_t size = 0;                         ///< ペイロードサイズ（バイト）
	bool reliable = true;                           ///< 信頼性ありで受信したか
};

/// @brief 信頼性付きUDPトランスポートインターフェース
/// @details GameNetworkingSockets等のリライアブルUDP層を抽象化する。
///          listen/connectの両方をサポートし、信頼性あり・なしの送信を選択できる。
class IReliableTransport
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IReliableTransport() = default;

	// ── 接続管理 ──

	/// @brief 指定ポートでリッスンを開始する（サーバー側）
	/// @param port 待ち受けポート番号
	/// @return 成功なら true
	virtual bool listen(std::uint16_t port) = 0;

	/// @brief リモートホストに接続する（クライアント側）
	/// @param address ホスト名またはIPアドレス
	/// @param port 接続先ポート番号
	/// @return 接続ID（失敗時はINVALID_CONNECTION）
	virtual ConnectionId connect(std::string_view address, std::uint16_t port) = 0;

	/// @brief 指定接続を切断する
	/// @param connId 切断する接続ID
	virtual void disconnect(ConnectionId connId) = 0;

	// ── データ送受信 ──

	/// @brief データを送信する
	/// @param connId 送信先の接続ID
	/// @param data 送信データへのポインタ
	/// @param size 送信データサイズ（バイト）
	/// @param reliable 信頼性あり送信（再送あり）にするか
	virtual void send(ConnectionId connId, const void* data,
	                  std::uint32_t size, bool reliable = true) = 0;

	/// @brief 受信済みパケットを取得する
	/// @return 受信パケットの一覧（キューが空なら空のvector）
	[[nodiscard]] virtual std::vector<ReceivedPacket> receive() = 0;

	// ── ポーリング ──

	/// @brief ネットワークイベントを処理する（毎フレーム呼ぶ）
	/// @details 内部キューの処理、コールバックの発火、タイムアウト検出等を行う。
	virtual void poll() = 0;

	// ── 状態照会 ──

	/// @brief 指定接続の状態を取得する
	/// @param connId 接続ID
	/// @return 接続状態
	[[nodiscard]] virtual ConnectionStatus getConnectionStatus(
		ConnectionId connId) const = 0;

	// ── コールバック ──

	/// @brief 接続確立時のコールバックを設定する
	/// @param callback ConnectionIdを引数に取るコールバック
	void setOnConnected(std::function<void(ConnectionId)> callback)
	{
		m_onConnected = std::move(callback);
	}

	/// @brief 切断時のコールバックを設定する
	/// @param callback ConnectionIdを引数に取るコールバック
	void setOnDisconnected(std::function<void(ConnectionId)> callback)
	{
		m_onDisconnected = std::move(callback);
	}

	/// @brief データ受信時のコールバックを設定する
	/// @param callback ReceivedPacketを引数に取るコールバック
	void setOnData(std::function<void(const ReceivedPacket&)> callback)
	{
		m_onData = std::move(callback);
	}

protected:
	/// @brief 接続確立コールバックを発火する
	void fireOnConnected(ConnectionId id) const
	{
		if (m_onConnected) m_onConnected(id);
	}

	/// @brief 切断コールバックを発火する
	void fireOnDisconnected(ConnectionId id) const
	{
		if (m_onDisconnected) m_onDisconnected(id);
	}

	/// @brief データ受信コールバックを発火する
	void fireOnData(const ReceivedPacket& packet) const
	{
		if (m_onData) m_onData(packet);
	}

	std::function<void(ConnectionId)> m_onConnected;            ///< 接続コールバック
	std::function<void(ConnectionId)> m_onDisconnected;         ///< 切断コールバック
	std::function<void(const ReceivedPacket&)> m_onData;        ///< データ受信コールバック
};

// ═════════════════════════════════════════════════════════════
// GNS実装（GameNetworkingSockets利用可能時）
// ═════════════════════════════════════════════════════════════

#ifdef MITIRU_HAS_GNS

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

/// @brief GameNetworkingSocketsを使った信頼性付きUDPトランスポート
/// @details Valve製のGameNetworkingSocketsライブラリをラップし、
///          IReliableTransportインターフェースで提供する。
class GnsReliableTransport final : public IReliableTransport
{
public:
	GnsReliableTransport()
	{
		SteamDatagramErrMsg errMsg;
		if (!GameNetworkingSockets_Init(nullptr, errMsg))
		{
			m_initError = errMsg;
			return;
		}
		m_interface = SteamNetworkingSockets();
		m_initialized = true;
		SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(
			&GnsReliableTransport::onConnectionStatusChanged);
	}

	~GnsReliableTransport() override
	{
		if (!m_initialized) return;
		if (m_listenSocket != k_HSteamListenSocket_Invalid)
		{
			m_interface->CloseListenSocket(m_listenSocket);
		}
		if (m_pollGroup != k_HSteamNetPollGroup_Invalid)
		{
			m_interface->DestroyPollGroup(m_pollGroup);
		}
		for (const auto& [id, conn] : m_connections)
		{
			m_interface->CloseConnection(conn, 0, nullptr, false);
		}
		GameNetworkingSockets_Kill();
	}

	GnsReliableTransport(const GnsReliableTransport&) = delete;
	GnsReliableTransport& operator=(const GnsReliableTransport&) = delete;
	GnsReliableTransport(GnsReliableTransport&&) = delete;
	GnsReliableTransport& operator=(GnsReliableTransport&&) = delete;

	bool listen(std::uint16_t port) override
	{
		if (!m_initialized) return false;
		SteamNetworkingIPAddr addr{};
		addr.Clear();
		addr.m_port = port;
		m_listenSocket = m_interface->CreateListenSocketIP(addr, 0, nullptr);
		m_pollGroup = m_interface->CreatePollGroup();
		return m_listenSocket != k_HSteamListenSocket_Invalid;
	}

	ConnectionId connect(std::string_view address, std::uint16_t port) override
	{
		if (!m_initialized) return INVALID_CONNECTION;
		SteamNetworkingIPAddr addr{};
		addr.Clear();
		const std::string addrStr(address);
		if (!addr.ParseString(addrStr.c_str())) return INVALID_CONNECTION;
		addr.m_port = port;
		HSteamNetConnection conn = m_interface->ConnectByIPAddress(addr, 0, nullptr);
		if (conn == k_HSteamNetConnection_Invalid) return INVALID_CONNECTION;

		// クライアント側でもpoll groupを作成し、接続を追加する
		if (m_pollGroup == k_HSteamNetPollGroup_Invalid)
			m_pollGroup = m_interface->CreatePollGroup();
		m_interface->SetConnectionPollGroup(conn, m_pollGroup);
		m_interface->SetConnectionUserData(conn,
			static_cast<int64>(reinterpret_cast<intptr_t>(this)));

		const ConnectionId id = ++m_nextId;
		m_connections[id] = conn;
		m_connToId[conn] = id;
		m_statuses[id] = ConnectionStatus::Connecting;
		return id;
	}

	void disconnect(ConnectionId connId) override
	{
		auto it = m_connections.find(connId);
		if (it == m_connections.end()) return;
		m_interface->CloseConnection(it->second, 0, nullptr, true);
		m_connToId.erase(it->second);
		m_connections.erase(it);
		m_statuses.erase(connId);
		fireOnDisconnected(connId);
	}

	void send(ConnectionId connId, const void* data,
	          std::uint32_t size, bool reliable) override
	{
		static constexpr std::uint32_t kMaxPacketSize = 512 * 1024;
		if (data == nullptr || size == 0 || size > kMaxPacketSize) return;
		auto it = m_connections.find(connId);
		if (it == m_connections.end()) return;
		const int flags = reliable
			? k_nSteamNetworkingSend_Reliable
			: k_nSteamNetworkingSend_Unreliable;
		const EResult res = m_interface->SendMessageToConnection(
			it->second, data, size, flags, nullptr);
		if (res != k_EResultOK)
		{
			// 送信失敗時はログ等で対応可能（現在はサイレント）
		}
	}

	[[nodiscard]] std::vector<ReceivedPacket> receive() override
	{
		std::vector<ReceivedPacket> result;
		ISteamNetworkingMessage* msgs[64];
		const int count = m_interface->ReceiveMessagesOnPollGroup(
			m_pollGroup, msgs, 64);
		static constexpr std::uint32_t kMaxPacketSize = 512 * 1024;
		for (int i = 0; i < count; ++i)
		{
			auto* msg = msgs[i];
			auto idIt = m_connToId.find(msg->m_conn);
			const ConnectionId cid = (idIt != m_connToId.end())
				? idIt->second : INVALID_CONNECTION;
			const auto msgSize = static_cast<std::uint32_t>(msg->m_cbSize);
			if (msgSize == 0 || msgSize > kMaxPacketSize)
			{
				msg->Release();
				continue;
			}
			ReceivedPacket pkt;
			pkt.connectionId = cid;
			pkt.size = msgSize;
			pkt.data.resize(pkt.size);
			std::memcpy(pkt.data.data(), msg->m_pData, pkt.size);
			pkt.reliable = true; // GNSでは受信時に区別不要
			result.push_back(std::move(pkt));
			fireOnData(result.back());
			msg->Release();
		}
		return result;
	}

	void poll() override
	{
		if (!m_initialized) return;
		m_interface->RunCallbacks();
	}

private:
	/// @brief GNS接続状態変化コールバック（静的エントリポイント）
	/// @details SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged
	///          に登録するC-style静的関数。m_nUserDataからthisポインタを復元して
	///          handleStatusChange()に委譲する。
	static void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		auto* self = reinterpret_cast<GnsReliableTransport*>(
			static_cast<intptr_t>(pInfo->m_info.m_nUserData));
		if (self) self->handleStatusChange(pInfo);
	}

	/// @brief 接続状態変化のインスタンス処理
	void handleStatusChange(SteamNetConnectionStatusChangedCallback_t* pInfo)
	{
		const HSteamNetConnection conn = pInfo->m_hConn;
		auto idIt = m_connToId.find(conn);

		switch (pInfo->m_info.m_eState)
		{
		case k_ESteamNetworkingConnectionState_Connecting:
			// サーバー側の新規受信接続（クライアント側はconnect()でマップ済み）
			if (idIt == m_connToId.end())
			{
				if (m_interface->AcceptConnection(conn) != k_EResultOK)
				{
					m_interface->CloseConnection(conn, 0, nullptr, false);
					break;
				}
				m_interface->SetConnectionPollGroup(conn, m_pollGroup);
				m_interface->SetConnectionUserData(conn,
					static_cast<int64>(reinterpret_cast<intptr_t>(this)));
				const ConnectionId id = ++m_nextId;
				m_connections[id] = conn;
				m_connToId[conn] = id;
				m_statuses[id] = ConnectionStatus::Connecting;
			}
			break;

		case k_ESteamNetworkingConnectionState_Connected:
			if (idIt != m_connToId.end())
			{
				m_statuses[idIt->second] = ConnectionStatus::Connected;
				fireOnConnected(idIt->second);
			}
			break;

		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
			if (idIt != m_connToId.end())
			{
				const ConnectionId id = idIt->second;
				m_statuses[id] = (pInfo->m_info.m_eState ==
					k_ESteamNetworkingConnectionState_ClosedByPeer)
					? ConnectionStatus::Disconnected : ConnectionStatus::Failed;
				fireOnDisconnected(id);
				m_interface->CloseConnection(conn, 0, nullptr, false);
				m_connections.erase(id);
				m_connToId.erase(conn);
				m_statuses.erase(id);
			}
			break;

		default:
			break;
		}
	}

public:

	[[nodiscard]] ConnectionStatus getConnectionStatus(
		ConnectionId connId) const override
	{
		auto it = m_statuses.find(connId);
		if (it != m_statuses.end()) return it->second;
		return ConnectionStatus::Disconnected;
	}

private:
	ISteamNetworkingSockets* m_interface = nullptr;
	HSteamListenSocket m_listenSocket = k_HSteamListenSocket_Invalid;
	HSteamNetPollGroup m_pollGroup = k_HSteamNetPollGroup_Invalid;
	std::unordered_map<ConnectionId, HSteamNetConnection> m_connections;
	std::unordered_map<HSteamNetConnection, ConnectionId> m_connToId;
	std::unordered_map<ConnectionId, ConnectionStatus> m_statuses;
	ConnectionId m_nextId = 0;
	bool m_initialized = false;
	std::string m_initError;
};

#endif // MITIRU_HAS_GNS

// ═════════════════════════════════════════════════════════════
// Null実装（ライブラリ不在時のスタブ）
// ═════════════════════════════════════════════════════════════

/// @brief 操作をログ出力するだけのスタブトランスポート
/// @details GameNetworkingSocketsが利用できない環境で使用する。
///          すべての操作はno-opで、receive()は常に空を返す。
class NullReliableTransport final : public IReliableTransport
{
public:
	bool listen([[maybe_unused]] std::uint16_t port) override
	{
		return false;
	}

	ConnectionId connect([[maybe_unused]] std::string_view address,
	                     [[maybe_unused]] std::uint16_t port) override
	{
		return INVALID_CONNECTION;
	}

	void disconnect([[maybe_unused]] ConnectionId connId) override {}

	void send([[maybe_unused]] ConnectionId connId,
	          [[maybe_unused]] const void* data,
	          [[maybe_unused]] std::uint32_t size,
	          [[maybe_unused]] bool reliable) override {}

	[[nodiscard]] std::vector<ReceivedPacket> receive() override
	{
		return {};
	}

	void poll() override {}

	[[nodiscard]] ConnectionStatus getConnectionStatus(
		[[maybe_unused]] ConnectionId connId) const override
	{
		return ConnectionStatus::Disconnected;
	}
};

// ═════════════════════════════════════════════════════════════
// ファクトリ関数
// ═════════════════════════════════════════════════════════════

/// @brief 環境に応じた信頼性付きUDPトランスポートを生成する
/// @return GNS利用可能時はGnsReliableTransport、それ以外はNullReliableTransport
[[nodiscard]] inline std::unique_ptr<IReliableTransport> createReliableTransport()
{
#ifdef MITIRU_HAS_GNS
	return std::make_unique<GnsReliableTransport>();
#else
	return std::make_unique<NullReliableTransport>();
#endif
}

} // namespace mitiru::network
