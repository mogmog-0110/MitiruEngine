#pragma once

/// @file NetworkCodec.hpp
/// @brief ネットワークメッセージのエンコード/デコード
///
/// メッセージヘッダーとペイロードのバイナリシリアライゼーションを提供する。
/// リトルエンディアンでのパッキング・アンパッキングを行う。
///
/// @code
/// auto packet = sgc::network::NetworkCodec::pack(1, 0, payload);
/// auto result = sgc::network::NetworkCodec::unpack(packet);
/// if (result)
/// {
///     auto& [header, payload] = *result;
///     // header.messageType, header.sequenceNumber ...
/// }
/// @endcode

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace sgc::network
{

/// @brief メッセージヘッダー
///
/// パケットの先頭に付与されるメタ情報。
struct MessageHeader
{
	uint16_t messageType = 0;     ///< メッセージ種別
	uint32_t sequenceNumber = 0;  ///< シーケンス番号
	uint32_t payloadSize = 0;     ///< ペイロードサイズ（バイト）
	uint32_t timestamp = 0;       ///< タイムスタンプ
};

/// @brief ネットワークメッセージのエンコード/デコード
///
/// メッセージヘッダーとペイロードをバイト列に変換する静的ユーティリティ。
class NetworkCodec
{
public:
	/// @brief ヘッダーサイズ（バイト）
	static constexpr size_t HEADER_SIZE =
		sizeof(uint16_t) + sizeof(uint32_t) * 3;

	/// @brief ヘッダーをバイト列にエンコードする
	/// @param header エンコードするヘッダー
	/// @return エンコードされたバイト列
	static std::vector<uint8_t> encodeHeader(const MessageHeader& header)
	{
		std::vector<uint8_t> data(HEADER_SIZE);
		size_t offset = 0;
		writeValue(data, offset, header.messageType);
		writeValue(data, offset, header.sequenceNumber);
		writeValue(data, offset, header.payloadSize);
		writeValue(data, offset, header.timestamp);
		return data;
	}

	/// @brief バイト列からヘッダーをデコードする
	/// @param data デコード元のバイト列
	/// @return 成功時はヘッダー、失敗時はnullopt
	static std::optional<MessageHeader> decodeHeader(
		std::span<const uint8_t> data)
	{
		if (data.size() < HEADER_SIZE)
		{
			return std::nullopt;
		}

		MessageHeader header;
		size_t offset = 0;
		header.messageType = readValue<uint16_t>(data, offset);
		header.sequenceNumber = readValue<uint32_t>(data, offset);
		header.payloadSize = readValue<uint32_t>(data, offset);
		header.timestamp = readValue<uint32_t>(data, offset);
		return header;
	}

	/// @brief ヘッダー+ペイロードをパケットにパッキングする
	/// @param messageType メッセージ種別
	/// @param seq シーケンス番号
	/// @param payload ペイロードデータ
	/// @return パッキングされたパケット
	static std::vector<uint8_t> pack(
		uint16_t messageType,
		uint32_t seq,
		std::span<const uint8_t> payload)
	{
		MessageHeader header;
		header.messageType = messageType;
		header.sequenceNumber = seq;
		header.payloadSize = static_cast<uint32_t>(payload.size());
		header.timestamp = 0;

		auto packet = encodeHeader(header);
		packet.insert(packet.end(), payload.begin(), payload.end());
		return packet;
	}

	/// @brief パケットからヘッダーとペイロードをアンパッキングする
	/// @param packet アンパッキングするパケット
	/// @return 成功時はヘッダーとペイロードのペア、失敗時はnullopt
	static std::optional<std::pair<MessageHeader, std::vector<uint8_t>>>
	unpack(std::span<const uint8_t> packet)
	{
		auto header = decodeHeader(packet);
		if (!header)
		{
			return std::nullopt;
		}

		if (packet.size() < HEADER_SIZE + header->payloadSize)
		{
			return std::nullopt;
		}

		std::vector<uint8_t> payload(
			packet.begin() + HEADER_SIZE,
			packet.begin() + HEADER_SIZE + header->payloadSize);

		return std::pair{*header, std::move(payload)};
	}

private:
	/// @brief 値をバイト列に書き込む
	/// @tparam T 書き込む型
	/// @param data 書き込み先
	/// @param offset 書き込み位置（更新される）
	/// @param value 書き込む値
	template <typename T>
	static void writeValue(std::vector<uint8_t>& data, size_t& offset, T value)
	{
		std::memcpy(data.data() + offset, &value, sizeof(T));
		offset += sizeof(T);
	}

	/// @brief バイト列から値を読み出す
	/// @tparam T 読み出す型
	/// @param data 読み出し元
	/// @param offset 読み出し位置（更新される）
	/// @return 読み出した値
	template <typename T>
	static T readValue(std::span<const uint8_t> data, size_t& offset)
	{
		T value{};
		std::memcpy(&value, data.data() + offset, sizeof(T));
		offset += sizeof(T);
		return value;
	}
};

} // namespace sgc::network
