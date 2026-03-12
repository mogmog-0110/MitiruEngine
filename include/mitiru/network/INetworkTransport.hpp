#pragma once

/// @file INetworkTransport.hpp
/// @brief ネットワーク転送層インターフェース
/// @details TCP/UDP等のトランスポート層を抽象化する。
///          具象クラスで実際のソケット通信やローカル転送を実装する。

#include <mitiru/network/NetworkTypes.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace mitiru::network
{

/// @brief ネットワーク転送層インターフェース
/// @details サーバー（listen）とクライアント（connect）の両方を
///          統一的に扱うための抽象インターフェース。
class INetworkTransport
{
public:
	/// @brief 仮想デストラクタ
	virtual ~INetworkTransport() = default;

	/// @brief 指定ポートでリッスンを開始する
	/// @param port 待ち受けポート番号
	/// @return 成功なら true
	virtual bool listen(std::uint16_t port) = 0;

	/// @brief リモートホストに接続する
	/// @param host ホスト名またはIPアドレス
	/// @param port 接続先ポート番号
	/// @return 成功なら true
	virtual bool connect(std::string_view host, std::uint16_t port) = 0;

	/// @brief 指定接続を切断する
	/// @param id 切断する接続ID
	virtual void disconnect(ConnectionId id) = 0;

	/// @brief データを送信する
	/// @param id 送信先の接続ID
	/// @param data 送信データ
	virtual void send(ConnectionId id, const std::vector<std::uint8_t>& data) = 0;

	/// @brief 受信済みメッセージを取得する
	/// @return 受信メッセージの一覧（キューが空なら空のvector）
	[[nodiscard]] virtual std::vector<NetworkMessage> poll() = 0;

	/// @brief 指定接続がアクティブか判定する
	/// @param id 接続ID
	/// @return 接続中なら true
	[[nodiscard]] virtual bool isConnected(ConnectionId id) const = 0;

	/// @brief ネットワーク統計を取得する
	/// @return 現在の統計情報
	[[nodiscard]] virtual NetworkStats stats() const = 0;
};

} // namespace mitiru::network
