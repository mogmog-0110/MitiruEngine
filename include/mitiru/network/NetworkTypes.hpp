#pragma once

/// @file NetworkTypes.hpp
/// @brief ネットワーク型定義
/// @details 接続ID、パケットヘッダ、ネットワークイベント等の基本型を定義する。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::network
{

/// @brief 接続ID型
using ConnectionId = std::uint32_t;

/// @brief 無効な接続IDを表す定数
constexpr ConnectionId INVALID_CONNECTION = 0;

/// @brief ネットワークイベント種別
enum class NetworkEvent
{
	Connected,     ///< 接続完了
	Disconnected,  ///< 切断
	DataReceived,  ///< データ受信
	Error          ///< エラー発生
};

/// @brief パケットヘッダ
/// @details 各ネットワークメッセージの先頭に付与されるメタ情報。
struct PacketHeader
{
	std::uint32_t size = 0;      ///< ペイロードサイズ（バイト）
	std::uint32_t type = 0;      ///< メッセージ型ID
	std::uint32_t sequence = 0;  ///< シーケンス番号
};

/// @brief ネットワークメッセージ
/// @details 送信元・ヘッダ・ペイロードを含む完全なメッセージ。
struct NetworkMessage
{
	ConnectionId sender = INVALID_CONNECTION;  ///< 送信元の接続ID
	PacketHeader header;                        ///< パケットヘッダ
	std::vector<std::uint8_t> payload;          ///< ペイロードデータ
};

/// @brief ネットワーク統計情報
/// @details 送受信量・レイテンシ・パケットロス率などの計測値。
struct NetworkStats
{
	std::uint64_t bytesSent = 0;       ///< 送信バイト数
	std::uint64_t bytesReceived = 0;   ///< 受信バイト数
	std::uint64_t packetsSent = 0;     ///< 送信パケット数
	std::uint64_t packetsReceived = 0; ///< 受信パケット数
	float latencyMs = 0.0f;            ///< レイテンシ（ミリ秒）
	float packetLoss = 0.0f;           ///< パケットロス率 [0.0, 1.0]
};

} // namespace mitiru::network
