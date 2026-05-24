#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "sgc/net/MessageChannel.hpp"

TEST_CASE("MessageBuffer starts empty", "[net][messagebuffer]")
{
	sgc::MessageBuffer buf;
	REQUIRE(buf.empty());
	REQUIRE(buf.size() == 0);
}

TEST_CASE("MessageBuffer write trivial types", "[net][messagebuffer]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint32_t>(42);
	buf.write<float>(3.14f);

	REQUIRE(buf.size() == sizeof(std::uint32_t) + sizeof(float));
	REQUIRE_FALSE(buf.empty());
}

TEST_CASE("MessageBuffer write and read round-trip uint32", "[net][messagebuffer]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint32_t>(12345);

	auto reader = buf.createReader();
	auto result = reader.read<std::uint32_t>();
	REQUIRE(result.hasValue());
	REQUIRE(result.value() == 12345);
}

TEST_CASE("MessageBuffer write and read multiple types", "[net][messagebuffer]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint32_t>(100);
	buf.write<float>(2.5f);
	buf.write<std::int16_t>(-42);

	auto reader = buf.createReader();
	REQUIRE(reader.read<std::uint32_t>().value() == 100);
	REQUIRE(reader.read<float>().value() == 2.5f);
	REQUIRE(reader.read<std::int16_t>().value() == -42);
}

TEST_CASE("MessageBuffer write raw bytes", "[net][messagebuffer]")
{
	sgc::MessageBuffer buf;
	const char hello[] = "hello";
	buf.write(hello, 5);

	REQUIRE(buf.size() == 5);

	auto reader = buf.createReader();
	char result[5] = {};
	auto readResult = reader.read(result, 5);
	REQUIRE(readResult.hasValue());
	REQUIRE(readResult.value() == 5);
	REQUIRE(std::string(result, 5) == "hello");
}

TEST_CASE("MessageBuffer clear empties buffer", "[net][messagebuffer]")
{
	sgc::MessageBuffer buf;
	buf.write<int>(42);
	REQUIRE_FALSE(buf.empty());

	buf.clear();
	REQUIRE(buf.empty());
	REQUIRE(buf.size() == 0);
}

TEST_CASE("MessageReader remaining tracks position", "[net][messagereader]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint32_t>(1);
	buf.write<std::uint32_t>(2);

	auto reader = buf.createReader();
	REQUIRE(reader.remaining() == 8);

	(void)reader.read<std::uint32_t>();
	REQUIRE(reader.remaining() == 4);

	(void)reader.read<std::uint32_t>();
	REQUIRE(reader.remaining() == 0);
}

TEST_CASE("MessageReader returns error on out of range", "[net][messagereader]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint8_t>(1);

	auto reader = buf.createReader();
	(void)reader.read<std::uint8_t>();

	auto result = reader.read<std::uint32_t>();
	REQUIRE(result.hasError());
}

TEST_CASE("MessageReader raw read returns error on out of range", "[net][messagereader]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint8_t>(1);

	auto reader = buf.createReader();
	char dest[16] = {};
	auto result = reader.read(dest, 16);
	REQUIRE(result.hasError());
}

TEST_CASE("MessageReader reset restarts reading", "[net][messagereader]")
{
	sgc::MessageBuffer buf;
	buf.write<std::uint32_t>(42);

	auto reader = buf.createReader();
	REQUIRE(reader.read<std::uint32_t>().value() == 42);
	REQUIRE(reader.remaining() == 0);

	reader.reset();
	REQUIRE(reader.remaining() == sizeof(std::uint32_t));
	REQUIRE(reader.read<std::uint32_t>().value() == 42);
}

TEST_CASE("MessageHeader fields", "[net][messageheader]")
{
	sgc::MessageHeader header;
	header.type = 1;
	header.size = 100;

	REQUIRE(header.type == 1);
	REQUIRE(header.size == 100);
}

TEST_CASE("MessageDispatcher starts with no handlers", "[net][dispatcher]")
{
	sgc::MessageDispatcher dispatcher;
	REQUIRE(dispatcher.handlerCount() == 0);
	REQUIRE_FALSE(dispatcher.hasHandler(1));
}

TEST_CASE("MessageDispatcher register and dispatch", "[net][dispatcher]")
{
	sgc::MessageDispatcher dispatcher;
	int received = 0;

	dispatcher.registerHandler(1, [&](const sgc::MessageHeader&, sgc::MessageReader& reader) {
		received = reader.read<int>().value();
	});

	REQUIRE(dispatcher.handlerCount() == 1);
	REQUIRE(dispatcher.hasHandler(1));

	sgc::MessageBuffer buf;
	buf.write<int>(42);
	sgc::MessageHeader header{1, static_cast<std::uint32_t>(buf.size())};

	bool dispatched = dispatcher.dispatch(header, buf);
	REQUIRE(dispatched);
	REQUIRE(received == 42);
}

TEST_CASE("MessageDispatcher returns false for unknown type", "[net][dispatcher]")
{
	sgc::MessageDispatcher dispatcher;

	sgc::MessageBuffer buf;
	sgc::MessageHeader header{99, 0};

	REQUIRE_FALSE(dispatcher.dispatch(header, buf));
}

TEST_CASE("MessageDispatcher unregister removes handler", "[net][dispatcher]")
{
	sgc::MessageDispatcher dispatcher;
	dispatcher.registerHandler(1, [](const sgc::MessageHeader&, sgc::MessageReader&) {});

	REQUIRE(dispatcher.hasHandler(1));
	dispatcher.unregisterHandler(1);
	REQUIRE_FALSE(dispatcher.hasHandler(1));
	REQUIRE(dispatcher.handlerCount() == 0);
}

TEST_CASE("MessageDispatcher clear removes all handlers", "[net][dispatcher]")
{
	sgc::MessageDispatcher dispatcher;
	dispatcher.registerHandler(1, [](const sgc::MessageHeader&, sgc::MessageReader&) {});
	dispatcher.registerHandler(2, [](const sgc::MessageHeader&, sgc::MessageReader&) {});

	dispatcher.clear();
	REQUIRE(dispatcher.handlerCount() == 0);
}

TEST_CASE("MessageDispatcher multiple handlers", "[net][dispatcher]")
{
	sgc::MessageDispatcher dispatcher;
	int received1 = 0;
	int received2 = 0;

	dispatcher.registerHandler(1, [&](const sgc::MessageHeader&, sgc::MessageReader& reader) {
		received1 = reader.read<int>().value();
	});
	dispatcher.registerHandler(2, [&](const sgc::MessageHeader&, sgc::MessageReader& reader) {
		received2 = reader.read<int>().value();
	});

	sgc::MessageBuffer buf1;
	buf1.write<int>(10);
	dispatcher.dispatch({1, sizeof(int)}, buf1);

	sgc::MessageBuffer buf2;
	buf2.write<int>(20);
	dispatcher.dispatch({2, sizeof(int)}, buf2);

	REQUIRE(received1 == 10);
	REQUIRE(received2 == 20);
}
