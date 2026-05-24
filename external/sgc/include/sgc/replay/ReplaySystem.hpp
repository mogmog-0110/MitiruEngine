#pragma once

/// @file ReplaySystem.hpp
/// @brief 入力リプレイ記録・再生システム
///
/// ゲームの入力をフレーム単位で記録し、後から再生する。
/// キーボード・マウス・カスタムイベントに対応。
///
/// @code
/// sgc::replay::ReplayRecorder recorder;
/// recorder.startRecording();
/// recorder.recordFrame({sgc::replay::KeyEvent{65, true}});
/// recorder.recordFrame({sgc::replay::MouseEvent{100, 200, 0, true}});
/// auto data = recorder.stopRecording();
///
/// sgc::replay::ReplayPlayer player;
/// player.load(data);
/// while (!player.isFinished())
/// {
///     auto frame = player.nextFrame();
///     // 入力を適用
/// }
/// @endcode

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sgc::replay
{

/// @brief キー入力イベント
struct KeyEvent
{
	int keyCode = 0;     ///< キーコード
	bool pressed = true; ///< 押下(true) / 解放(false)

	[[nodiscard]] constexpr bool operator==(const KeyEvent&) const noexcept = default;
};

/// @brief マウス入力イベント
struct MouseEvent
{
	int x = 0;           ///< X座標
	int y = 0;           ///< Y座標
	int button = 0;      ///< ボタン番号
	bool pressed = true; ///< 押下(true) / 解放(false)

	[[nodiscard]] constexpr bool operator==(const MouseEvent&) const noexcept = default;
};

/// @brief カスタムイベント
struct CustomEvent
{
	std::string type;  ///< イベント種別
	std::string data;  ///< イベントデータ

	[[nodiscard]] bool operator==(const CustomEvent&) const = default;
};

/// @brief 入力イベント（いずれかの型）
using InputEvent = std::variant<KeyEvent, MouseEvent, CustomEvent>;

/// @brief 1フレーム分の入力データ
struct InputFrame
{
	uint64_t frameNumber = 0;          ///< フレーム番号
	std::vector<InputEvent> events;    ///< そのフレームのイベント一覧
};

/// @brief リプレイメタデータ
struct ReplayMetadata
{
	uint32_t version = 1;          ///< データバージョン
	uint64_t timestamp = 0;        ///< 記録開始のタイムスタンプ
	std::string description;       ///< 説明テキスト
};

/// @brief リプレイデータ
struct ReplayData
{
	ReplayMetadata metadata;               ///< メタデータ
	std::vector<InputFrame> frames;        ///< 全フレームデータ

	/// @brief フレーム数を取得する
	[[nodiscard]] size_t frameCount() const noexcept { return frames.size(); }

	/// @brief データが空か判定する
	[[nodiscard]] bool empty() const noexcept { return frames.empty(); }
};

/// @brief リプレイ記録器
///
/// フレームごとの入力イベントを記録し、ReplayDataとして出力する。
class ReplayRecorder
{
public:
	/// @brief 記録を開始する
	/// @param timestamp 記録開始タイムスタンプ
	void startRecording(uint64_t timestamp = 0)
	{
		m_data = ReplayData{};
		m_data.metadata.timestamp = timestamp;
		m_recording = true;
		m_currentFrame = 0;
	}

	/// @brief 1フレーム分の入力を記録する
	/// @param events そのフレームのイベント一覧
	/// @return 記録中でない場合はfalse
	bool recordFrame(const std::vector<InputEvent>& events)
	{
		if (!m_recording)
		{
			return false;
		}
		InputFrame frame;
		frame.frameNumber = m_currentFrame;
		frame.events = events;
		m_data.frames.push_back(std::move(frame));
		++m_currentFrame;
		return true;
	}

	/// @brief 空フレームを記録する（イベントなし）
	/// @return 記録中でない場合はfalse
	bool recordEmptyFrame()
	{
		return recordFrame({});
	}

	/// @brief 記録を停止し、データを返す
	/// @return 記録されたリプレイデータ
	[[nodiscard]] ReplayData stopRecording()
	{
		m_recording = false;
		return std::move(m_data);
	}

	/// @brief 記録中か判定する
	[[nodiscard]] bool isRecording() const noexcept { return m_recording; }

	/// @brief 現在のフレーム番号を取得する
	[[nodiscard]] uint64_t currentFrame() const noexcept { return m_currentFrame; }

private:
	ReplayData m_data;
	uint64_t m_currentFrame = 0;
	bool m_recording = false;
};

/// @brief リプレイ再生器
///
/// ReplayDataを読み込み、フレームごとに入力を再生する。
class ReplayPlayer
{
public:
	/// @brief リプレイデータを読み込む
	/// @param data リプレイデータ
	void load(const ReplayData& data)
	{
		m_data = data;
		m_playbackIndex = 0;
	}

	/// @brief リプレイデータをムーブで読み込む
	/// @param data リプレイデータ
	void load(ReplayData&& data)
	{
		m_data = std::move(data);
		m_playbackIndex = 0;
	}

	/// @brief 次のフレームを取得する
	/// @return フレームデータ（終了時はnullopt）
	[[nodiscard]] std::optional<InputFrame> nextFrame()
	{
		if (m_playbackIndex >= m_data.frames.size())
		{
			return std::nullopt;
		}
		return m_data.frames[m_playbackIndex++];
	}

	/// @brief 再生が完了したか判定する
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_playbackIndex >= m_data.frames.size();
	}

	/// @brief 再生位置をリセットする
	void reset() noexcept
	{
		m_playbackIndex = 0;
	}

	/// @brief 総フレーム数を取得する
	[[nodiscard]] size_t totalFrames() const noexcept { return m_data.frames.size(); }

	/// @brief 現在の再生位置を取得する
	[[nodiscard]] size_t currentIndex() const noexcept { return m_playbackIndex; }

	/// @brief メタデータを取得する
	[[nodiscard]] const ReplayMetadata& metadata() const noexcept { return m_data.metadata; }

private:
	ReplayData m_data;
	size_t m_playbackIndex = 0;
};

} // namespace sgc::replay
