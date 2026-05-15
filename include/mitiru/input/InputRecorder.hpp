#pragma once

/// @file InputRecorder.hpp
/// @brief 入力記録システム
/// @details 決定論的リプレイのための入力記録機能。
///          フレームごとの入力コマンドを記録し、ReplayDataとして出力する。

#include <cstdint>
#include <string>
#include <vector>

#include "mitiru/input/InputInjector.hpp"

namespace mitiru
{

/// @brief 1フレーム分の入力データ
/// @details フレーム番号と、そのフレームで発生した入力コマンドの組。
struct InputFrame
{
	std::uint64_t frameNumber = 0;            ///< フレーム番号
	std::vector<InputCommand> commands;       ///< そのフレームの入力コマンド一覧

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{\"frame\":" + std::to_string(frameNumber) + ",\"commands\":[";
		for (std::size_t i = 0; i < commands.size(); ++i)
		{
			if (i > 0) json += ",";
			json += commandToJson(commands[i]);
		}
		json += "]}";
		return json;
	}

private:
	/// @brief InputCommandをJSON文字列に変換する
	/// @param cmd 変換対象のコマンド
	/// @return JSON形式の文字列
	[[nodiscard]] static std::string commandToJson(const InputCommand& cmd)
	{
		std::string json;
		json += "{\"type\":" + std::to_string(static_cast<int>(cmd.type));
		json += ",\"keyCode\":" + std::to_string(cmd.keyCode);
		json += ",\"mouseButton\":" + std::to_string(cmd.mouseButton);
		json += ",\"mouseX\":" + std::to_string(cmd.mouseX);
		json += ",\"mouseY\":" + std::to_string(cmd.mouseY);
		json += "}";
		return json;
	}
};

/// @brief リプレイデータ
/// @details 記録セッション全体のデータを保持する。
///          乱数シード・TPS・全フレームの入力を含む。
struct ReplayData
{
	std::uint64_t seed = 0;                  ///< 乱数シード
	int tps = 60;                            ///< Ticks Per Second
	std::vector<InputFrame> frames;          ///< フレームごとの入力データ

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{\"seed\":" + std::to_string(seed);
		json += ",\"tps\":" + std::to_string(tps);
		json += ",\"frames\":[";
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			if (i > 0) json += ",";
			json += frames[i].toJson();
		}
		json += "]}";
		return json;
	}

	/// @brief JSON文字列からReplayDataを構築する（簡易パーサー）
	/// @param jsonStr JSON文字列
	/// @return 構築されたReplayData
	/// @note 簡易実装のため、正しいフォーマットを前提とする
	[[nodiscard]] static ReplayData fromJson(const std::string& jsonStr)
	{
		ReplayData data;

		/// seed の抽出
		auto seedPos = jsonStr.find("\"seed\":");
		if (seedPos != std::string::npos)
		{
			seedPos += 7;
			data.seed = std::stoull(jsonStr.substr(seedPos));
		}

		/// tps の抽出
		auto tpsPos = jsonStr.find("\"tps\":");
		if (tpsPos != std::string::npos)
		{
			tpsPos += 6;
			data.tps = std::stoi(jsonStr.substr(tpsPos));
		}

		/// フレームデータの抽出（簡易）
		/// 完全なJSONパーサーは将来の外部ライブラリ統合で対応
		/// 現時点ではtoJson()でシリアライズしたデータの読み戻しは
		/// フレームデータなしの基本情報のみサポート

		return data;
	}

	/// @brief 総フレーム数を取得する
	/// @return フレーム数
	[[nodiscard]] std::size_t totalFrames() const noexcept
	{
		return frames.size();
	}
};

/// @brief 入力記録クラス
/// @details フレームごとの入力コマンドを記録し、ReplayDataとして出力する。
class InputRecorder
{
public:
	/// @brief 記録を開始する
	/// @param seed 乱数シード
	/// @param tps Ticks Per Second
	void beginRecording(std::uint64_t seed = 0, int tps = 60)
	{
		m_data = ReplayData{};
		m_data.seed = seed;
		m_data.tps = tps;
		m_recording = true;
	}

	/// @brief 1フレーム分のコマンドを記録する
	/// @param frameNumber フレーム番号
	/// @param commands そのフレームの入力コマンド一覧
	void recordFrame(std::uint64_t frameNumber, const std::vector<InputCommand>& commands)
	{
		if (!m_recording) return;

		m_data.frames.push_back(InputFrame{
			.frameNumber = frameNumber,
			.commands = commands
		});
	}

	/// @brief 記録を終了し、ReplayDataを返す
	/// @return 記録されたリプレイデータ
	[[nodiscard]] ReplayData endRecording()
	{
		m_recording = false;
		return m_data;
	}

	/// @brief 記録中か判定する
	/// @return 記録中なら true
	[[nodiscard]] bool isRecording() const noexcept
	{
		return m_recording;
	}

	/// @brief 記録されたフレーム数を取得する
	/// @return フレーム数
	[[nodiscard]] std::size_t recordedFrameCount() const noexcept
	{
		return m_data.frames.size();
	}

private:
	ReplayData m_data;         ///< 記録中のリプレイデータ
	bool m_recording = false;  ///< 記録中フラグ
};

} // namespace mitiru
