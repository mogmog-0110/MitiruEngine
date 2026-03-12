#pragma once

/// @file Lobby.hpp
/// @brief ロビーシステム
/// @details マルチプレイヤーゲームのロビー管理を行う。
///          プレイヤーの追加・削除・準備状態の管理をサポートする。

#include <mitiru/network/NetworkTypes.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace mitiru::network
{

/// @brief ロビーシステム
/// @details ゲーム開始前のプレイヤー管理を行うロビー。
///          全プレイヤーが準備完了したらゲームを開始できる。
class Lobby
{
public:
	/// @brief ロビー内のプレイヤー情報
	struct Player
	{
		ConnectionId id = INVALID_CONNECTION; ///< 接続ID
		std::string name;                      ///< プレイヤー名
		bool ready = false;                    ///< 準備完了フラグ
	};

	/// @brief プレイヤーをロビーに追加する
	/// @param id 接続ID
	/// @param name プレイヤー名
	void addPlayer(ConnectionId id, std::string name)
	{
		Player player;
		player.id = id;
		player.name = std::move(name);
		player.ready = false;
		m_players.push_back(std::move(player));
	}

	/// @brief プレイヤーをロビーから削除する
	/// @param id 接続ID
	void removePlayer(ConnectionId id)
	{
		m_players.erase(
			std::remove_if(m_players.begin(), m_players.end(),
				[id](const Player& p) { return p.id == id; }),
			m_players.end());
	}

	/// @brief プレイヤーの準備状態を設定する
	/// @param id 接続ID
	/// @param ready 準備完了なら true
	void setReady(ConnectionId id, bool ready)
	{
		auto it = std::find_if(m_players.begin(), m_players.end(),
			[id](const Player& p) { return p.id == id; });
		if (it != m_players.end())
		{
			it->ready = ready;
		}
	}

	/// @brief 全プレイヤーが準備完了か判定する
	/// @return 全員準備完了なら true（プレイヤー0人の場合は false）
	[[nodiscard]] bool allReady() const
	{
		if (m_players.empty())
		{
			return false;
		}
		return std::all_of(m_players.begin(), m_players.end(),
			[](const Player& p) { return p.ready; });
	}

	/// @brief ロビー内のプレイヤー数を取得する
	/// @return プレイヤー数
	[[nodiscard]] std::size_t playerCount() const noexcept
	{
		return m_players.size();
	}

	/// @brief プレイヤー一覧を取得する
	/// @return プレイヤーのconst参照
	[[nodiscard]] const std::vector<Player>& players() const noexcept
	{
		return m_players;
	}

	/// @brief ロビー状態をJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json = R"({"players":[)";
		for (std::size_t i = 0; i < m_players.size(); ++i)
		{
			const auto& p = m_players[i];
			if (i > 0)
			{
				json += ",";
			}
			json += R"({"id":)" + std::to_string(p.id)
				+ R"(,"name":")" + p.name
				+ R"(","ready":)" + (p.ready ? "true" : "false")
				+ "}";
		}
		json += "]}";
		return json;
	}

private:
	std::vector<Player> m_players; ///< ロビー内プレイヤー一覧
};

} // namespace mitiru::network
