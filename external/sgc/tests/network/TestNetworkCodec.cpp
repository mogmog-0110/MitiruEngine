#include <catch2/catch_test_macros.hpp>
#include "sgc/network/NetworkCodec.hpp"

using namespace sgc::network;

TEST_CASE("NetworkCodec encodes header correctly", "[network][codec]")
{
	MessageHeader header;
	header.messageType = 42;
	header.sequenceNumber = 100;
	header.payloadSize = 256;
	header.timestamp = 9999;

	auto encoded = NetworkCodec::encodeHeader(header);
	REQUIRE(encoded.size() == NetworkCodec::HEADER_SIZE);
}

TEST_CASE("NetworkCodec decodes header correctly", "[network][codec]")
{
	MessageHeader original;
	original.messageType = 42;
	original.sequenceNumber = 100;
	original.payloadSize = 256;
	original.timestamp = 9999;

	auto encoded = NetworkCodec::encodeHeader(original);
	auto decoded = NetworkCodec::decodeHeader(encoded);

	REQUIRE(decoded.has_value());
	REQUIRE(decoded->messageType == 42);
	REQUIRE(decoded->sequenceNumber == 100);
	REQUIRE(decoded->payloadSize == 256);
	REQUIRE(decoded->timestamp == 9999);
}

TEST_CASE("NetworkCodec packs and unpacks packets", "[network][codec]")
{
	std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};

	auto packet = NetworkCodec::pack(7, 55, payload);
	REQUIRE(packet.size() == NetworkCodec::HEADER_SIZE + payload.size());

	auto result = NetworkCodec::unpack(packet);
	REQUIRE(result.has_value());

	auto& [header, data] = *result;
	REQUIRE(header.messageType == 7);
	REQUIRE(header.sequenceNumber == 55);
	REQUIRE(header.payloadSize == 4);
	REQUIRE(data.size() == 4);
	REQUIRE(data[0] == 0x01);
	REQUIRE(data[3] == 0x04);
}

TEST_CASE("NetworkCodec handles empty payload", "[network][codec]")
{
	std::vector<uint8_t> emptyPayload;
	auto packet = NetworkCodec::pack(1, 0, emptyPayload);

	auto result = NetworkCodec::unpack(packet);
	REQUIRE(result.has_value());
	REQUIRE(result->first.payloadSize == 0);
	REQUIRE(result->second.empty());
}

TEST_CASE("NetworkCodec returns nullopt for invalid data", "[network][codec]")
{
	// データが短すぎる場合
	std::vector<uint8_t> tooShort = {0x01, 0x02};
	REQUIRE_FALSE(NetworkCodec::decodeHeader(tooShort).has_value());
	REQUIRE_FALSE(NetworkCodec::unpack(tooShort).has_value());

	// ヘッダーはあるがペイロードが不足している場合
	MessageHeader header;
	header.messageType = 1;
	header.sequenceNumber = 0;
	header.payloadSize = 100;  // 100バイトのペイロードを宣言
	header.timestamp = 0;

	auto encoded = NetworkCodec::encodeHeader(header);
	// ペイロードを付けない
	REQUIRE_FALSE(NetworkCodec::unpack(encoded).has_value());
}
