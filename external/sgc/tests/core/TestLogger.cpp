/// @file TestLogger.cpp
/// @brief Logger.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/Logger.hpp"

#include <string>
#include <vector>

/// @brief テスト用Sink — メッセージをvectorに蓄積する
struct TestSink
{
	std::vector<std::string>* messages;

	void write(const sgc::LogMessage& msg) const
	{
		messages->push_back(std::string(sgc::logLevelToString(msg.level)) + ": " + msg.text);
	}
};

TEST_CASE("Logger default level is Info", "[core][logger]")
{
	sgc::Logger logger;
	REQUIRE(logger.getLevel() == sgc::LogLevel::Info);
}

TEST_CASE("Logger setLevel changes level", "[core][logger]")
{
	sgc::Logger logger;
	logger.setLevel(sgc::LogLevel::Debug);
	REQUIRE(logger.getLevel() == sgc::LogLevel::Debug);
}

TEST_CASE("Logger filters messages below level", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Warn);

	SGC_LOG_DEBUG(logger, "should be filtered");
	SGC_LOG_INFO(logger, "should be filtered too");
	SGC_LOG_WARN(logger, "this should pass");
	SGC_LOG_ERROR(logger, "this too");

	REQUIRE(messages.size() == 2);
	REQUIRE(messages[0] == "WARN: this should pass");
	REQUIRE(messages[1] == "ERROR: this too");
}

TEST_CASE("Logger format with arguments", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Trace);

	SGC_LOG_INFO(logger, "Player {} took {} damage", "Alice", 42);

	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == "INFO: Player Alice took 42 damage");
}

TEST_CASE("Logger Off level suppresses all messages", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Off);

	SGC_LOG_FATAL(logger, "should not appear");

	REQUIRE(messages.empty());
}

TEST_CASE("logLevelToString returns correct strings", "[core][logger]")
{
	REQUIRE(std::string(sgc::logLevelToString(sgc::LogLevel::Trace)) == "TRACE");
	REQUIRE(std::string(sgc::logLevelToString(sgc::LogLevel::Debug)) == "DEBUG");
	REQUIRE(std::string(sgc::logLevelToString(sgc::LogLevel::Info)) == "INFO");
	REQUIRE(std::string(sgc::logLevelToString(sgc::LogLevel::Warn)) == "WARN");
	REQUIRE(std::string(sgc::logLevelToString(sgc::LogLevel::Error)) == "ERROR");
	REQUIRE(std::string(sgc::logLevelToString(sgc::LogLevel::Fatal)) == "FATAL");
}

TEST_CASE("Logger multiple sinks receive same message", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> sink1Messages;
	std::vector<std::string> sink2Messages;

	logger.addSink(TestSink{&sink1Messages});
	logger.addSink(TestSink{&sink2Messages});
	logger.setLevel(sgc::LogLevel::Trace);

	SGC_LOG_INFO(logger, "hello");

	REQUIRE(sink1Messages.size() == 1);
	REQUIRE(sink2Messages.size() == 1);
	REQUIRE(sink1Messages[0] == sink2Messages[0]);
}

// ── convenience methods (non-macro) ─────────────────────────────

TEST_CASE("Logger info method works without format args", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Trace);

	logger.info("simple message");

	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == "INFO: simple message");
}

TEST_CASE("Logger info method works with format args", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Trace);

	logger.info("Player {} took {} damage", "Alice", 42);

	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0] == "INFO: Player Alice took 42 damage");
}

TEST_CASE("Logger all convenience methods work", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Trace);

	logger.trace("t {}", 1);
	logger.debug("d {}", 2);
	logger.info("i {}", 3);
	logger.warn("w {}", 4);
	logger.error("e {}", 5);
	logger.fatal("f {}", 6);

	REQUIRE(messages.size() == 6);
	REQUIRE(messages[0] == "TRACE: t 1");
	REQUIRE(messages[1] == "DEBUG: d 2");
	REQUIRE(messages[2] == "INFO: i 3");
	REQUIRE(messages[3] == "WARN: w 4");
	REQUIRE(messages[4] == "ERROR: e 5");
	REQUIRE(messages[5] == "FATAL: f 6");
}

TEST_CASE("Logger convenience methods respect level filter", "[core][logger]")
{
	sgc::Logger logger;
	logger.clearSinks();

	std::vector<std::string> messages;
	logger.addSink(TestSink{&messages});
	logger.setLevel(sgc::LogLevel::Error);

	logger.debug("filtered");
	logger.info("filtered");
	logger.warn("filtered");
	logger.error("passes");
	logger.fatal("passes");

	REQUIRE(messages.size() == 2);
	REQUIRE(messages[0] == "ERROR: passes");
	REQUIRE(messages[1] == "FATAL: passes");
}
