#pragma once

/// @file StateSync.hpp
/// @brief 状態同期マネージャー
/// @details クライアント/サーバー間のゲーム状態同期を管理する。
///          スナップショットベースの差分同期をサポートする。

#include <cstdint>
#include <deque>
#include <string>

namespace mitiru::network
{

/// @brief 状態同期マネージャー
/// @details サーバー権限モデルにおけるゲーム状態の同期を管理する。
///          pushState()で新しいスナップショットを追加し、
///          acknowledgeVersion()で確認済みバージョンを設定する。
///          未確認のスナップショットはpendingCount()で確認できる。
class StateSync
{
public:
	/// @brief 権限モード（サーバー/クライアント）を設定する
	/// @param isServer true ならサーバー権限モード
	void setAuthority(bool isServer) noexcept
	{
		m_isServer = isServer;
	}

	/// @brief 新しい状態スナップショットをプッシュする
	/// @param stateJson 状態のJSON文字列
	void pushState(std::string stateJson)
	{
		++m_version;
		m_states.push_back(VersionedState{m_version, std::move(stateJson)});
	}

	/// @brief 最新の状態を取得する
	/// @return 最新のJSON状態文字列（履歴が空なら空文字列）
	[[nodiscard]] std::string latestState() const
	{
		if (m_states.empty())
		{
			return {};
		}
		return m_states.back().json;
	}

	/// @brief 現在の状態バージョンを取得する
	/// @return 状態バージョン番号
	[[nodiscard]] std::uint64_t stateVersion() const noexcept
	{
		return m_version;
	}

	/// @brief 確認済みバージョンを設定する
	/// @param version 確認済みバージョン番号
	/// @details 指定バージョン以前の履歴を破棄する。
	void acknowledgeVersion(std::uint64_t version)
	{
		m_acknowledgedVersion = version;
		// 確認済みバージョン以前の履歴を除去
		while (!m_states.empty() && m_states.front().version <= version)
		{
			m_states.pop_front();
		}
	}

	/// @brief 未確認の状態数を取得する
	/// @return 未確認（pending）の状態スナップショット数
	[[nodiscard]] std::size_t pendingCount() const noexcept
	{
		return m_states.size();
	}

private:
	/// @brief バージョン付き状態
	struct VersionedState
	{
		std::uint64_t version = 0; ///< バージョン番号
		std::string json;           ///< 状態JSON
	};

	bool m_isServer = false;                   ///< サーバー権限モードか
	std::uint64_t m_version = 0;               ///< 現在のバージョン
	std::uint64_t m_acknowledgedVersion = 0;   ///< 確認済みバージョン
	std::deque<VersionedState> m_states;       ///< 状態履歴
};

} // namespace mitiru::network
