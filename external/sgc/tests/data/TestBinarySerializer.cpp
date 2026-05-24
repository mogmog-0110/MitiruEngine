#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdio>
#include <filesystem>
#include <string>

#include "sgc/data/BinarySerializer.hpp"
#include "sgc/data/BinaryDeserializer.hpp"

using sgc::data::BinarySerializer;
using sgc::data::BinaryDeserializer;

TEST_CASE("BinarySerializer - round trip U8 U16 U32 U64", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeU8(0xFF);
	ser.writeU16(0x1234);
	ser.writeU32(0xDEADBEEF);
	ser.writeU64(0x0102030405060708ULL);

	BinaryDeserializer des(ser.data());
	REQUIRE(des.readU8() == 0xFF);
	REQUIRE(des.readU16() == 0x1234);
	REQUIRE(des.readU32() == 0xDEADBEEF);
	REQUIRE(des.readU64() == 0x0102030405060708ULL);
	REQUIRE_FALSE(des.hasRemaining());
}

TEST_CASE("BinarySerializer - round trip signed integers", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeI32(-42);
	ser.writeI64(-9876543210LL);

	BinaryDeserializer des(ser.data());
	REQUIRE(des.readI32() == -42);
	REQUIRE(des.readI64() == -9876543210LL);
}

TEST_CASE("BinarySerializer - round trip float and double", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeF32(3.14f);
	ser.writeF64(2.718281828459045);

	BinaryDeserializer des(ser.data());
	REQUIRE_THAT(des.readF32(), Catch::Matchers::WithinRel(3.14f, 1e-6f));
	REQUIRE_THAT(des.readF64(), Catch::Matchers::WithinRel(2.718281828459045, 1e-12));
}

TEST_CASE("BinarySerializer - round trip bool", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeBool(true);
	ser.writeBool(false);
	ser.writeBool(true);

	BinaryDeserializer des(ser.data());
	REQUIRE(des.readBool() == true);
	REQUIRE(des.readBool() == false);
	REQUIRE(des.readBool() == true);
}

TEST_CASE("BinarySerializer - round trip string", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeString("Hello, World!");
	ser.writeString("");
	ser.writeString("test123");

	BinaryDeserializer des(ser.data());
	REQUIRE(des.readString() == "Hello, World!");
	REQUIRE(des.readString() == "");
	REQUIRE(des.readString() == "test123");
	REQUIRE_FALSE(des.hasRemaining());
}

TEST_CASE("BinarySerializer - round trip raw bytes", "[data][binary]")
{
	const uint8_t original[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	BinarySerializer ser;
	ser.writeBytes(original, sizeof(original));

	REQUIRE(ser.size() == 5);

	BinaryDeserializer des(ser.data());
	uint8_t loaded[5] = {};
	des.readBytes(loaded, sizeof(loaded));

	for (size_t i = 0; i < 5; ++i)
	{
		REQUIRE(loaded[i] == original[i]);
	}
}

TEST_CASE("BinarySerializer - size and clear", "[data][binary]")
{
	BinarySerializer ser;
	REQUIRE(ser.size() == 0);

	ser.writeU32(1);
	ser.writeU32(2);
	REQUIRE(ser.size() == 8);

	ser.clear();
	REQUIRE(ser.size() == 0);
	REQUIRE(ser.data().empty());
}

TEST_CASE("BinarySerializer - file round trip", "[data][binary]")
{
	const std::string path = "test_binary_roundtrip.bin";

	BinarySerializer ser;
	ser.writeU32(12345);
	ser.writeString("file test");
	ser.writeF32(99.9f);
	REQUIRE(ser.saveToFile(path));

	auto des = BinaryDeserializer::fromFile(path);
	REQUIRE(des.readU32() == 12345);
	REQUIRE(des.readString() == "file test");
	REQUIRE_THAT(des.readF32(), Catch::Matchers::WithinRel(99.9f, 1e-5f));
	REQUIRE_FALSE(des.hasRemaining());

	std::remove(path.c_str());
}

TEST_CASE("BinaryDeserializer - read overflow throws", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeU8(42);

	BinaryDeserializer des(ser.data());
	REQUIRE(des.readU8() == 42);
	REQUIRE(des.remaining() == 0);

	REQUIRE_THROWS_AS(des.readU8(), std::runtime_error);
}

TEST_CASE("BinaryDeserializer - position tracking", "[data][binary]")
{
	BinarySerializer ser;
	ser.writeU32(1);
	ser.writeU32(2);

	BinaryDeserializer des(ser.data());
	REQUIRE(des.position() == 0);
	REQUIRE(des.remaining() == 8);

	des.readU32();
	REQUIRE(des.position() == 4);
	REQUIRE(des.remaining() == 4);

	des.readU32();
	REQUIRE(des.position() == 8);
	REQUIRE(des.remaining() == 0);
	REQUIRE_FALSE(des.hasRemaining());
}
