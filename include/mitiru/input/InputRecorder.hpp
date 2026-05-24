#pragma once

/// @file InputRecorder.hpp
/// @brief 入力記録システム
/// @details 決定論的リプレイのための入力記録機能。
///          フレームごとの入力コマンドを記録し、ReplayDataとして出力する。

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
	[[nodiscard]] std::string toJson() const
	{
		return toJsonValue().dump();
	}

	/// @brief nlohmann::json オブジェクトに変換する
	[[nodiscard]] nlohmann::json toJsonValue() const
	{
		nlohmann::json jcmds = nlohmann::json::array();
		for (const auto& c : commands)
		{
			jcmds.push_back(nlohmann::json{
				{"type",        static_cast<int>(c.type)},
				{"keyCode",     c.keyCode},
				{"mouseButton", c.mouseButton},
				{"mouseX",      c.mouseX},
				{"mouseY",      c.mouseY},
			});
		}
		return nlohmann::json{
			{"frame",    frameNumber},
			{"commands", std::move(jcmds)},
		};
	}

	/// @brief nlohmann::json オブジェクトから InputFrame を構築する
	[[nodiscard]] static InputFrame fromJsonValue(const nlohmann::json& j)
	{
		InputFrame f;
		f.frameNumber = j.value("frame", std::uint64_t{0});
		if (j.contains("commands") && j["commands"].is_array())
		{
			for (const auto& jc : j["commands"])
			{
				InputCommand c;
				c.type        = static_cast<InputCommandType>(jc.value("type", 0));
				c.keyCode     = jc.value("keyCode", 0);
				c.mouseButton = jc.value("mouseButton", 0);
				c.mouseX      = jc.value("mouseX", 0.0f);
				c.mouseY      = jc.value("mouseY", 0.0f);
				f.commands.push_back(c);
			}
		}
		return f;
	}
};

/// @brief リプレイデータ
/// @details 記録セッション全体のデータを保持する。
///          乱数シード・TPS・全フレームの入力を含む。
///          JSON 経由で disk に save / load 可能 (決定論再現の最低限のペイロード)。
struct ReplayData
{
	std::uint64_t seed = 0;                  ///< 乱数シード
	int tps = 60;                            ///< Ticks Per Second
	std::vector<InputFrame> frames;          ///< フレームごとの入力データ

	/// @brief nlohmann::json オブジェクトに変換する
	[[nodiscard]] nlohmann::json toJsonValue() const
	{
		nlohmann::json jframes = nlohmann::json::array();
		for (const auto& f : frames)
		{
			jframes.push_back(f.toJsonValue());
		}
		return nlohmann::json{
			{"seed",   seed},
			{"tps",    tps},
			{"frames", std::move(jframes)},
		};
	}

	/// @brief JSON文字列に変換する
	[[nodiscard]] std::string toJson() const
	{
		return toJsonValue().dump();
	}

	/// @brief nlohmann::json オブジェクトから ReplayData を構築する
	[[nodiscard]] static ReplayData fromJsonValue(const nlohmann::json& j)
	{
		ReplayData data;
		data.seed = j.value("seed", std::uint64_t{0});
		data.tps  = j.value("tps", 60);
		if (j.contains("frames") && j["frames"].is_array())
		{
			for (const auto& jf : j["frames"])
			{
				data.frames.push_back(InputFrame::fromJsonValue(jf));
			}
		}
		return data;
	}

	/// @brief JSON 文字列から ReplayData を構築する
	/// @throw nlohmann::json::parse_error 不正な JSON の場合
	[[nodiscard]] static ReplayData fromJson(const std::string& jsonStr)
	{
		return fromJsonValue(nlohmann::json::parse(jsonStr));
	}

	/// @brief 指定パスに JSON で保存する
	/// @throw std::runtime_error ファイルを開けない場合
	void saveToFile(const std::string& path) const
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			throw std::runtime_error("ReplayData::saveToFile: cannot open '" + path + "'");
		}
		// dump(2) で人間が grep / diff しやすい改行付きを出す。
		// バイナリサイズが問題になったら dump() に戻して圧縮レイヤーを噛ます。
		out << toJsonValue().dump(2);
	}

	/// @brief 指定パスから JSON を読み込む
	/// @throw std::runtime_error ファイルを開けない場合
	/// @throw nlohmann::json::parse_error 不正な JSON の場合
	[[nodiscard]] static ReplayData loadFromFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			throw std::runtime_error("ReplayData::loadFromFile: cannot open '" + path + "'");
		}
		return fromJsonValue(nlohmann::json::parse(in));
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
