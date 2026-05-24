#pragma once

/// @file ITransport.hpp
/// @brief ネットワークトランスポート抽象化とループバック実装
///
/// 接続・送受信・切断を抽象化するトランスポート層インターフェースと、
/// テスト用のインプロセス・ループバック実装を提供する。
///
/// @code
/// sgc::network::LoopbackTransport server;
/// sgc::network::LoopbackTransport client;
/// sgc::network::LoopbackTransport::link(server, client);
///
/// server.listen(8080);
/// client.connect("localhost", 8080);
///
/// // サーバー側でpollして接続イベントを取得
/// auto events = server.poll();
///
/// // データ送信
/// std::vector<uint8_t> data = {1, 2, 3};
/// client.send(1, data);
///
/// // サーバー側で受信
/// auto messages = server.poll();
/// @endcode

#include <cstdint>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgc::network
{

/// @brief トランスポートイベント種別
enum class TransportEvent
{
	Connected,     ///< 接続確立
	Disconnected,  ///< 切断
	DataReceived,  ///< データ受信
	Error          ///< エラー発生
};

/// @brief トランスポートメッセージ
///
/// 接続IDとデータペイロードを保持する。
struct TransportMessage
{
	uint32_t connectionId{0};         ///< 接続識別子
	std::vector<uint8_t> data;        ///< ペイロードデータ
	TransportEvent event{TransportEvent::DataReceived};  ///< イベント種別
};

/// @brief ネットワークトランスポート抽象インターフェース
///
/// TCP/UDP/WebSocket等の具体的なプロトコルを抽象化する。
class ITransport
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITransport() = default;

	/// @brief 指定ポートでリッスンを開始する
	/// @param port リッスンポート番号
	/// @return 成功すればtrue
	virtual bool listen(uint16_t port) = 0;

	/// @brief 指定ホスト・ポートに接続する
	/// @param host ホスト名またはIPアドレス
	/// @param port ポート番号
	/// @return 成功すればtrue
	virtual bool connect(std::string_view host, uint16_t port) = 0;

	/// @brief 指定接続を切断する
	/// @param connectionId 切断する接続ID
	virtual void disconnect(uint32_t connectionId) = 0;

	/// @brief データを送信する
	/// @param connectionId 送信先接続ID
	/// @param data 送信データ
	/// @return 送信に成功すればtrue
	virtual bool send(uint32_t connectionId, std::span<const uint8_t> data) = 0;

	/// @brief 受信キューからメッセージを取得する
	/// @return 受信メッセージのベクタ
	[[nodiscard]] virtual std::vector<TransportMessage> poll() = 0;

	/// @brief トランスポートをシャットダウンする
	virtual void shutdown() = 0;

	/// @brief リッスン中かどうかを返す
	/// @return リッスン中ならtrue
	[[nodiscard]] virtual bool isListening() const = 0;
};

/// @brief テスト用ループバックトランスポート
///
/// インプロセスでのメッセージ送受信を実現する。
/// link()で2つのインスタンスを接続し、send()したデータが
/// 相手側のpoll()で取得できる。
class LoopbackTransport final : public ITransport
{
public:
	/// @brief デフォルトコンストラクタ
	LoopbackTransport() = default;

	/// @brief 2つのループバックトランスポートを接続する
	/// @param a 1つ目のトランスポート
	/// @param b 2つ目のトランスポート
	///
	/// a.send() → b.poll()、b.send() → a.poll() となる。
	static void link(LoopbackTransport& a, LoopbackTransport& b) noexcept
	{
		a.m_peer = &b;
		b.m_peer = &a;
	}

	/// @brief リッスンを開始する（ループバックでは状態フラグのみ設定）
	/// @param port ポート番号（ループバックでは使用しない）
	/// @return 常にtrue
	bool listen(uint16_t port) override
	{
		m_listening = true;
		m_port = port;
		return true;
	}

	/// @brief 接続する（ループバックでは接続イベントを生成）
	/// @param host ホスト名（ループバックでは使用しない）
	/// @param port ポート番号（ループバックでは使用しない）
	/// @return ピアが設定されていればtrue
	bool connect(std::string_view /*host*/, uint16_t /*port*/) override
	{
		if (!m_peer) return false;

		m_connected = true;
		++m_nextConnectionId;

		// 自分側に接続イベントを追加
		{
			std::scoped_lock lock(m_mutex);
			m_incoming.push(TransportMessage{
				m_nextConnectionId, {}, TransportEvent::Connected});
		}

		// ピア側にも接続イベントを追加
		{
			std::scoped_lock lock(m_peer->m_mutex);
			m_peer->m_incoming.push(TransportMessage{
				m_nextConnectionId, {}, TransportEvent::Connected});
		}

		return true;
	}

	/// @brief 接続を切断する
	/// @param connectionId 切断する接続ID
	void disconnect(uint32_t connectionId) override
	{
		m_connected = false;

		// 自分側に切断イベントを追加
		{
			std::scoped_lock lock(m_mutex);
			m_incoming.push(TransportMessage{
				connectionId, {}, TransportEvent::Disconnected});
		}

		// ピア側にも切断イベントを追加
		if (m_peer)
		{
			std::scoped_lock lock(m_peer->m_mutex);
			m_peer->m_incoming.push(TransportMessage{
				connectionId, {}, TransportEvent::Disconnected});
		}
	}

	/// @brief データを送信する（ピアの受信キューに追加）
	/// @param connectionId 接続ID
	/// @param data 送信データ
	/// @return ピアが設定されていればtrue
	bool send(uint32_t connectionId, std::span<const uint8_t> data) override
	{
		if (!m_peer) return false;

		std::scoped_lock lock(m_peer->m_mutex);
		m_peer->m_incoming.push(TransportMessage{
			connectionId,
			std::vector<uint8_t>(data.begin(), data.end()),
			TransportEvent::DataReceived});
		return true;
	}

	/// @brief 受信キューからメッセージを全て取得する
	/// @return 受信メッセージのベクタ
	[[nodiscard]] std::vector<TransportMessage> poll() override
	{
		std::scoped_lock lock(m_mutex);
		std::vector<TransportMessage> messages;
		while (!m_incoming.empty())
		{
			messages.push_back(std::move(m_incoming.front()));
			m_incoming.pop();
		}
		return messages;
	}

	/// @brief トランスポートをシャットダウンする
	void shutdown() override
	{
		m_listening = false;
		m_connected = false;
		m_peer = nullptr;
		std::scoped_lock lock(m_mutex);
		while (!m_incoming.empty()) m_incoming.pop();
	}

	/// @brief リッスン中かどうかを返す
	/// @return リッスン中ならtrue
	[[nodiscard]] bool isListening() const override
	{
		return m_listening;
	}

	/// @brief 接続中かどうかを返す
	/// @return 接続中ならtrue
	[[nodiscard]] bool isConnected() const noexcept
	{
		return m_connected;
	}

private:
	LoopbackTransport* m_peer{nullptr};           ///< 接続先ピア
	std::queue<TransportMessage> m_incoming;       ///< 受信キュー
	mutable std::mutex m_mutex;                    ///< 受信キューのミューテックス
	uint32_t m_nextConnectionId{0};                ///< 次の接続ID
	uint16_t m_port{0};                            ///< リッスンポート
	bool m_listening{false};                       ///< リッスン中フラグ
	bool m_connected{false};                       ///< 接続中フラグ
};

} // namespace sgc::network
