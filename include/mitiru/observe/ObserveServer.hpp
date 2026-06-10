#pragma once

/// @file ObserveServer.hpp
/// @brief AI観測サーバー (廃止予定 — ADR 0019)
/// @details @deprecated 新規コードでは EngineHttpServer (/api/observe/*) を使うこと。
///          ObserveServer/ObserveRouter/observe::HttpServer は次フェーズで削除する。
///          HTTP経由でAIエージェントがゲーム状態を問い合わせるためのサーバー。
///          HttpServerとObserveRouterを統合し、ワンコールで起動できるAPIを提供する。

#include <cstdint>
#include <memory>
#include <string>

#include <mitiru/observe/HttpServer.hpp>
#include <mitiru/observe/Inspector.hpp>
#include <mitiru/observe/ObserveRouter.hpp>

namespace mitiru
{

/// 前方宣言
class Engine;

} // namespace mitiru

namespace mitiru::observe
{

/// @brief 観測サーバーの設定
struct ObserveServerConfig
{
	std::uint16_t port = 8080;           ///< リッスンポート番号
	std::string host = "127.0.0.1";      ///< バインドアドレス（デフォルトはローカルのみ）
	bool enableCors = true;              ///< CORSヘッダーを有効にするか
	bool enableScreenshot = true;        ///< スクリーンショットエンドポイントを有効にするか
	int maxSnapshotRateMs = 0;           ///< スナップショットの最小間隔（ミリ秒、0=制限なし）
};

/// @brief AI観測サーバー
/// @details HttpServerとObserveRouterを所有し、
///          エンジンに接続してHTTPエンドポイントを公開する。
///
/// @code
/// mitiru::Engine engine;
/// mitiru::observe::Inspector inspector;
/// mitiru::observe::ObserveServer server;
///
/// server.start(8080, engine, inspector);
/// // ... ゲームループ実行 ...
/// server.stop();
/// @endcode
class ObserveServer
{
public:
	/// @brief デフォルトコンストラクタ
	ObserveServer() = default;

	/// @brief 設定付きコンストラクタ
	/// @param config サーバー設定
	explicit ObserveServer(const ObserveServerConfig& config)
		: m_config(config)
	{
	}

	/// @brief デストラクタ（自動停止を保証する）
	~ObserveServer()
	{
		stop();
	}

	/// @brief コピー禁止
	ObserveServer(const ObserveServer&) = delete;
	ObserveServer& operator=(const ObserveServer&) = delete;

	/// @brief ムーブ禁止
	ObserveServer(ObserveServer&&) = delete;
	ObserveServer& operator=(ObserveServer&&) = delete;

	/// @brief サーバーを開始する
	/// @param port リッスンポート番号
	/// @param engine エンジンへの参照
	/// @param inspector インスペクターへの参照
	/// @return 開始成功なら true
	bool start(std::uint16_t port, Engine& engine, Inspector& inspector)
	{
		if (m_httpServer && m_httpServer->isRunning())
		{
			return false;
		}

		m_config.port = port;

		/// HTTPサーバーを生成する
		m_httpServer = std::make_unique<HttpServer>();
		m_httpServer->setBindAddress(m_config.host);

		/// ルーターを生成してルートを登録する
		m_router = std::make_unique<ObserveRouter>(
			engine, inspector, engine.inputInjector());
		m_router->setupRoutes(*m_httpServer);

		/// サーバーを起動する
		if (!m_httpServer->start(port))
		{
			m_httpServer.reset();
			m_router.reset();
			return false;
		}

		return true;
	}

	/// @brief 設定済みポートでサーバーを開始する
	/// @param engine エンジンへの参照
	/// @param inspector インスペクターへの参照
	/// @return 開始成功なら true
	bool start(Engine& engine, Inspector& inspector)
	{
		return start(m_config.port, engine, inspector);
	}

	/// @brief サーバーを停止する
	void stop() noexcept
	{
		if (m_httpServer)
		{
			m_httpServer->stop();
		}
		m_httpServer.reset();
		m_router.reset();
	}

	/// @brief サーバーが稼働中か判定する
	/// @return 稼働中なら true
	[[nodiscard]] bool isRunning() const noexcept
	{
		return m_httpServer && m_httpServer->isRunning();
	}

	/// @brief 現在の設定を取得する
	/// @return サーバー設定への const 参照
	[[nodiscard]] const ObserveServerConfig& config() const noexcept
	{
		return m_config;
	}

private:
	ObserveServerConfig m_config;                    ///< サーバー設定
	std::unique_ptr<HttpServer> m_httpServer;        ///< HTTPサーバー
	std::unique_ptr<ObserveRouter> m_router;         ///< ルーター
};

} // namespace mitiru::observe
