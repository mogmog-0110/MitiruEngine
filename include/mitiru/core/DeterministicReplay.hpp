#pragma once

/// @file DeterministicReplay.hpp
/// @brief 決定論的リプレイシステム
/// @details ゲーム入力とイベントをフレーム単位で記録・再生する。
///          CausalChainとの統合によりリプレイ中の因果イベントも記録可能。

#include <mitiru/observe/CausalChain.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::core
{

/// @brief リプレイ入力状態（1フレーム分）
struct InputState
{
	bool buttonA = false;    ///< Aボタン
	bool buttonB = false;    ///< Bボタン
	float axisX = 0.0f;     ///< X軸入力 [-1.0, 1.0]
	float axisY = 0.0f;     ///< Y軸入力 [-1.0, 1.0]

	std::map<std::string, float> extra; ///< 追加入力（拡張用）

	[[nodiscard]] bool operator==(const InputState& other) const noexcept
	{
		return buttonA == other.buttonA && buttonB == other.buttonB
			&& axisX == other.axisX && axisY == other.axisY;
	}
	[[nodiscard]] bool operator!=(const InputState& other) const noexcept
	{ return !(*this == other); }
};

/// @brief リプレイイベント（ゲーム内で発生した出来事）
struct ReplayEvent
{
	std::string type;         ///< イベント種別
	std::string description;  ///< 詳細説明
	std::map<std::string, std::string> data; ///< 任意key-value
};

/// @brief リプレイメタデータ
struct ReplayMetadata
{
	std::uint32_t randomSeed = 0;  ///< 乱数シード
	std::string version;            ///< ゲームバージョン
	std::string timestamp;          ///< 記録日時
};

/// @brief 1フレーム分のリプレイ記録
struct FrameRecord
{
	std::uint64_t frame = 0;              ///< フレーム番号
	InputState input;                      ///< 入力状態
	std::vector<ReplayEvent> events;      ///< このフレームのイベント
};

/// @brief リプレイデータ全体
struct ReplayData
{
	/// @brief マジックナンバー
	static constexpr std::uint32_t kMagic = 0x4D52504C; // "MRPL"
	/// @brief 現在のフォーマットバージョン
	static constexpr std::uint32_t kFormatVersion = 1;

	ReplayMetadata metadata;              ///< メタデータ
	std::vector<FrameRecord> frames;      ///< フレームごとの記録

	/// @brief 総フレーム数を返す
	[[nodiscard]] std::size_t totalFrames() const noexcept
	{
		return frames.size();
	}
};

/// @brief リプレイ記録器
/// @details ゲーム実行中にフレーム単位で入力とイベントを記録する。
class ReplayRecorder
{
public:
	/// @brief 記録を開始する
	/// @param randomSeed 乱数シード（決定論的再現のため）
	/// @param version ゲームバージョン文字列
	void beginRecording(std::uint32_t randomSeed = 0,
		const std::string& version = "")
	{
		m_data = ReplayData{};
		m_data.metadata.randomSeed = randomSeed;
		m_data.metadata.version = version;
		m_recording = true;
	}

	/// @brief 入力状態を記録する
	/// @param frame フレーム番号
	/// @param input 入力状態
	void recordInput(std::uint64_t frame, const InputState& input)
	{
		if (!m_recording) return;

		auto* record = getOrCreateFrame(frame);
		record->input = input;
	}

	/// @brief イベントを記録する
	/// @param frame フレーム番号
	/// @param event リプレイイベント
	void recordEvent(std::uint64_t frame, ReplayEvent event)
	{
		if (!m_recording) return;

		auto* record = getOrCreateFrame(frame);
		record->events.push_back(std::move(event));
	}

	/// @brief CausalEventをリプレイイベントとして記録する
	/// @param causalEvent 因果イベント
	void recordCausalEvent(const observe::CausalEvent& causalEvent)
	{
		if (!m_recording) return;

		ReplayEvent event;
		event.type = causalEvent.type;
		event.description = causalEvent.description;
		event.data = causalEvent.data;

		recordEvent(causalEvent.frame, std::move(event));
	}

	/// @brief 記録を終了しリプレイデータを返す
	/// @return 記録されたリプレイデータ
	[[nodiscard]] ReplayData endRecording()
	{
		m_recording = false;

		/// フレーム番号順にソート
		std::sort(m_data.frames.begin(), m_data.frames.end(),
			[](const FrameRecord& a, const FrameRecord& b) {
				return a.frame < b.frame;
			});

		return m_data;
	}

	/// @brief 記録中かどうかを返す
	[[nodiscard]] bool isRecording() const noexcept
	{
		return m_recording;
	}

	/// @brief リプレイデータをファイルに保存する
	[[nodiscard]] static bool saveToFile(
		const std::string& path, const ReplayData& data)
	{
		std::ofstream f(path, std::ios::binary);
		if (!f.is_open()) return false;
		writeU32(f, ReplayData::kMagic);
		writeU32(f, ReplayData::kFormatVersion);
		writeU32(f, data.metadata.randomSeed);
		writeString(f, data.metadata.version);
		writeString(f, data.metadata.timestamp);
		writeU32(f, static_cast<uint32_t>(data.frames.size()));
		for (const auto& fr : data.frames)
		{
			writeU64(f, fr.frame);
			writeBool(f, fr.input.buttonA);
			writeBool(f, fr.input.buttonB);
			writeFloat(f, fr.input.axisX);
			writeFloat(f, fr.input.axisY);
			writeU32(f, static_cast<uint32_t>(fr.events.size()));
			for (const auto& ev : fr.events)
			{
				writeString(f, ev.type);
				writeString(f, ev.description);
				writeU32(f, static_cast<uint32_t>(ev.data.size()));
				for (const auto& [k, v] : ev.data)
				{ writeString(f, k); writeString(f, v); }
			}
		}
		return f.good();
	}

private:
	/// @brief フレームレコードを取得または作成する
	/// @param frame フレーム番号
	/// @return フレームレコードへのポインタ
	FrameRecord* getOrCreateFrame(std::uint64_t frame)
	{
		for (auto& record : m_data.frames)
		{
			if (record.frame == frame)
			{
				return &record;
			}
		}

		m_data.frames.push_back(FrameRecord{frame, {}, {}});
		return &m_data.frames.back();
	}

	/// バイナリ書き込みヘルパー
	static void writeU32(std::ofstream& f, std::uint32_t v)
	{ f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
	static void writeU64(std::ofstream& f, std::uint64_t v)
	{ f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
	static void writeFloat(std::ofstream& f, float v)
	{ f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
	static void writeBool(std::ofstream& f, bool v)
	{ uint8_t b = v ? 1 : 0; f.write(reinterpret_cast<const char*>(&b), 1); }
	static void writeString(std::ofstream& f, const std::string& s)
	{
		writeU32(f, static_cast<uint32_t>(s.size()));
		if (!s.empty()) f.write(s.data(), static_cast<std::streamsize>(s.size()));
	}

	ReplayData m_data;       ///< 記録中のデータ
	bool m_recording = false; ///< 記録中フラグ
};

/// @brief リプレイ再生器
/// @details ファイルからリプレイデータを読み込み、フレーム単位で入力とイベントを取得する。
class ReplayPlayer
{
public:
	/// @brief ファイルからリプレイデータを読み込む
	[[nodiscard]] static std::optional<ReplayData> loadFromFile(
		const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f.is_open()) return std::nullopt;
		if (readU32(f) != ReplayData::kMagic) return std::nullopt;
		if (readU32(f) != ReplayData::kFormatVersion) return std::nullopt;

		ReplayData data;
		data.metadata.randomSeed = readU32(f);
		data.metadata.version = readString(f);
		data.metadata.timestamp = readString(f);
		const auto fc = readU32(f);
		data.frames.reserve(fc);
		for (uint32_t i = 0; i < fc; ++i)
		{
			FrameRecord rec;
			rec.frame = readU64(f);
			rec.input.buttonA = readBool(f);
			rec.input.buttonB = readBool(f);
			rec.input.axisX = readFloat(f);
			rec.input.axisY = readFloat(f);
			const auto ec = readU32(f);
			rec.events.reserve(ec);
			for (uint32_t j = 0; j < ec; ++j)
			{
				ReplayEvent ev;
				ev.type = readString(f);
				ev.description = readString(f);
				const auto kc = readU32(f);
				for (uint32_t k = 0; k < kc; ++k)
				{ auto key = readString(f); ev.data[std::move(key)] = readString(f); }
				rec.events.push_back(std::move(ev));
			}
			data.frames.push_back(std::move(rec));
		}
		if (!f.good()) return std::nullopt;
		return data;
	}

	/// @brief 指定フレームの入力状態を取得する
	/// @param data リプレイデータ
	/// @param frame フレーム番号
	/// @return 入力状態（該当フレームがなければ nullopt）
	[[nodiscard]] static std::optional<InputState> getInput(
		const ReplayData& data, std::uint64_t frame)
	{
		const auto* record = findFrame(data, frame);
		if (record == nullptr) return std::nullopt;
		return record->input;
	}

	/// @brief 指定フレームのイベント一覧を取得する
	/// @param data リプレイデータ
	/// @param frame フレーム番号
	/// @return イベント一覧（該当フレームがなければ空ベクタ）
	[[nodiscard]] static std::vector<ReplayEvent> getEvents(
		const ReplayData& data, std::uint64_t frame)
	{
		const auto* record = findFrame(data, frame);
		if (record == nullptr) return {};
		return record->events;
	}

	/// @brief 指定フレームが最終フレーム以降かを判定する
	/// @param data リプレイデータ
	/// @param frame フレーム番号
	/// @return 最終フレームを超えていれば true
	[[nodiscard]] static bool isFinished(
		const ReplayData& data, std::uint64_t frame) noexcept
	{
		if (data.frames.empty()) return true;
		return frame > data.frames.back().frame;
	}

	/// @brief リプレイデータからCausalChainを再構築する
	/// @param data リプレイデータ
	/// @param chain [out] 因果チェーン
	/// @details リプレイ中の全イベントをCausalChainに記録する。
	///          因果関係のリンクは同一フレーム内のイベント間で構築する。
	static void buildCausalChain(
		const ReplayData& data, observe::CausalChain& chain)
	{
		chain.clear();

		for (const auto& frame : data.frames)
		{
			observe::CausalEventId prevId =
				observe::INVALID_CAUSAL_EVENT;

			for (const auto& event : frame.events)
			{
				prevId = chain.record(
					event.type,
					event.description,
					frame.frame,
					prevId,
					event.data);
			}
		}
	}

private:
	/// @brief フレームレコードを検索する
	[[nodiscard]] static const FrameRecord* findFrame(
		const ReplayData& data, std::uint64_t frame) noexcept
	{
		for (const auto& record : data.frames)
		{
			if (record.frame == frame) return &record;
		}
		return nullptr;
	}

	/// バイナリ読み込みヘルパー
	[[nodiscard]] static std::uint32_t readU32(std::ifstream& f)
	{ std::uint32_t v = 0; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; }
	[[nodiscard]] static std::uint64_t readU64(std::ifstream& f)
	{ std::uint64_t v = 0; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; }
	[[nodiscard]] static float readFloat(std::ifstream& f)
	{ float v = 0.0f; f.read(reinterpret_cast<char*>(&v), sizeof(v)); return v; }
	[[nodiscard]] static bool readBool(std::ifstream& f)
	{ uint8_t b = 0; f.read(reinterpret_cast<char*>(&b), sizeof(b)); return b != 0; }
	[[nodiscard]] static std::string readString(std::ifstream& f)
	{
		const auto len = readU32(f);
		if (len == 0) return {};
		std::string s(len, '\0');
		f.read(s.data(), static_cast<std::streamsize>(len));
		return s;
	}
};

} // namespace mitiru::core
