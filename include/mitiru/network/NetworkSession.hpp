#pragma once

/// @file NetworkSession.hpp
/// @brief ネットワークセッション管理
/// @details Client/Server/P2Pモードに対応したセッション管理。
///          INetworkTransport上に構築し、ピア管理・メッセージルーティング・
///          ハートビート・再接続を提供する。
///
/// @code
/// using namespace mitiru::network;
/// NetworkSession session;
/// session.hostAsServer(12345, transport);
///
/// // 毎フレーム
/// session.update(deltaTime);
/// session.broadcastGameState(stateJson);
///
/// // メッセージ受信
/// for (const auto& msg : session.receive()) { ... }
/// @endcode

#include <mitiru/network/NetworkMessage.hpp>
#include <mitiru/network/INetworkTransport.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::network
{

/// @brief セッションモード
enum class SessionMode : std::uint8_t
{
	None   = 0,  ///< 未初期化
	Server = 1,  ///< サーバーモード（ホスト）
	Client = 2,  ///< クライアントモード
	P2P    = 3,  ///< ピアツーピアモード
};

/// @brief ピア接続状態
enum class PeerState : std::uint8_t
{
	Connecting    = 0,  ///< 接続試行中
	Connected     = 1,  ///< 接続確立済み
	Disconnecting = 2,  ///< 切断処理中
	Disconnected  = 3,  ///< 切断済み
};

/// @brief ピア情報
struct PeerInfo
{
	ConnectionId connectionId = INVALID_CONNECTION;  ///< 接続ID
	std::string name;                                ///< 表示名
	PeerState state = PeerState::Disconnected;       ///< 接続状態
	float latencyMs = 0.0f;                          ///< 推定レイテンシ（ミリ秒）
	float lastHeartbeatTime = 0.0f;                  ///< 最後のハートビート受信時刻
	float connectedSince = 0.0f;                     ///< 接続確立時刻
	std::uint32_t nextSequence = 0;                  ///< 次のシーケンス番号
};

/// @brief セッション設定
struct SessionConfig
{
	float heartbeatIntervalSec = 1.0f;      ///< ハートビート送信間隔（秒）
	float timeoutSec = 10.0f;               ///< タイムアウト閾値（秒）
	float reconnectDelaySec = 3.0f;         ///< 再接続までの待機時間（秒）
	std::uint32_t maxPeers = 16;            ///< 最大ピア数
	bool autoReconnect = true;              ///< クライアント自動再接続
	std::string playerName = "Player";      ///< ローカルプレイヤー名
};

/// @brief セッションイベント種別
enum class SessionEvent : std::uint8_t
{
	PeerConnected    = 0,  ///< ピア接続
	PeerDisconnected = 1,  ///< ピア切断
	PeerTimedOut     = 2,  ///< ピアタイムアウト
	SessionStarted   = 3,  ///< セッション開始
	SessionEnded     = 4,  ///< セッション終了
};

/// @brief セッションイベント通知
struct SessionEventInfo
{
	SessionEvent event = SessionEvent::PeerConnected;
	ConnectionId peerId = INVALID_CONNECTION;
	std::string detail;
};

/// @brief ネットワークセッション管理クラス
/// @details トランスポート層を抽象化し、ゲームロジックに対して
///          統一的なマルチプレイヤーセッションAPIを提供する。
class NetworkSession
{
public:
	/// @brief イベントコールバック型
	using EventCallback = std::function<void(const SessionEventInfo&)>;

	/// @brief メッセージハンドラ型
	using MessageHandler = std::function<void(const GameMessage&)>;

	// ── セッション管理 ──

	/// @brief サーバーとしてセッションをホストする
	/// @param port リッスンポート
	/// @param transport トランスポート層（所有権を移動）
	/// @return 成功なら true
	bool hostAsServer(std::uint16_t port,
	                  std::shared_ptr<INetworkTransport> transport)
	{
		if (m_mode != SessionMode::None) return false;
		m_transport = std::move(transport);
		if (!m_transport->listen(port)) return false;

		m_mode = SessionMode::Server;
		m_elapsedTime = 0.0f;
		fireEvent({SessionEvent::SessionStarted, INVALID_CONNECTION, "server"});
		return true;
	}

	/// @brief クライアントとしてサーバーに接続する
	/// @param host サーバーアドレス
	/// @param port サーバーポート
	/// @param transport トランスポート層（所有権を移動）
	/// @return 成功なら true
	bool joinAsClient(std::string_view host, std::uint16_t port,
	                  std::shared_ptr<INetworkTransport> transport)
	{
		if (m_mode != SessionMode::None) return false;
		m_transport = std::move(transport);
		if (!m_transport->connect(host, port)) return false;

		m_mode = SessionMode::Client;
		m_serverHost = std::string(host);
		m_serverPort = port;
		m_elapsedTime = 0.0f;
		fireEvent({SessionEvent::SessionStarted, INVALID_CONNECTION, "client"});
		return true;
	}

	/// @brief P2Pモードで初期化する
	/// @param port ローカルポート
	/// @param transport トランスポート層（所有権を移動）
	/// @return 成功なら true
	bool startP2P(std::uint16_t port,
	              std::shared_ptr<INetworkTransport> transport)
	{
		if (m_mode != SessionMode::None) return false;
		m_transport = std::move(transport);
		if (!m_transport->listen(port)) return false;

		m_mode = SessionMode::P2P;
		m_elapsedTime = 0.0f;
		fireEvent({SessionEvent::SessionStarted, INVALID_CONNECTION, "p2p"});
		return true;
	}

	/// @brief セッションを終了する
	void shutdown()
	{
		// 全ピアに切断通知を送信
		for (const auto& [id, peer] : m_peers)
		{
			if (peer.state == PeerState::Connected)
			{
				sendTo(id, MessageFactory::disconnectNotice(
					m_localId, "session_shutdown"));
			}
		}

		m_peers.clear();
		m_incomingMessages.clear();
		m_transport.reset();
		m_mode = SessionMode::None;
		fireEvent({SessionEvent::SessionEnded, INVALID_CONNECTION, "shutdown"});
	}

	// ── 毎フレーム更新 ──

	/// @brief セッションを更新する（毎フレーム呼ぶ）
	/// @param deltaTimeSec フレーム経過時間（秒）
	void update(float deltaTimeSec)
	{
		if (!m_transport) return;

		m_elapsedTime += deltaTimeSec;
		m_heartbeatAccumulator += deltaTimeSec;

		// トランスポートからメッセージを取得
		pollTransport();

		// ハートビート送信
		if (m_heartbeatAccumulator >= m_config.heartbeatIntervalSec)
		{
			m_heartbeatAccumulator = 0.0f;
			sendHeartbeats();
		}

		// タイムアウト検出
		checkTimeouts();
	}

	// ── メッセージ送受信 ──

	/// @brief 特定ピアにメッセージを送信する
	/// @param peerId 送信先ピアID
	/// @param msg メッセージ
	void sendTo(ConnectionId peerId, GameMessage msg)
	{
		if (!m_transport) return;
		msg.senderId = m_localId;

		auto it = m_peers.find(peerId);
		if (it != m_peers.end())
		{
			msg.sequenceNumber = it->second.nextSequence++;
		}

		m_transport->send(peerId, msg.toBytes());
	}

	/// @brief 全接続済みピアにブロードキャストする
	/// @param msg メッセージ
	void broadcast(GameMessage msg)
	{
		for (const auto& [id, peer] : m_peers)
		{
			if (peer.state == PeerState::Connected)
			{
				sendTo(id, msg);
			}
		}
	}

	/// @brief ゲーム状態をブロードキャストする
	/// @param stateJson 状態データ
	void broadcastGameState(const nlohmann::json& stateJson)
	{
		broadcast(MessageFactory::gameState(m_localId, stateJson));
	}

	/// @brief 入力データを送信する（クライアント→サーバー）
	/// @param inputJson 入力データ
	void sendInput(const nlohmann::json& inputJson)
	{
		if (m_mode == SessionMode::Client)
		{
			// サーバーに送信（最初のピアがサーバー）
			for (const auto& [id, peer] : m_peers)
			{
				if (peer.state == PeerState::Connected)
				{
					sendTo(id, MessageFactory::input(m_localId, inputJson));
					break;
				}
			}
		}
		else
		{
			// サーバー/P2Pモードではブロードキャスト
			broadcast(MessageFactory::input(m_localId, inputJson));
		}
	}

	/// @brief 受信済みメッセージを取得する
	/// @return 受信メッセージキュー
	[[nodiscard]] std::vector<GameMessage> receive()
	{
		std::vector<GameMessage> result;
		std::swap(result, m_incomingMessages);
		return result;
	}

	// ── ピア管理 ──

	/// @brief ピア情報を取得する
	/// @param peerId ピアID
	/// @return ピア情報（存在しない場合は nullopt）
	[[nodiscard]] const PeerInfo* getPeer(ConnectionId peerId) const
	{
		auto it = m_peers.find(peerId);
		if (it == m_peers.end()) return nullptr;
		return &it->second;
	}

	/// @brief 全ピアのリストを取得する
	/// @return ピアID→PeerInfoマップの参照
	[[nodiscard]] const std::unordered_map<ConnectionId, PeerInfo>& peers() const noexcept
	{
		return m_peers;
	}

	/// @brief 接続中のピア数を取得する
	[[nodiscard]] std::size_t connectedPeerCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& [id, peer] : m_peers)
		{
			if (peer.state == PeerState::Connected) ++count;
		}
		return count;
	}

	/// @brief 特定ピアを切断する
	/// @param peerId 切断対象
	/// @param reason 理由
	void disconnectPeer(ConnectionId peerId, std::string_view reason = "")
	{
		auto it = m_peers.find(peerId);
		if (it == m_peers.end()) return;

		sendTo(peerId, MessageFactory::disconnectNotice(m_localId, reason));
		m_transport->disconnect(peerId);
		it->second.state = PeerState::Disconnected;

		fireEvent({SessionEvent::PeerDisconnected, peerId,
			std::string(reason)});
		m_peers.erase(it);
	}

	// ── 設定・コールバック ──

	/// @brief セッション設定を取得する
	[[nodiscard]] SessionConfig& config() noexcept { return m_config; }
	[[nodiscard]] const SessionConfig& config() const noexcept { return m_config; }

	/// @brief セッションモードを取得する
	[[nodiscard]] SessionMode mode() const noexcept { return m_mode; }

	/// @brief ローカルIDを設定する
	void setLocalId(ConnectionId id) noexcept { m_localId = id; }

	/// @brief ローカルIDを取得する
	[[nodiscard]] ConnectionId localId() const noexcept { return m_localId; }

	/// @brief イベントコールバックを設定する
	void setEventCallback(EventCallback callback)
	{
		m_eventCallback = std::move(callback);
	}

	/// @brief メッセージタイプ別ハンドラを登録する
	/// @param type 対象メッセージタイプ
	/// @param handler ハンドラ関数
	void onMessage(MessageType type, MessageHandler handler)
	{
		m_handlers[static_cast<std::uint8_t>(type)] = std::move(handler);
	}

	/// @brief セッションがアクティブか判定する
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_mode != SessionMode::None && m_transport != nullptr;
	}

	/// @brief 経過時間を取得する
	[[nodiscard]] float elapsedTime() const noexcept { return m_elapsedTime; }

private:
	// ── 内部処理 ──

	/// @brief トランスポートからメッセージをポーリングする
	void pollTransport()
	{
		auto rawMessages = m_transport->poll();
		for (auto& rawMsg : rawMessages)
		{
			// 低レベルイベント処理
			if (rawMsg.header.type == static_cast<std::uint32_t>(NetworkEvent::Connected))
			{
				handlePeerConnect(rawMsg.sender);
				continue;
			}
			if (rawMsg.header.type == static_cast<std::uint32_t>(NetworkEvent::Disconnected))
			{
				handlePeerDisconnect(rawMsg.sender);
				continue;
			}

			// GameMessage デシリアライズ
			auto gameMsg = GameMessage::fromBytes(rawMsg.payload);
			if (!gameMsg) continue;

			// ハートビート処理
			if (gameMsg->type == MessageType::Heartbeat)
			{
				handleHeartbeat(rawMsg.sender);
				continue;
			}

			// Connect メッセージ処理
			if (gameMsg->type == MessageType::Connect)
			{
				handleConnectMessage(rawMsg.sender, *gameMsg);
				continue;
			}

			// Disconnect メッセージ処理
			if (gameMsg->type == MessageType::Disconnect)
			{
				handlePeerDisconnect(rawMsg.sender);
				continue;
			}

			// 登録済みハンドラで処理
			auto handlerIt = m_handlers.find(static_cast<std::uint8_t>(gameMsg->type));
			if (handlerIt != m_handlers.end() && handlerIt->second)
			{
				handlerIt->second(*gameMsg);
			}

			// 受信キューに追加
			m_incomingMessages.push_back(std::move(*gameMsg));
		}
	}

	/// @brief ピア接続処理
	void handlePeerConnect(ConnectionId connId)
	{
		if (m_peers.size() >= m_config.maxPeers)
		{
			m_transport->disconnect(connId);
			return;
		}

		PeerInfo peer;
		peer.connectionId = connId;
		peer.state = PeerState::Connected;
		peer.lastHeartbeatTime = m_elapsedTime;
		peer.connectedSince = m_elapsedTime;
		m_peers[connId] = peer;

		fireEvent({SessionEvent::PeerConnected, connId, ""});
	}

	/// @brief Connect メッセージ処理（名前付き接続）
	void handleConnectMessage(ConnectionId connId, const GameMessage& msg)
	{
		auto it = m_peers.find(connId);
		if (it != m_peers.end())
		{
			auto payload = msg.jsonPayload();
			if (payload && payload->contains("name"))
			{
				it->second.name = (*payload)["name"].get<std::string>();
			}
		}
	}

	/// @brief ピア切断処理
	void handlePeerDisconnect(ConnectionId connId)
	{
		auto it = m_peers.find(connId);
		if (it == m_peers.end()) return;

		it->second.state = PeerState::Disconnected;
		fireEvent({SessionEvent::PeerDisconnected, connId, ""});
		m_peers.erase(it);
	}

	/// @brief ハートビート受信処理
	void handleHeartbeat(ConnectionId connId)
	{
		auto it = m_peers.find(connId);
		if (it == m_peers.end()) return;

		const float rtt = m_elapsedTime - it->second.lastHeartbeatTime;
		it->second.latencyMs = rtt * 1000.0f * 0.5f; // 片道推定
		it->second.lastHeartbeatTime = m_elapsedTime;
	}

	/// @brief 全ピアにハートビートを送信する
	void sendHeartbeats()
	{
		for (const auto& [id, peer] : m_peers)
		{
			if (peer.state == PeerState::Connected)
			{
				sendTo(id, MessageFactory::heartbeat(m_localId));
			}
		}
	}

	/// @brief タイムアウトしたピアを検出する
	void checkTimeouts()
	{
		std::vector<ConnectionId> timedOut;
		for (const auto& [id, peer] : m_peers)
		{
			if (peer.state != PeerState::Connected) continue;
			const float elapsed = m_elapsedTime - peer.lastHeartbeatTime;
			if (elapsed > m_config.timeoutSec)
			{
				timedOut.push_back(id);
			}
		}

		for (const auto id : timedOut)
		{
			auto it = m_peers.find(id);
			if (it == m_peers.end()) continue;

			it->second.state = PeerState::Disconnected;
			m_transport->disconnect(id);
			fireEvent({SessionEvent::PeerTimedOut, id, "heartbeat_timeout"});
			m_peers.erase(it);
		}
	}

	/// @brief イベントを発火する
	void fireEvent(SessionEventInfo info) const
	{
		if (m_eventCallback)
		{
			m_eventCallback(info);
		}
	}

	// ── メンバー変数 ──

	SessionMode m_mode = SessionMode::None;
	SessionConfig m_config;
	std::shared_ptr<INetworkTransport> m_transport;
	ConnectionId m_localId = INVALID_CONNECTION;

	std::unordered_map<ConnectionId, PeerInfo> m_peers;
	std::vector<GameMessage> m_incomingMessages;
	std::unordered_map<std::uint8_t, MessageHandler> m_handlers;

	EventCallback m_eventCallback;

	float m_elapsedTime = 0.0f;
	float m_heartbeatAccumulator = 0.0f;

	// クライアント再接続用
	std::string m_serverHost;
	std::uint16_t m_serverPort = 0;
};

} // namespace mitiru::network
