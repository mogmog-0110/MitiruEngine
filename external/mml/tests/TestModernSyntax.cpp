/// @file TestModernSyntax.cpp
/// @brief 拡張文法 (X パラメーター、和音、@n の音色、Q、アルペジエーター) の検査

#include <catch2/catch_test_macros.hpp>

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MmlParser.hpp>

#include <string>
#include <vector>

using namespace mitiru_mml;

namespace
{

TrackTarget extendedTarget()
{
	TrackTarget t;
	t.track = 0;
	t.chip = 0;
	t.channel = 0;
	t.caps.valid = true;
	t.extended = true;
	return t;
}

std::vector<SeqEvent> build(const std::string& mml, bool extended = true)
{
	TrackTarget t = extendedTarget();
	t.extended = extended;
	return MmlEventBuilder::fromMelodic(MmlEventBuilder::expandLoops(MmlParser::parse(mml)), t);
}

int count(const std::vector<SeqEvent>& ev, SeqEventType type)
{
	int n = 0;
	for (const auto& e : ev) n += (e.type == type) ? 1 : 0;
	return n;
}

} // namespace

TEST_CASE("Parser reads X parameter set, ramp and ramp from current", "[mml][modern][syntax]")
{
	const auto cmds = MmlParser::parse("Xcutoff=2000 Xres=40 Xcutoff=500>4000/8 Xcutoff=>800/4. Xwave=square");
	REQUIRE(cmds.size() == 5);
	CHECK(cmds[0].type == CommandType::Param);
	CHECK(cmds[0].value == modern_param::CUTOFF);
	CHECK(cmds[0].extra == 2000);
	CHECK(cmds[0].duration == 0);
	CHECK(cmds[1].value == modern_param::RESONANCE);
	CHECK(cmds[2].extra2 == 500);
	CHECK(cmds[2].extra == 4000);
	CHECK(cmds[2].duration == 8);
	CHECK(cmds[3].extra2 == modern_param::FROM_CURRENT);
	CHECK(cmds[3].extra == 800);
	CHECK(cmds[3].duration == 4);
	CHECK(cmds[3].dotted);
	CHECK(cmds[4].value == modern_param::WAVE);
	CHECK(cmds[4].extra == 1);
}

TEST_CASE("Parser skips an unknown X name instead of reading its letters as notes", "[mml][modern][syntax]")
{
	const auto cmds = MmlParser::parse("Xcutof=2000 C");
	REQUIRE(cmds.size() == 1);
	CHECK(cmds[0].type == CommandType::Note);
}

TEST_CASE("Parser keeps the octave-up command after an X value", "[mml][modern][syntax]")
{
	const auto cmds = MmlParser::parse("Xres=30>C");
	REQUIRE(cmds.size() == 3);
	CHECK(cmds[0].type == CommandType::Param);
	CHECK(cmds[1].type == CommandType::OctaveUp);
	CHECK(cmds[2].type == CommandType::Note);
}

TEST_CASE("Parser reads a chord as one note with a tone mask", "[mml][modern][syntax]")
{
	const auto cmds = MmlParser::parse("'CEG'4. 'C+FA'8");
	REQUIRE(cmds.size() == 2);
	CHECK(cmds[0].type == CommandType::Note);
	CHECK(cmds[0].value == 0);
	CHECK(cmds[0].duration == 4);
	CHECK(cmds[0].dotted);
	CHECK(cmds[0].extra == ((1 << 4) | (1 << 7)));
	CHECK(cmds[1].value == 1);
	CHECK(cmds[1].extra == ((1 << 4) | (1 << 8)));
}

TEST_CASE("Chord tones above the octave go up with the octave marks", "[mml][modern][syntax]")
{
	const auto cmds = MmlParser::parse("'CEG>C'2");
	REQUIRE(cmds.size() == 1);
	CHECK(cmds[0].extra == ((1 << 4) | (1 << 7) | (1 << 12)));
}

TEST_CASE("Extended tracks turn at-number into a voice event and ignore it otherwise", "[mml][modern][syntax]")
{
	const auto on = build("@7 C4");
	REQUIRE(count(on, SeqEventType::Voice) == 1);
	CHECK(on[0].type == SeqEventType::Voice);
	CHECK(on[0].a == 7);
	CHECK(count(build("@7 C4", false), SeqEventType::Voice) == 0);
}

TEST_CASE("Extended tracks make the same events as plain tracks for plain MML", "[mml][modern][syntax]")
{
	const std::string mml = "T120 O4 L8 V12 CDEF&F G4 R4 A2 _B4 <C4";
	const auto a = build(mml, true);
	const auto b = build(mml, false);
	REQUIRE(a.size() == b.size());
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		CHECK(a[i].tick == b[i].tick);
		CHECK(a[i].type == b[i].type);
		CHECK(a[i].a == b[i].a);
		CHECK(a[i].b == b[i].b);
	}
}

TEST_CASE("A chord emits one note-on per tone at the same tick and one note-off", "[mml][modern][syntax]")
{
	const auto ev = build("O4 'CEG'4");
	REQUIRE(count(ev, SeqEventType::NoteOn) == 3);
	REQUIRE(count(ev, SeqEventType::NoteOff) == 1);
	CHECK(ev[0].a == 60);
	CHECK(ev[1].a == 64);
	CHECK(ev[2].a == 67);
	CHECK(ev[0].tick == ev[2].tick);
}

TEST_CASE("A ramp becomes per-tick parameter events that end on the target", "[mml][modern][syntax]")
{
	const auto ev = build("Xcutoff=500>4000/8");
	REQUIRE(!ev.empty());
	CHECK(ev.front().tick == 0);
	CHECK(ev.front().a == modern_param::CUTOFF);
	CHECK(ev.front().b == 500);
	CHECK(ev.back().tick == 24);
	CHECK(ev.back().b == 4000);
	for (std::size_t i = 1; i < ev.size(); ++i)
	{
		CHECK(ev[i].b > ev[i - 1].b);
	}
	// 対数補間: 真ん中 (12 tick) はおよそ幾何平均の 1414 Hz
	for (const auto& e : ev)
	{
		if (e.tick == 12)
		{
			CHECK(e.b > 1380);
			CHECK(e.b < 1450);
		}
	}
}

TEST_CASE("A ramp without a start value begins at the last set value", "[mml][modern][syntax]")
{
	const auto ev = build("Xres=80 Xres=>20/16");
	REQUIRE(ev.size() > 2);
	CHECK(ev[0].b == 80);
	CHECK(ev[1].b == 80);
	CHECK(ev.back().b == 20);
}

TEST_CASE("Quantize shortens the gate on extended tracks", "[mml][modern][syntax]")
{
	const auto full = build("Q8 C4");
	const auto half = build("Q4 C4");
	REQUIRE(full.size() == 2);
	REQUIRE(half.size() == 2);
	CHECK(full[1].tick == 47);
	CHECK(half[1].tick == 24);
}

TEST_CASE("The arpeggiator splits a chord into steps and stays inside the note", "[mml][modern][syntax]")
{
	const auto ev = build("Xarp=up Xarprate=16 Xarpgate=50 O4 'CEG'2");
	std::vector<int> keys;
	std::vector<int> ticks;
	for (const auto& e : ev)
	{
		if (e.type == SeqEventType::NoteOn)
		{
			keys.push_back(e.a);
			ticks.push_back(static_cast<int>(e.tick));
		}
	}
	REQUIRE(keys.size() == 8); // 2 分音符 = 96 tick、16 分 = 12 tick
	CHECK(keys[0] == 60);
	CHECK(keys[1] == 64);
	CHECK(keys[2] == 67);
	CHECK(keys[3] == 60);
	CHECK(ticks[1] - ticks[0] == 12);
	for (const auto& e : ev)
	{
		CHECK(e.tick < 96);
	}
}

TEST_CASE("The arpeggiator directions up-down and down and the octave range", "[mml][modern][syntax]")
{
	const auto collect = [](const std::string& mml)
	{
		std::vector<int> keys;
		for (const auto& e : build(mml))
		{
			if (e.type == SeqEventType::NoteOn) keys.push_back(e.a);
		}
		return keys;
	};
	const auto down = collect("Xarp=down Xarprate=16 O4 'CEG'4");
	REQUIRE(down.size() == 4);
	CHECK(down[0] == 67);
	CHECK(down[1] == 64);
	CHECK(down[2] == 60);
	CHECK(down[3] == 67);

	const auto updown = collect("Xarp=updown Xarprate=16 O4 'CEG'4");
	REQUIRE(updown.size() == 4);
	CHECK(updown[0] == 60);
	CHECK(updown[1] == 64);
	CHECK(updown[2] == 67);
	CHECK(updown[3] == 64);

	const auto twoOct = collect("Xarp=up Xarprate=16 Xarpoct=2 O4 'CEG'2");
	REQUIRE(twoOct.size() >= 7);
	CHECK(twoOct[3] == 72);
	CHECK(twoOct[5] == 79);
}

TEST_CASE("The random arpeggio is the same every time and never repeats a key twice in a row", "[mml][modern][syntax]")
{
	const auto collect = []()
	{
		std::vector<int> keys;
		for (const auto& e : build("Xarp=random Xarprate=16 O4 'CEGB'1"))
		{
			if (e.type == SeqEventType::NoteOn) keys.push_back(e.a);
		}
		return keys;
	};
	const auto a = collect();
	const auto b = collect();
	REQUIRE(a.size() == 16);
	CHECK(a == b);
	for (std::size_t i = 1; i < a.size(); ++i)
	{
		CHECK(a[i] != a[i - 1]);
	}
}

TEST_CASE("A parameter command with a bad value is dropped without leaving its letters as notes", "[mml][modern][syntax]")
{
	const auto cmds = MmlParser::parse("Xwave=bad C Xres=oops D");
	REQUIRE(cmds.size() == 2);
	CHECK(cmds[0].type == CommandType::Note);
	CHECK(cmds[0].value == 0);
	CHECK(cmds[1].type == CommandType::Note);
	CHECK(cmds[1].value == 2);
}
