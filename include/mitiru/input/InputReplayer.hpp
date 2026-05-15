#pragma once

/// @file InputReplayer.hpp
/// @brief 入力リプレイシステム
/// @details 記録された入力データを再生し、決定論的リプレイを実現する。
///          InputRecorderで記録したReplayDataを読み込み、
///          フレーム番号に対応するコマンドを返す。

#include <cstdint>
#include <vector>

#include "mitiru/input/InputInjector.hpp"
#include "mitiru/input/InputRecorder.hpp"

namespace mitiru
{

/// @brief 入力リプレイヤー
/// @details ReplayDataをロードし、フレーム番号に基づいてコマンドを再生する。
class InputReplayer
{
public:
	/// @brief リプレイデータをロードする
	/// @param data リプレイデータ
	void load(const ReplayData& data)
	{
		m_data = data;
		m_currentIndex = 0;
		m_finished = false;
	}

	/// @brief 指定フレームのコマンド一覧を取得する
	/// @param frameNumber 取得したいフレーム番号
	/// @return そのフレームの入力コマンド一覧（該当なしなら空）
	[[nodiscard]] std::vector<InputCommand> getCommandsForFrame(std::uint64_t frameNumber) const
	{
		/// バイナリサーチやインデックスキャッシュも将来検討するが、
		/// 現時点ではフレーム配列の線形走査で十分
		for (const auto& frame : m_data.frames)
		{
			if (frame.frameNumber == frameNumber)
			{
				return frame.commands;
			}
		}
		return {};
	}

	/// @brief 次のフレームのコマンドを順次取得する
	/// @return 次のフレームの入力コマンド一覧（終了なら空）
	[[nodiscard]] std::vector<InputCommand> consumeNextFrame()
	{
		if (m_currentIndex >= m_data.frames.size())
		{
			m_finished = true;
			return {};
		}
		const auto& frame = m_data.frames[m_currentIndex];
		++m_currentIndex;

		if (m_currentIndex >= m_data.frames.size())
		{
			m_finished = true;
		}
		return frame.commands;
	}

	/// @brief リプレイが終了したか判定する
	/// @return 全フレーム再生済みなら true
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_finished;
	}

	/// @brief リプレイをリセットする（先頭に戻る）
	void reset() noexcept
	{
		m_currentIndex = 0;
		m_finished = false;
	}

	/// @brief 総フレーム数を取得する
	/// @return リプレイデータの総フレーム数
	[[nodiscard]] std::size_t totalFrames() const noexcept
	{
		return m_data.frames.size();
	}

	/// @brief 現在の再生位置を取得する
	/// @return 現在のフレームインデックス
	[[nodiscard]] std::size_t currentIndex() const noexcept
	{
		return m_currentIndex;
	}

	/// @brief 乱数シードを取得する
	/// @return リプレイデータのシード値
	[[nodiscard]] std::uint64_t seed() const noexcept
	{
		return m_data.seed;
	}

	/// @brief TPSを取得する
	/// @return Ticks Per Second
	[[nodiscard]] int tps() const noexcept
	{
		return m_data.tps;
	}

private:
	ReplayData m_data;                ///< リプレイデータ
	std::size_t m_currentIndex = 0;   ///< 現在の再生インデックス
	bool m_finished = false;          ///< 再生完了フラグ
};

} // namespace mitiru
