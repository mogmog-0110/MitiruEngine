#pragma once

/// @file NetworkMessage.hpp
/// @brief ネットワークメッセージプロトコル
/// @details メッセージ型定義、シリアライズ/デシリアライズ、
///          信頼性レベルの指定をサポートする高レベルメッセージ層。
///          NetworkTypesの低レベルNetworkMessageと区別するため、
///          GameMessageとして定義する。
///
/// @code
/// using namespace mitiru::network;
/// GameMessage msg;
/// msg.type = MessageType::GameState;
/// msg.senderId = 1;
/// msg.reliable = true;
/// msg.setJsonPayload({{"x", 10}, {"y", 20}});
///
/// auto bytes = msg.toBytes();
/// auto decoded = GameMessage::fromBytes(bytes);
/// @endcode

#include <mitiru/network/NetworkTypes.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::network
{

/// @brief 高レベルメッセージ種別
enum class MessageType : std::uint8_t
{
	Connect     = 0,  ///< 接続要求
	Disconnect  = 1,  ///< 切断通知
	Heartbeat   = 2,  ///< 生存確認
	GameState   = 3,  ///< ゲーム状態同期
	Input       = 4,  ///< プレイヤー入力
	RPC         = 5,  ///< リモートプロシージャコール
	LobbyAction = 6,  ///< ロビー操作
	Custom      = 255 ///< ユーザー定義
};

/// @brief 配送信頼性レベル
enum class DeliveryMode : std::uint8_t
{
	Unreliable       = 0,  ///< 信頼性なし（UDP的、ロスを許容）
	Reliable         = 1,  ///< 信頼性あり（再送あり）
	ReliableOrdered  = 2,  ///< 信頼性あり＋順序保証
};

/// @brief 高レベルゲームメッセージ
/// @details エンジンのメッセージプロトコル。バイナリシリアライズと
///          nlohmann/json ベースのペイロード操作をサポートする。
struct GameMessage
{
	MessageType type = MessageType::Custom;           ///< メッセージ種別
	ConnectionId senderId = INVALID_CONNECTION;       ///< 送信者ID
	std::uint32_t sequenceNumber = 0;                 ///< シーケンス番号
	std::uint64_t timestampMs = 0;                    ///< タイムスタンプ（ミリ秒）
	DeliveryMode delivery = DeliveryMode::Reliable;   ///< 配送モード
	std::uint8_t channel = 0;                         ///< チャンネル番号（論理的な分離）
	std::vector<std::uint8_t> payload;                ///< ペイロードデータ

	/// @brief 現在時刻をタイムスタンプに設定する
	void stampNow() noexcept
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		timestampMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}

	/// @brief JSON をペイロードに設定する
	/// @param j nlohmann::json オブジェクト
	void setJsonPayload(const nlohmann::json& j)
	{
		const std::string s = j.dump();
		payload.assign(s.begin(), s.end());
	}

	/// @brief ペイロードを JSON として解析する
	/// @return パース成功時は json、失敗時は nullopt
	[[nodiscard]] std::optional<nlohmann::json> jsonPayload() const
	{
		if (payload.empty()) return std::nullopt;
		try
		{
			const std::string s(payload.begin(), payload.end());
			return nlohmann::json::parse(s);
		}
		catch (const nlohmann::json::parse_error&)
		{
			return std::nullopt;
		}
	}

	/// @brief 文字列ペイロードを設定する
	/// @param text 文字列データ
	void setTextPayload(std::string_view text)
	{
		payload.assign(text.begin(), text.end());
	}

	/// @brief ペイロードを文字列として取得する
	/// @return 文字列
	[[nodiscard]] std::string textPayload() const
	{
		return {payload.begin(), payload.end()};
	}

	/// @brief バイナリにシリアライズする
	/// @return シリアライズされたバイト列
	///
	/// ワイヤフォーマット:
	///   [0]     magic (0x4D = 'M')
	///   [1]     version (1)
	///   [2]     MessageType
	///   [3]     DeliveryMode
	///   [4]     channel
	///   [5..8]  senderId (LE)
	///   [9..12] sequenceNumber (LE)
	///   [13..20] timestampMs (LE)
	///   [21..24] payloadSize (LE)
	///   [25..]  payload
	[[nodiscard]] std::vector<std::uint8_t> toBytes() const
	{
		std::vector<std::uint8_t> buf;
		buf.reserve(kHeaderSize + payload.size());

		// magic + version
		buf.push_back(kMagic);
		buf.push_back(kVersion);

		// type, delivery, channel
		buf.push_back(static_cast<std::uint8_t>(type));
		buf.push_back(static_cast<std::uint8_t>(delivery));
		buf.push_back(channel);

		// senderId (4 bytes LE)
		pushU32(buf, senderId);

		// sequenceNumber (4 bytes LE)
		pushU32(buf, sequenceNumber);

		// timestampMs (8 bytes LE)
		pushU64(buf, timestampMs);

		// payloadSize (4 bytes LE)
		pushU32(buf, static_cast<std::uint32_t>(payload.size()));

		// payload
		buf.insert(buf.end(), payload.begin(), payload.end());

		return buf;
	}

	/// @brief バイト列からデシリアライズする
	/// @param data バイト列
	/// @return パース成功時は GameMessage、失敗時は nullopt
	[[nodiscard]] static std::optional<GameMessage> fromBytes(
		const std::vector<std::uint8_t>& data)
	{
		if (data.size() < kHeaderSize) return std::nullopt;
		if (data[0] != kMagic) return std::nullopt;
		if (data[1] != kVersion) return std::nullopt;

		GameMessage msg;
		msg.type = static_cast<MessageType>(data[2]);
		msg.delivery = static_cast<DeliveryMode>(data[3]);
		msg.channel = data[4];
		msg.senderId = readU32(data, 5);
		msg.sequenceNumber = readU32(data, 9);
		msg.timestampMs = readU64(data, 13);

		const std::uint32_t payloadSize = readU32(data, 21);
		if (data.size() < kHeaderSize + payloadSize) return std::nullopt;

		msg.payload.assign(
			data.begin() + kHeaderSize,
			data.begin() + kHeaderSize + payloadSize);

		return msg;
	}

	/// @brief ヘッダーサイズ（バイト）
	static constexpr std::size_t kHeaderSize = 25;

private:
	static constexpr std::uint8_t kMagic = 0x4D;   ///< 'M' for Mitiru
	static constexpr std::uint8_t kVersion = 1;

	static void pushU32(std::vector<std::uint8_t>& buf, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
		{
			buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
		}
	}

	static void pushU64(std::vector<std::uint8_t>& buf, std::uint64_t v)
	{
		for (int i = 0; i < 8; ++i)
		{
			buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
		}
	}

	[[nodiscard]] static std::uint32_t readU32(
		const std::vector<std::uint8_t>& buf, std::size_t offset)
	{
		std::uint32_t v = 0;
		for (int i = 0; i < 4; ++i)
		{
			v |= static_cast<std::uint32_t>(buf[offset + i]) << (i * 8);
		}
		return v;
	}

	[[nodiscard]] static std::uint64_t readU64(
		const std::vector<std::uint8_t>& buf, std::size_t offset)
	{
		std::uint64_t v = 0;
		for (int i = 0; i < 8; ++i)
		{
			v |= static_cast<std::uint64_t>(buf[offset + i]) << (i * 8);
		}
		return v;
	}
};

/// @brief GameMessage用のファクトリヘルパー群
namespace MessageFactory
{

/// @brief ハートビートメッセージを生成する
/// @param senderId 送信者ID
/// @return ハートビートメッセージ
[[nodiscard]] inline GameMessage heartbeat(ConnectionId senderId)
{
	GameMessage msg;
	msg.type = MessageType::Heartbeat;
	msg.senderId = senderId;
	msg.delivery = DeliveryMode::Unreliable;
	msg.stampNow();
	return msg;
}

/// @brief 接続要求メッセージを生成する
/// @param playerName プレイヤー名
/// @return 接続要求メッセージ
[[nodiscard]] inline GameMessage connectRequest(std::string_view playerName)
{
	GameMessage msg;
	msg.type = MessageType::Connect;
	msg.delivery = DeliveryMode::Reliable;
	msg.setJsonPayload({{"name", std::string(playerName)}});
	msg.stampNow();
	return msg;
}

/// @brief 切断通知メッセージを生成する
/// @param senderId 送信者ID
/// @param reason 切断理由
/// @return 切断通知メッセージ
[[nodiscard]] inline GameMessage disconnectNotice(
	ConnectionId senderId, std::string_view reason = "")
{
	GameMessage msg;
	msg.type = MessageType::Disconnect;
	msg.senderId = senderId;
	msg.delivery = DeliveryMode::Reliable;
	if (!reason.empty())
	{
		msg.setJsonPayload({{"reason", std::string(reason)}});
	}
	msg.stampNow();
	return msg;
}

/// @brief ゲーム状態メッセージを生成する
/// @param senderId 送信者ID
/// @param stateJson 状態データ
/// @return ゲーム状態メッセージ
[[nodiscard]] inline GameMessage gameState(
	ConnectionId senderId, const nlohmann::json& stateJson)
{
	GameMessage msg;
	msg.type = MessageType::GameState;
	msg.senderId = senderId;
	msg.delivery = DeliveryMode::ReliableOrdered;
	msg.setJsonPayload(stateJson);
	msg.stampNow();
	return msg;
}

/// @brief 入力メッセージを生成する
/// @param senderId 送信者ID
/// @param inputJson 入力データ
/// @return 入力メッセージ
[[nodiscard]] inline GameMessage input(
	ConnectionId senderId, const nlohmann::json& inputJson)
{
	GameMessage msg;
	msg.type = MessageType::Input;
	msg.senderId = senderId;
	msg.delivery = DeliveryMode::Reliable;
	msg.setJsonPayload(inputJson);
	msg.stampNow();
	return msg;
}

} // namespace MessageFactory

} // namespace mitiru::network
