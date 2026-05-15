#pragma once

/// @file LobbySystem.hpp
/// @brief 高レベルロビーシステム
/// @details 既存のLobby(プレイヤー管理)をベースに、ルーム作成・検索・
///          LAN内ブロードキャスト検出・ゲーム開始調整を提供する。
///          NetworkSessionおよびINetworkTransportと連携して動作する。
///
/// @code
/// using namespace mitiru::network;
///
/// // ホスト側
/// LobbySystem lobby;
/// auto room = lobby.createRoom("MyGame", 4);
/// room->setReady(myId, true);
/// lobby.update(dt); // LANブロードキャスト送信
///
/// // クライアント側
/// LobbySystem lobby;
/// lobby.startDiscovery(27016);
/// lobby.update(dt);
/// auto rooms = lobby.discoveredRooms();
/// lobby.joinRoom(rooms[0].roomId, "PlayerName");
/// @endcode

#include <mitiru/network/Lobby.hpp>
#include <mitiru/network/NetworkTypes.hpp>
#include <mitiru/network/SocketCompat.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::network
{

/// @brief ルーム状態
enum class RoomState : std::uint8_t
{
	Waiting   = 0,  ///< プレイヤー待機中
	Starting  = 1,  ///< ゲーム開始処理中
	InGame    = 2,  ///< ゲーム進行中
	Closed    = 3,  ///< クローズ済み
};

/// @brief ルーム情報
struct RoomInfo
{
	std::uint32_t roomId = 0;           ///< ルームID
	std::string name;                    ///< ルーム名
	std::string hostAddress;             ///< ホストアドレス
	std::uint16_t hostPort = 0;          ///< ホストポート
	std::uint32_t currentPlayers = 0;    ///< 現在のプレイヤー数
	std::uint32_t maxPlayers = 4;        ///< 最大プレイヤー数
	RoomState state = RoomState::Waiting;///< ルーム状態
	std::string gameMode;                ///< ゲームモード名
	float lastSeenTime = 0.0f;          ///< 最後に検出された時刻

	/// @brief 満員かどうかを返す
	[[nodiscard]] bool isFull() const noexcept
	{
		return currentPlayers >= maxPlayers;
	}

	/// @brief 参加可能かどうかを返す
	[[nodiscard]] bool isJoinable() const noexcept
	{
		return !isFull() && state == RoomState::Waiting;
	}

	/// @brief JSON にシリアライズする
	[[nodiscard]] nlohmann::json toJson() const
	{
		return {
			{"roomId", roomId},
			{"name", name},
			{"hostAddress", hostAddress},
			{"hostPort", hostPort},
			{"currentPlayers", currentPlayers},
			{"maxPlayers", maxPlayers},
			{"state", static_cast<int>(state)},
			{"gameMode", gameMode}
		};
	}

	/// @brief JSON からデシリアライズする
	[[nodiscard]] static std::optional<RoomInfo> fromJson(const nlohmann::json& j)
	{
		if (!j.contains("roomId") || !j.contains("name")) return std::nullopt;

		RoomInfo info;
		info.roomId = j["roomId"].get<std::uint32_t>();
		info.name = j["name"].get<std::string>();
		info.hostAddress = j.value("hostAddress", std::string{});
		info.hostPort = j.value("hostPort", std::uint16_t{0});
		info.currentPlayers = j.value("currentPlayers", std::uint32_t{0});
		info.maxPlayers = j.value("maxPlayers", std::uint32_t{4});
		info.state = static_cast<RoomState>(j.value("state", 0));
		info.gameMode = j.value("gameMode", std::string{});
		return info;
	}
};

/// @brief ロビーイベント種別
enum class LobbyEvent : std::uint8_t
{
	RoomCreated      = 0,  ///< ルーム作成
	RoomDestroyed    = 1,  ///< ルーム破棄
	PlayerJoined     = 2,  ///< プレイヤー参加
	PlayerLeft       = 3,  ///< プレイヤー離脱
	PlayerReady      = 4,  ///< プレイヤー準備完了
	AllReady         = 5,  ///< 全プレイヤー準備完了
	GameStarting     = 6,  ///< ゲーム開始
	RoomDiscovered   = 7,  ///< ルーム検出（LAN）
};

/// @brief ロビーイベント通知
struct LobbyEventInfo
{
	LobbyEvent event = LobbyEvent::RoomCreated;
	std::uint32_t roomId = 0;
	ConnectionId playerId = INVALID_CONNECTION;
	std::string detail;
};

/// @brief ロビーシステム設定
struct LobbyConfig
{
	std::uint16_t discoveryPort = 27016;     ///< LAN検出用ポート
	float broadcastIntervalSec = 2.0f;       ///< ブロードキャスト間隔（秒）
	float roomTimeoutSec = 10.0f;            ///< ルーム検出タイムアウト（秒）
	std::string discoveryMagic = "MITIRU_LOBBY_V1"; ///< 検出パケット識別子
};

/// @brief 高レベルロビーシステム
/// @details ルーム管理・LAN検出・準備状態管理・ゲーム開始調整を統合する。
class LobbySystem
{
public:
	/// @brief イベントコールバック型
	using EventCallback = std::function<void(const LobbyEventInfo&)>;

	/// @brief ゲーム開始コールバック型
	using GameStartCallback = std::function<void(const RoomInfo&, const Lobby&)>;

	// ── ルーム管理 ──

	/// @brief ルームを作成する（ホスト側）
	/// @param name ルーム名
	/// @param maxPlayers 最大プレイヤー数
	/// @param gameMode ゲームモード名
	/// @return 作成されたルームのID
	std::uint32_t createRoom(std::string_view name,
	                         std::uint32_t maxPlayers = 4,
	                         std::string_view gameMode = "")
	{
		const std::uint32_t roomId = ++m_nextRoomId;

		RoomInfo room;
		room.roomId = roomId;
		room.name = std::string(name);
		room.maxPlayers = maxPlayers;
		room.gameMode = std::string(gameMode);
		room.state = RoomState::Waiting;
		room.currentPlayers = 0;

		m_hostedRooms[roomId] = room;
		m_lobbies[roomId] = Lobby{};

		fireEvent({LobbyEvent::RoomCreated, roomId, INVALID_CONNECTION,
			std::string(name)});

		return roomId;
	}

	/// @brief ルームを破棄する
	/// @param roomId ルームID
	void destroyRoom(std::uint32_t roomId)
	{
		m_hostedRooms.erase(roomId);
		m_lobbies.erase(roomId);
		fireEvent({LobbyEvent::RoomDestroyed, roomId, INVALID_CONNECTION, ""});
	}

	/// @brief プレイヤーをルームに追加する
	/// @param roomId ルームID
	/// @param playerId 接続ID
	/// @param playerName プレイヤー名
	/// @return 成功なら true
	bool joinRoom(std::uint32_t roomId, ConnectionId playerId,
	              std::string_view playerName)
	{
		auto roomIt = m_hostedRooms.find(roomId);
		if (roomIt == m_hostedRooms.end()) return false;
		if (!roomIt->second.isJoinable()) return false;

		auto lobbyIt = m_lobbies.find(roomId);
		if (lobbyIt == m_lobbies.end()) return false;

		lobbyIt->second.addPlayer(playerId, std::string(playerName));
		roomIt->second.currentPlayers =
			static_cast<std::uint32_t>(lobbyIt->second.playerCount());

		fireEvent({LobbyEvent::PlayerJoined, roomId, playerId,
			std::string(playerName)});

		return true;
	}

	/// @brief プレイヤーをルームから除去する
	/// @param roomId ルームID
	/// @param playerId 接続ID
	void leaveRoom(std::uint32_t roomId, ConnectionId playerId)
	{
		auto lobbyIt = m_lobbies.find(roomId);
		if (lobbyIt == m_lobbies.end()) return;

		lobbyIt->second.removePlayer(playerId);

		auto roomIt = m_hostedRooms.find(roomId);
		if (roomIt != m_hostedRooms.end())
		{
			roomIt->second.currentPlayers =
				static_cast<std::uint32_t>(lobbyIt->second.playerCount());
		}

		fireEvent({LobbyEvent::PlayerLeft, roomId, playerId, ""});
	}

	/// @brief プレイヤーの準備状態を設定する
	/// @param roomId ルームID
	/// @param playerId 接続ID
	/// @param ready 準備完了か
	void setReady(std::uint32_t roomId, ConnectionId playerId, bool ready)
	{
		auto lobbyIt = m_lobbies.find(roomId);
		if (lobbyIt == m_lobbies.end()) return;

		lobbyIt->second.setReady(playerId, ready);

		if (ready)
		{
			fireEvent({LobbyEvent::PlayerReady, roomId, playerId, ""});
		}

		if (lobbyIt->second.allReady())
		{
			fireEvent({LobbyEvent::AllReady, roomId, INVALID_CONNECTION, ""});
		}
	}

	/// @brief ゲーム開始を試みる
	/// @param roomId ルームID
	/// @return 全員準備完了でゲーム開始できたら true
	bool tryStartGame(std::uint32_t roomId)
	{
		auto lobbyIt = m_lobbies.find(roomId);
		if (lobbyIt == m_lobbies.end()) return false;
		if (!lobbyIt->second.allReady()) return false;

		auto roomIt = m_hostedRooms.find(roomId);
		if (roomIt == m_hostedRooms.end()) return false;

		roomIt->second.state = RoomState::Starting;
		fireEvent({LobbyEvent::GameStarting, roomId, INVALID_CONNECTION, ""});

		if (m_gameStartCallback)
		{
			m_gameStartCallback(roomIt->second, lobbyIt->second);
		}

		roomIt->second.state = RoomState::InGame;
		return true;
	}

	// ── LAN検出 ──

	/// @brief LAN検出用ソケットを開始する（受信側）
	/// @return 成功なら true
	bool startDiscovery()
	{
		if (m_discoverySocket != INVALID_SOCK) return true;

#ifdef _WIN32
		if (!m_wsaGuard.isInitialized()) return false;
#endif

		m_discoverySocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_discoverySocket == INVALID_SOCK) return false;

		// SO_REUSEADDR
		int optVal = 1;
		::setsockopt(m_discoverySocket, SOL_SOCKET, SO_REUSEADDR,
			reinterpret_cast<const char*>(&optVal), sizeof(optVal));

		// 非ブロッキング
		if (!setNonBlocking(m_discoverySocket))
		{
			closeSocket(m_discoverySocket);
			m_discoverySocket = INVALID_SOCK;
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(m_config.discoveryPort);

		if (::bind(m_discoverySocket,
			reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR)
		{
			closeSocket(m_discoverySocket);
			m_discoverySocket = INVALID_SOCK;
			return false;
		}

		return true;
	}

	/// @brief LAN検出を停止する
	void stopDiscovery()
	{
		if (m_discoverySocket != INVALID_SOCK)
		{
			closeSocket(m_discoverySocket);
			m_discoverySocket = INVALID_SOCK;
		}
	}

	/// @brief 毎フレーム更新
	/// @param deltaTimeSec フレーム経過時間（秒）
	void update(float deltaTimeSec)
	{
		m_elapsedTime += deltaTimeSec;
		m_broadcastAccumulator += deltaTimeSec;

		// ブロードキャスト送信（ホスト側）
		if (m_broadcastAccumulator >= m_config.broadcastIntervalSec)
		{
			m_broadcastAccumulator = 0.0f;
			broadcastRooms();
		}

		// 検出パケット受信
		receiveDiscovery();

		// タイムアウトしたルームを除去
		pruneStaleRooms();
	}

	/// @brief 検出されたルーム一覧を取得する
	[[nodiscard]] std::vector<RoomInfo> discoveredRooms() const
	{
		std::vector<RoomInfo> result;
		result.reserve(m_discoveredRooms.size());
		for (const auto& [id, room] : m_discoveredRooms)
		{
			result.push_back(room);
		}
		return result;
	}

	// ── ホストされたルーム ──

	/// @brief ホスト中のルーム情報を取得する
	/// @param roomId ルームID
	/// @return ルーム情報（存在しない場合は nullptr）
	[[nodiscard]] const RoomInfo* getRoom(std::uint32_t roomId) const
	{
		auto it = m_hostedRooms.find(roomId);
		if (it == m_hostedRooms.end()) return nullptr;
		return &it->second;
	}

	/// @brief ルームのロビーを取得する
	/// @param roomId ルームID
	/// @return ロビー（存在しない場合は nullptr）
	[[nodiscard]] const Lobby* getLobby(std::uint32_t roomId) const
	{
		auto it = m_lobbies.find(roomId);
		if (it == m_lobbies.end()) return nullptr;
		return &it->second;
	}

	/// @brief ホスト中のルーム数を返す
	[[nodiscard]] std::size_t hostedRoomCount() const noexcept
	{
		return m_hostedRooms.size();
	}

	// ── 設定・コールバック ──

	/// @brief 設定を取得する
	[[nodiscard]] LobbyConfig& config() noexcept { return m_config; }
	[[nodiscard]] const LobbyConfig& config() const noexcept { return m_config; }

	/// @brief イベントコールバックを設定する
	void setEventCallback(EventCallback callback)
	{
		m_eventCallback = std::move(callback);
	}

	/// @brief ゲーム開始コールバックを設定する
	void setGameStartCallback(GameStartCallback callback)
	{
		m_gameStartCallback = std::move(callback);
	}

	/// @brief デストラクタ
	~LobbySystem()
	{
		stopDiscovery();
		if (m_broadcastSocket != INVALID_SOCK)
		{
			closeSocket(m_broadcastSocket);
		}
	}

	LobbySystem() = default;
	LobbySystem(const LobbySystem&) = delete;
	LobbySystem& operator=(const LobbySystem&) = delete;
	LobbySystem(LobbySystem&&) = delete;
	LobbySystem& operator=(LobbySystem&&) = delete;

private:
	// ── LAN ブロードキャスト ──

	/// @brief ホスト中のルーム情報をブロードキャストする
	void broadcastRooms()
	{
		if (m_hostedRooms.empty()) return;

		// ブロードキャスト用ソケットを遅延初期化
		if (m_broadcastSocket == INVALID_SOCK)
		{
			m_broadcastSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (m_broadcastSocket == INVALID_SOCK) return;

			int optVal = 1;
			::setsockopt(m_broadcastSocket, SOL_SOCKET, SO_BROADCAST,
				reinterpret_cast<const char*>(&optVal), sizeof(optVal));

			(void)setNonBlocking(m_broadcastSocket);
		}

		// ルーム情報をJSONにまとめてブロードキャスト
		nlohmann::json packet;
		packet["magic"] = m_config.discoveryMagic;
		nlohmann::json roomsArray = nlohmann::json::array();
		for (const auto& [id, room] : m_hostedRooms)
		{
			if (room.state == RoomState::Waiting)
			{
				roomsArray.push_back(room.toJson());
			}
		}
		packet["rooms"] = roomsArray;

		const std::string data = packet.dump();

		sockaddr_in broadcastAddr{};
		broadcastAddr.sin_family = AF_INET;
		broadcastAddr.sin_port = htons(m_config.discoveryPort);
		broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

		::sendto(m_broadcastSocket, data.c_str(),
			static_cast<int>(data.size()), 0,
			reinterpret_cast<sockaddr*>(&broadcastAddr), sizeof(broadcastAddr));
	}

	/// @brief 検出パケットを受信する
	void receiveDiscovery()
	{
		if (m_discoverySocket == INVALID_SOCK) return;

		char buffer[4096];
		sockaddr_in senderAddr{};
#ifdef _WIN32
		int addrLen = sizeof(senderAddr);
#else
		socklen_t addrLen = sizeof(senderAddr);
#endif

		while (true)
		{
			const int received = ::recvfrom(m_discoverySocket, buffer,
				sizeof(buffer) - 1, 0,
				reinterpret_cast<sockaddr*>(&senderAddr), &addrLen);

			if (received <= 0) break;

			buffer[received] = '\0';

			try
			{
				auto packet = nlohmann::json::parse(buffer, buffer + received);
				if (!packet.contains("magic") ||
					packet["magic"].get<std::string>() != m_config.discoveryMagic)
				{
					continue;
				}

				// 送信元アドレスを取得
				char addrStr[INET_ADDRSTRLEN] = {};
				inet_ntop(AF_INET, &senderAddr.sin_addr, addrStr, sizeof(addrStr));

				if (packet.contains("rooms") && packet["rooms"].is_array())
				{
					for (const auto& roomJson : packet["rooms"])
					{
						auto roomOpt = RoomInfo::fromJson(roomJson);
						if (!roomOpt) continue;

						roomOpt->hostAddress = addrStr;
						roomOpt->lastSeenTime = m_elapsedTime;

						const bool isNew =
							m_discoveredRooms.find(roomOpt->roomId)
								== m_discoveredRooms.end();

						m_discoveredRooms[roomOpt->roomId] = *roomOpt;

						if (isNew)
						{
							fireEvent({LobbyEvent::RoomDiscovered,
								roomOpt->roomId, INVALID_CONNECTION,
								roomOpt->name});
						}
					}
				}
			}
			catch (const nlohmann::json::parse_error&)
			{
				// 不正パケットは無視
			}
		}
	}

	/// @brief タイムアウトした検出済みルームを除去する
	void pruneStaleRooms()
	{
		std::vector<std::uint32_t> stale;
		for (const auto& [id, room] : m_discoveredRooms)
		{
			if (m_elapsedTime - room.lastSeenTime > m_config.roomTimeoutSec)
			{
				stale.push_back(id);
			}
		}
		for (const auto id : stale)
		{
			m_discoveredRooms.erase(id);
		}
	}

	/// @brief イベントを発火する
	void fireEvent(LobbyEventInfo info) const
	{
		if (m_eventCallback)
		{
			m_eventCallback(info);
		}
	}

	// ── メンバー変数 ──

	LobbyConfig m_config;
	std::uint32_t m_nextRoomId = 0;

	std::unordered_map<std::uint32_t, RoomInfo> m_hostedRooms;
	std::unordered_map<std::uint32_t, Lobby> m_lobbies;
	std::unordered_map<std::uint32_t, RoomInfo> m_discoveredRooms;

	EventCallback m_eventCallback;
	GameStartCallback m_gameStartCallback;

	// LAN検出用ソケット
	WsaGuard m_wsaGuard;
	SocketHandle m_discoverySocket = INVALID_SOCK;
	SocketHandle m_broadcastSocket = INVALID_SOCK;

	float m_elapsedTime = 0.0f;
	float m_broadcastAccumulator = 0.0f;
};

} // namespace mitiru::network
