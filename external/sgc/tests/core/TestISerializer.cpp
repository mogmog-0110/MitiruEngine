#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/core/ISerializer.hpp"

using namespace sgc;
using Approx = Catch::Approx;

TEST_CASE("JsonSerializer - write and read int", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeInt("score", 42);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readInt("score") == 42);
	REQUIRE(des.hasKey("score"));
}

TEST_CASE("JsonSerializer - write and read float", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeFloat("pi", 3.14f);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readFloat("pi") == Approx(3.14f).margin(0.01f));
}

TEST_CASE("JsonSerializer - write and read string", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeString("name", "Hero");
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readString("name") == "Hero");
}

TEST_CASE("JsonSerializer - write and read bool", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeBool("active", true);
	ser.writeBool("dead", false);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readBool("active") == true);
	REQUIRE(des.readBool("dead") == false);
}

TEST_CASE("JsonSerializer - multiple values", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeString("name", "Test");
	ser.writeInt("level", 10);
	ser.writeFloat("hp", 85.5f);
	ser.writeBool("alive", true);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readString("name") == "Test");
	REQUIRE(des.readInt("level") == 10);
	REQUIRE(des.readFloat("hp") == Approx(85.5f).margin(0.1f));
	REQUIRE(des.readBool("alive") == true);
}

TEST_CASE("JsonSerializer - nested object", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeString("name", "Player");
	ser.beginObject("stats");
	ser.writeInt("str", 15);
	ser.writeInt("dex", 12);
	ser.endObject();
	ser.writeInt("level", 5);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readString("name") == "Player");
	REQUIRE(des.readInt("level") == 5);
	REQUIRE(des.hasKey("stats"));
}

TEST_CASE("JsonSerializer - array", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.beginArray("items");
	ser.endArray();
	ser.writeInt("count", 3);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readInt("count") == 3);
	REQUIRE(des.hasKey("items"));
}

TEST_CASE("JsonDeserializer - missing key returns default", "[core][ISerializer]")
{
	JsonDeserializer des("{}");
	REQUIRE(des.readInt("missing") == 0);
	REQUIRE(des.readFloat("missing") == 0.0f);
	REQUIRE(des.readString("missing").empty());
	REQUIRE(des.readBool("missing") == false);
	REQUIRE_FALSE(des.hasKey("missing"));
}

TEST_CASE("JsonSerializer - string with special characters", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeString("text", "hello \"world\"\nnewline");
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readString("text") == "hello \"world\"\nnewline");
}

TEST_CASE("JsonSerializer - negative numbers", "[core][ISerializer]")
{
	JsonSerializer ser;
	ser.writeInt("neg", -100);
	ser.writeFloat("negf", -3.5f);
	std::string json = ser.finalize();

	JsonDeserializer des(json);
	REQUIRE(des.readInt("neg") == -100);
	REQUIRE(des.readFloat("negf") == Approx(-3.5f).margin(0.01f));
}

TEST_CASE("JsonSerializer - ISerializer interface usage", "[core][ISerializer]")
{
	JsonSerializer ser;
	ISerializer& iface = ser;
	iface.writeInt("x", 10);
	iface.writeString("y", "hello");
	std::string json = iface.finalize();

	JsonDeserializer des(json);
	const IDeserializer& diface = des;
	REQUIRE(diface.readInt("x") == 10);
	REQUIRE(diface.readString("y") == "hello");
}

TEST_CASE("JsonSerializer - empty serializer produces empty object", "[core][ISerializer]")
{
	JsonSerializer ser;
	std::string json = ser.finalize();
	REQUIRE(json == "{}");
}
