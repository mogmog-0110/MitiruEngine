#pragma once

/// @file LocalTransport.hpp
/// @brief インプロセスローカル転送
/// @details 実ネットワークを使用しないテスト用トランスポート。
///          2つのLocalTransportインスタンスをペアリングして相互通信する。

#include <mitiru/network/INetworkTransport.hpp>

#include <algorithm>
#include <mutex>
#include <queue>

namespace mitiru::network
{

/// @brief ローカル（インプロセス）転送層
/// @details AIテストやユニットテスト向けに、実際のソケット通信なしで
///          メッセージの送受信をエミュレートする。
///          pair() で2つのインスタンスを接続して使用する。
class LocalTransport final : public INetworkTransport
{
public:
	/// @brief 2つのLocalTransportをペアリングする
	/// @param a 一方のトランスポート
	/// @param b もう一方のトランスポート
	/// @details ペアリング後、aのsend()はbのキューに、bのsend()はaのキューに
	///          メッセージが格納される。双方の接続IDは1に設定される。
	static void pair(LocalTransport& a, LocalTransport& b)
	{
		a.m_partner = &b;
		b.m_partner = &a;
		a.m_connected = true;
		b.m_connected = true;
		a.m_connectionId = 1;
		b.m_connectionId = 1;
	}

	/// @brief リッスン開始（ローカルでは常に成功）
	/// @param port ポート番号（無視される）
	/// @return 常に true
	bool listen(std::uint16_t /*port*/) override
	{
		return true;
	}

	/// @brief 接続（ローカルではペアリングで代用するため常にfalse）
	/// @param host ホスト名（無視される）
	/// @param port ポート番号（無視される）
	/// @return 常に false（pair()を使用すること）
	bool connect(std::string_view /*host*/, std::uint16_t /*port*/) override
	{
		return false;
	}

	/// @brief 接続を切断する
	/// @param id 接続ID
	void disconnect(ConnectionId /*id*/) override
	{
		m_connected = false;
		if (m_partner != nullptr)
		{
			m_partner->m_connected = false;
			m_partner->m_partner = nullptr;
			m_partner = nullptr;
		}
	}

	/// @brief データを送信する（パートナーのキューに格納）
	/// @param id 送信先の接続ID
	/// @param data 送信データ
	void send(ConnectionId id, const std::vector<std::uint8_t>& data) override
	{
		if (m_partner == nullptr || !m_connected)
		{
			return;
		}

		NetworkMessage msg;
		msg.sender = m_connectionId;
		msg.header.size = static_cast<std::uint32_t>(data.size());
		msg.payload = data;

		std::lock_guard<std::mutex> lock(m_partner->m_mutex);
		m_partner->m_queue.push(std::move(msg));

		++m_stats.packetsSent;
		m_stats.bytesSent += data.size();
		static_cast<void>(id);
	}

	/// @brief 受信済みメッセージを取得する
	/// @return キューに溜まったメッセージ一覧
	[[nodiscard]] std::vector<NetworkMessage> poll() override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<NetworkMessage> result;
		while (!m_queue.empty())
		{
			auto& front = m_queue.front();
			m_stats.packetsReceived++;
			m_stats.bytesReceived += front.payload.size();
			result.push_back(std::move(front));
			m_queue.pop();
		}
		return result;
	}

	/// @brief 指定接続がアクティブか判定する
	/// @param id 接続ID
	/// @return 接続中なら true
	[[nodiscard]] bool isConnected(ConnectionId /*id*/) const override
	{
		return m_connected;
	}

	/// @brief ネットワーク統計を取得する
	/// @return 現在の統計情報
	[[nodiscard]] NetworkStats stats() const override
	{
		return m_stats;
	}

private:
	LocalTransport* m_partner = nullptr;  ///< ペア相手
	bool m_connected = false;              ///< 接続状態
	ConnectionId m_connectionId = INVALID_CONNECTION; ///< 自身の接続ID
	std::queue<NetworkMessage> m_queue;    ///< 受信キュー
	mutable std::mutex m_mutex;            ///< キューの排他制御
	NetworkStats m_stats;                  ///< 統計情報
};

} // namespace mitiru::network
