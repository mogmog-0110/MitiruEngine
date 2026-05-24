// mitiru_subsys_replay — axis 4 (deterministic + replay) P4 deliverable.
//
// 1 binary, 5 modes:
//   --record <path>        : run dummy game for 300 frames + record
//                            InputSnapshot + per-frame game-state blob
//                            (cursor x,y) plus the RNG seed into a v3 file.
//   --record-variant <path>: same as --record but with a slightly different
//                            cursor speed — simulates "a code change". Same
//                            deterministic input, but the resulting state
//                            trace diverges. Use as run B for divergence.
//   --replay <path>        : feed the recorded InputSnapshot stream back into
//                            the same game; HUD shows the file's seed +
//                            totalFrames read from the v3 header.
//   --diff <fileA> <fileB> : compare two replay files frame-by-frame on INPUT
//                            and draw a timeline (silver = match, red = differ).
//   --state-divergence <A> <B> : compare the recorded STATE traces of two runs.
//                            Reports (structured JSON to stdout + detail file)
//                            the first frame where game state diverges and
//                            whether the input matched (→ code-caused vs
//                            input-caused). Renders a timeline window.
//
// What the game does:
//   - Holds a "cursor" position (x,y). Each frame the cursor moves left /
//     right / up / down by 2 px when the corresponding key is reported
//     "down" in the InputSnapshot.
//   - In record mode, the cursor is driven by SYNTHESIZED arrow-key input
//     (Right held for 80 frames, then Down 60, then Left 80, then Up 60).
//   - In replay mode, the cursor is driven by InputSnapshots loaded from
//     the file. Same exact key sequence → same exact trajectory.
//   - At frame 250 the engine takes a PNG screenshot into evidence/.
//   - At frame 300 it requests engine stop.
//
// Why this matters (axis 4 / "Deterministic + 自動リプレイ"):
//   InputSnapshot is the *only* input channel into game logic per ADR 0005.
//   If we faithfully record + replay the InputSnapshot stream, the game's
//   visible output is guaranteed bit-exact between record and replay runs
//   — without any gameplay-side replay scaffolding.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <mitiru/Mitiru.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>
#include <mitiru/replay/Player.hpp>
#include <mitiru/replay/Recorder.hpp>

namespace {

// ── Saturn (silver gray + sober red + ink) palette — must match other subsys demos ──
constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8 silver
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 ink border
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a
constexpr sgc::Colorf kAmberAccent {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c Saturn red
constexpr sgc::Colorf kCursorBody  {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 ink body

constexpr int kTotalFrames       = 300;
constexpr int kScreenshotFrame   = 250;
constexpr float kCursorSpeed     = 2.0f;
constexpr float kCursorSize      = 36.0f;

// Deterministic RNG seed stored in the v2 header at record time. Fixed value
// so two record runs produce byte-identical files (axis 4 demo).
constexpr std::uint64_t kRecordSeed = 0xC8002C2026ULL;

// Synthesize a deterministic InputSnapshot from the frame index. This drives
// the cursor along an L-shaped path: right → down → left → up.
mitiru::module::InputSnapshot synthInput(int frameIdx)
{
	mitiru::module::InputSnapshot snap{};

	// VK code constants (Win32 — DLL InputSnapshot uses VK indices)
	constexpr std::uint8_t kVkLeft  = 0x25;
	constexpr std::uint8_t kVkUp    = 0x26;
	constexpr std::uint8_t kVkRight = 0x27;
	constexpr std::uint8_t kVkDown  = 0x28;

	std::uint8_t held = 0;
	if      (frameIdx <  80) { held = kVkRight; }
	else if (frameIdx < 140) { held = kVkDown;  }
	else if (frameIdx < 220) { held = kVkLeft;  }
	else if (frameIdx < 280) { held = kVkUp;    }
	// frames 280..299: no key held — cursor parks. Lets the final
	// screenshot frame (250) sit inside the "left" segment for visual
	// distinctness from the start position.

	if (held != 0) { snap.keysDown[held] = 1; }
	return snap;
}

class ReplaySampleGame final : public mitiru::Game
{
public:
	enum class Mode : std::uint8_t { Record, Replay };

	// cursorSpeed defaults to kCursorSpeed; the variant record passes a
	// slightly different value to simulate "the code changed".
	ReplaySampleGame(Mode mode, std::string path, float cursorSpeed = kCursorSpeed)
		: m_mode(mode), m_path(std::move(path)), m_cursorSpeed(cursorSpeed) {}

	bool openSink()
	{
		if (m_mode == Mode::Record) { return m_rec.open(m_path, kRecordSeed); }
		return m_player.open(m_path);
	}

	[[nodiscard]] Mode        mode()     const noexcept { return m_mode; }
	[[nodiscard]] std::uint64_t framesRec() const noexcept { return m_rec.frameCount(); }
	[[nodiscard]] std::uint64_t framesPlayed() const noexcept { return m_player.framesRead(); }

	void update(float /*dt*/) override
	{
		// 1. Acquire this frame's input snapshot — synth vs file.
		mitiru::module::InputSnapshot snap{};
		std::uint32_t                 fileFrameIdx = 0;
		if (m_mode == Mode::Record)
		{
			snap = synthInput(m_frame);
		}
		else
		{
			if (!m_player.readNext(snap, fileFrameIdx))
			{
				// EOF / corruption — leave snap zeroed (cursor idles).
			}
		}
		m_lastSnap = snap;

		// 2. Apply input to cursor — same code path for both modes.
		applyInput(snap);

		// 3. In record mode, capture (input + post-update state) for this frame.
		//    State blob = cursor position, the entire observable game state.
		//    Recording AFTER applyInput means the trace reflects the effect of
		//    this frame's logic — which is exactly what divergence compares.
		if (m_mode == Mode::Record)
		{
			const GameStateBlob st{m_cursorX, m_cursorY};
			(void)m_rec.record(static_cast<std::uint32_t>(m_frame), snap,
			                   &st, sizeof(st));
		}

		// 4. Capture screenshot at the deterministic checkpoint frame.
		//    Set the flag *one frame ahead* — engine onFrameStart fires
		//    after the previous frame's present completes (back buffer is
		//    populated and copyable), so requesting at frame N-1 yields
		//    a PNG of frame N's content captured at next frame's start.
		if (m_frame + 1 == kScreenshotFrame && !m_screenshotted)
		{
			m_wantScreenshot = true;
		}

		// 5. Engine stop at the end of the planned session. Hold off until
		//    the screenshot has actually been written.
		if (m_frame >= kTotalFrames && m_screenshotted)
		{
			if (auto* eng = engine()) { eng->requestStop(); }
		}

		++m_frame;
	}

	void draw(mitiru::Screen& screen) override
	{
		m_screenW = static_cast<float>(screen.width());
		m_screenH = static_cast<float>(screen.height());
		if (!m_cursorInit)
		{
			m_cursorX    = m_screenW * 0.25f;
			m_cursorY    = m_screenH * 0.25f;
			m_cursorInit = true;
		}

		screen.clear(kPaperBg);
		drawGrid(screen);
		drawCursor(screen);
		drawHud(screen);
	}

	/// Called from EngineConfig::onFrameStart — the previous frame's
	/// present has completed by then, so back buffer is readable.
	void onFrameStartHook(mitiru::Engine& eng)
	{
		if (m_wantScreenshot && !m_screenshotted)
		{
			takeScreenshot(eng);
			m_wantScreenshot = false;
			m_screenshotted  = true;
		}
	}

	[[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
	{
		return {outsideW, outsideH};
	}

private:
	void applyInput(const mitiru::module::InputSnapshot& snap)
	{
		// Same VK codes used by synthInput. Re-read here so replay path can
		// drive cursor purely from snapshot bytes.
		constexpr std::uint8_t kVkLeft  = 0x25;
		constexpr std::uint8_t kVkUp    = 0x26;
		constexpr std::uint8_t kVkRight = 0x27;
		constexpr std::uint8_t kVkDown  = 0x28;

		// Horizontal motion always uses the base speed; vertical motion uses
		// m_cursorSpeed. The variant run only perturbs m_cursorSpeed, so the
		// state trace stays identical through the initial "right" phase
		// (frames 0..79) and only diverges once the "down" phase begins —
		// giving a mid-stream divergence frame, like a real code regression.
		if (snap.keysDown[kVkLeft])  { m_cursorX -= kCursorSpeed; }
		if (snap.keysDown[kVkRight]) { m_cursorX += kCursorSpeed; }
		if (snap.keysDown[kVkUp])    { m_cursorY -= m_cursorSpeed; }
		if (snap.keysDown[kVkDown])  { m_cursorY += m_cursorSpeed; }
	}

	void drawGrid(mitiru::Screen& screen)
	{
		constexpr float kStep = 32.0f;
		for (float x = kStep; x < m_screenW; x += kStep)
		{
			screen.drawRect(sgc::Rectf{x, 0.0f, 1.0f, m_screenH}, kPaperEdge);
		}
		for (float y = kStep; y < m_screenH; y += kStep)
		{
			screen.drawRect(sgc::Rectf{0.0f, y, m_screenW, 1.0f}, kPaperEdge);
		}
	}

	void drawCursor(mitiru::Screen& screen)
	{
		// Body + amber outline so the cursor is unmistakable in the
		// PNG diff. `sgc::Rectf{x, y, w, h}` uses the 4-arg constructor;
		// member fields are `.position` / `.size`, not `.x/.y/.w/.h`.
		const float bx = m_cursorX - kCursorSize * 0.5f;
		const float by = m_cursorY - kCursorSize * 0.5f;
		const float bw = kCursorSize;
		const float bh = kCursorSize;
		screen.drawRect(sgc::Rectf{bx, by, bw, bh}, kCursorBody);
		// 2 px amber edge by drawing 4 strips.
		const float t = 2.0f;
		screen.drawRect(sgc::Rectf{bx, by, bw, t}, kAmberAccent);
		screen.drawRect(sgc::Rectf{bx, by + bh - t, bw, t}, kAmberAccent);
		screen.drawRect(sgc::Rectf{bx, by, t, bh}, kAmberAccent);
		screen.drawRect(sgc::Rectf{bx + bw - t, by, t, bh}, kAmberAccent);
	}

	void drawHud(mitiru::Screen& screen)
	{
		char buf[160];
		const char* modeLabel = (m_mode == Mode::Record) ? "RECORD" : "REPLAY";
		if (m_mode == Mode::Replay)
		{
			std::snprintf(buf, sizeof(buf),
				"mode: %s   frame: %d   seed: 0x%llX   total: %u",
				modeLabel, m_frame,
				static_cast<unsigned long long>(m_player.rngSeed()),
				m_player.totalFrames());
		}
		else
		{
			std::snprintf(buf, sizeof(buf),
				"mode: %s   frame: %d   seed: 0x%llX",
				modeLabel, m_frame,
				static_cast<unsigned long long>(kRecordSeed));
		}
		screen.drawTextInRect(
			sgc::Rectf{16.0f, 12.0f, m_screenW - 32.0f, 28.0f},
			buf, kInk, 24.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);

		char buf2[160];
		std::snprintf(buf2, sizeof(buf2),
			"cursor: (%d, %d)   keysDown[Left/Up/Right/Down]: %d %d %d %d",
			static_cast<int>(m_cursorX),
			static_cast<int>(m_cursorY),
			m_lastSnap.keysDown[0x25],
			m_lastSnap.keysDown[0x26],
			m_lastSnap.keysDown[0x27],
			m_lastSnap.keysDown[0x28]);
		screen.drawTextInRect(
			sgc::Rectf{16.0f, m_screenH - 28.0f, m_screenW - 32.0f, 20.0f},
			buf2, kMute, 16.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);
	}

	void takeScreenshot(mitiru::Engine& eng)
	{
		const auto pixels = eng.capture();
		if (pixels.empty()) { return; }

		const int w = static_cast<int>(m_screenW);
		const int h = static_cast<int>(m_screenH);
		const char* tag = (m_mode == Mode::Record) ? "replay_record" : "replay_play";
		char path[128];
		std::snprintf(path, sizeof(path), "evidence/%s.png", tag);
		(void)mitiru::render::savePixelsToPng(pixels.data(), w, h, path);
	}

	// Per-frame game-state blob written into the v3 record. Kept POD so the
	// recorder can memcpy it; the engine never interprets its contents.
	struct GameStateBlob { float cursorX; float cursorY; };

	Mode                          m_mode;
	std::string                   m_path;
	float                         m_cursorSpeed;
	mitiru::replay::Recorder      m_rec;
	mitiru::replay::Player        m_player;
	mitiru::module::InputSnapshot m_lastSnap{};
	int                           m_frame{0};
	bool                          m_wantScreenshot{false};
	bool                          m_screenshotted{false};

	bool   m_cursorInit{false};
	float  m_screenW{800.0f};
	float  m_screenH{500.0f};
	float  m_cursorX{0.0f};
	float  m_cursorY{0.0f};
};

// ── Diff mode game ───────────────────────────────────────────────────────
// Compares two replay files frame-by-frame and renders a timeline:
//   silver vertical bar  = InputSnapshot bytes match
//   Saturn red bar       = mismatch
// Top HUD: "N/M frames match". Screenshots at the same checkpoint frame.
class ReplayDiffGame final : public mitiru::Game
{
public:
	ReplayDiffGame(std::string pathA, std::string pathB)
		: m_pathA(std::move(pathA)), m_pathB(std::move(pathB)) {}

	bool computeDiff()
	{
		m_diffs = mitiru::replay::Player::diff(m_pathA, m_pathB);
		m_matchCount = 0;
		for (const auto& d : m_diffs)
		{
			if (d.inputMatches) { ++m_matchCount; }
		}
		return !m_diffs.empty();
	}

	[[nodiscard]] std::size_t total()   const noexcept { return m_diffs.size(); }
	[[nodiscard]] std::size_t matches() const noexcept { return m_matchCount; }

	void update(float /*dt*/) override
	{
		if (m_frame + 1 == kScreenshotFrame && !m_screenshotted)
		{
			m_wantScreenshot = true;
		}
		if (m_frame >= kScreenshotFrame + 2 && m_screenshotted)
		{
			if (auto* eng = engine()) { eng->requestStop(); }
		}
		++m_frame;
	}

	void draw(mitiru::Screen& screen) override
	{
		m_screenW = static_cast<float>(screen.width());
		m_screenH = static_cast<float>(screen.height());

		screen.clear(kPaperBg);
		drawSummary(screen);
		drawTimeline(screen);
	}

	void onFrameStartHook(mitiru::Engine& eng)
	{
		if (m_wantScreenshot && !m_screenshotted)
		{
			const auto pixels = eng.capture();
			if (!pixels.empty())
			{
				(void)mitiru::render::savePixelsToPng(
					pixels.data(),
					static_cast<int>(m_screenW),
					static_cast<int>(m_screenH),
					"evidence/replay_diff.png");
			}
			m_wantScreenshot = false;
			m_screenshotted  = true;
		}
	}

	[[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
	{
		return {outsideW, outsideH};
	}

private:
	void drawSummary(mitiru::Screen& screen)
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf),
			"DIFF   %llu/%llu frames match",
			static_cast<unsigned long long>(m_matchCount),
			static_cast<unsigned long long>(m_diffs.size()));
		const bool allMatch =
			!m_diffs.empty() && m_matchCount == m_diffs.size();
		screen.drawTextInRect(
			sgc::Rectf{16.0f, 12.0f, m_screenW - 32.0f, 30.0f},
			buf, allMatch ? kInk : kAmberAccent, 26.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);

		char sub[160];
		std::snprintf(sub, sizeof(sub), "A: %s    B: %s",
			m_pathA.c_str(), m_pathB.c_str());
		screen.drawTextInRect(
			sgc::Rectf{16.0f, m_screenH - 28.0f, m_screenW - 32.0f, 20.0f},
			sub, kMute, 14.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);
	}

	void drawTimeline(mitiru::Screen& screen)
	{
		if (m_diffs.empty()) { return; }

		// Timeline band fills the middle of the screen. Each frame is one
		// vertical column; columns get at least 1 px and span the band width.
		const float bandX = 24.0f;
		const float bandY = m_screenH * 0.30f;
		const float bandW = m_screenW - 48.0f;
		const float bandH = m_screenH * 0.40f;

		// Backing track (ink border) so the band is visible even all-silver.
		screen.drawRect(sgc::Rectf{bandX - 2.0f, bandY - 2.0f,
		                           bandW + 4.0f, bandH + 4.0f}, kPaperEdge);
		screen.drawRect(sgc::Rectf{bandX, bandY, bandW, bandH}, kPaperBg);

		const std::size_t n = m_diffs.size();
		const float colW = bandW / static_cast<float>(n);
		for (std::size_t i = 0; i < n; ++i)
		{
			const float x = bandX + colW * static_cast<float>(i);
			// Silver matches blend into bg; draw a faint ink tick. Mismatches
			// draw a full-height Saturn red column so they pop.
			if (m_diffs[i].inputMatches)
			{
				const float w = (colW > 2.0f) ? colW - 1.0f : colW;
				screen.drawRect(sgc::Rectf{x, bandY, w, bandH}, kMatchBar);
			}
			else
			{
				const float w = (colW < 2.0f) ? 2.0f : colW;
				screen.drawRect(sgc::Rectf{x, bandY, w, bandH}, kAmberAccent);
			}
		}
	}

	// Slightly darker than bg so "all match" silver track is still readable.
	static constexpr sgc::Colorf kMatchBar{0.85f, 0.85f, 0.85f, 1.0f};

	std::string                                      m_pathA;
	std::string                                      m_pathB;
	std::vector<mitiru::replay::Player::FrameDiff>   m_diffs;
	std::size_t m_matchCount{0};
	int    m_frame{0};
	bool   m_wantScreenshot{false};
	bool   m_screenshotted{false};
	float  m_screenW{800.0f};
	float  m_screenH{500.0f};
};

// ── State-divergence mode game ─────────────────────────────────────────────
// Compares the recorded STATE traces of two runs and renders a timeline:
//   silver bar     = state matches this frame
//   Saturn red bar = state diverged this frame
// A vertical ink line marks the first divergent frame. Top HUD summarises
// "DIVERGED at frame N / input MATCHES → code-caused" (or "IDENTICAL").
class ReplayDivergenceGame final : public mitiru::Game
{
public:
	ReplayDivergenceGame(std::string pathA, std::string pathB)
		: m_pathA(std::move(pathA)), m_pathB(std::move(pathB)) {}

	bool compute()
	{
		m_div = mitiru::replay::Player::diffState(m_pathA, m_pathB);
		// totalFrames==0 means a header was unreadable (compare impossible).
		return m_div.totalFrames > 0;
	}

	[[nodiscard]] const mitiru::replay::Player::StateDivergence& result() const
	{
		return m_div;
	}

	void update(float /*dt*/) override
	{
		if (m_frame + 1 == kScreenshotFrame && !m_screenshotted)
		{
			m_wantScreenshot = true;
		}
		if (m_frame >= kScreenshotFrame + 2 && m_screenshotted)
		{
			if (auto* eng = engine()) { eng->requestStop(); }
		}
		++m_frame;
	}

	void draw(mitiru::Screen& screen) override
	{
		m_screenW = static_cast<float>(screen.width());
		m_screenH = static_cast<float>(screen.height());

		screen.clear(kPaperBg);
		drawSummary(screen);
		drawTimeline(screen);
		drawPaths(screen);
	}

	void onFrameStartHook(mitiru::Engine& eng)
	{
		if (m_wantScreenshot && !m_screenshotted)
		{
			const auto pixels = eng.capture();
			if (!pixels.empty())
			{
				(void)mitiru::render::savePixelsToPng(
					pixels.data(),
					static_cast<int>(m_screenW),
					static_cast<int>(m_screenH),
					"evidence/state_divergence.png");
			}
			m_wantScreenshot = false;
			m_screenshotted  = true;
		}
	}

	[[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
	{
		return {outsideW, outsideH};
	}

private:
	void drawSummary(mitiru::Screen& screen)
	{
		char buf[192];
		if (m_div.diverged)
		{
			std::snprintf(buf, sizeof(buf),
				"DIVERGED at frame %u / %u   input %s%s",
				m_div.firstDivergentFrame, m_div.totalFrames,
				m_div.inputMatchesAll ? "MATCHES" : "DIFFERS",
				m_div.inputMatchesAll ? " -> code-caused" : " -> input-caused");
		}
		else
		{
			std::snprintf(buf, sizeof(buf),
				"IDENTICAL   %u/%u frames match   (deterministic)",
				m_div.totalFrames, m_div.totalFrames);
		}
		screen.drawTextInRect(
			sgc::Rectf{16.0f, 12.0f, m_screenW - 32.0f, 30.0f},
			buf, m_div.diverged ? kAmberAccent : kInk, 24.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);

		char sub[192];
		std::snprintf(sub, sizeof(sub), "A: %s    B: %s",
			m_pathA.c_str(), m_pathB.c_str());
		screen.drawTextInRect(
			sgc::Rectf{16.0f, m_screenH - 28.0f, m_screenW - 32.0f, 20.0f},
			sub, kMute, 14.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);
	}

	void drawTimeline(mitiru::Screen& screen)
	{
		const auto& fm = m_div.frameMatch;
		if (fm.empty()) { return; }

		const float bandX = 24.0f;
		const float bandY = m_screenH * 0.26f;
		const float bandW = m_screenW - 48.0f;
		const float bandH = m_screenH * 0.22f;

		screen.drawRect(sgc::Rectf{bandX - 2.0f, bandY - 2.0f,
		                           bandW + 4.0f, bandH + 4.0f}, kPaperEdge);
		screen.drawRect(sgc::Rectf{bandX, bandY, bandW, bandH}, kPaperBg);

		const std::size_t n = fm.size();
		const float colW = bandW / static_cast<float>(n);
		for (std::size_t i = 0; i < n; ++i)
		{
			const float x = bandX + colW * static_cast<float>(i);
			if (fm[i])
			{
				const float w = (colW > 2.0f) ? colW - 1.0f : colW;
				screen.drawRect(sgc::Rectf{x, bandY, w, bandH}, kMatchBar);
			}
			else
			{
				const float w = (colW < 2.0f) ? 2.0f : colW;
				screen.drawRect(sgc::Rectf{x, bandY, w, bandH}, kAmberAccent);
			}
		}

		// Vertical ink line at the first divergent frame.
		if (m_div.diverged && m_div.firstDivergentFrame < n)
		{
			const float lx =
				bandX + colW * static_cast<float>(m_div.firstDivergentFrame);
			screen.drawRect(sgc::Rectf{lx, bandY - 10.0f, 2.0f, bandH + 20.0f},
			                kInk);
			char tag[48];
			std::snprintf(tag, sizeof(tag), "first @ %u",
			              m_div.firstDivergentFrame);
			screen.drawTextInRect(
				sgc::Rectf{lx + 4.0f, bandY - 26.0f, 160.0f, 18.0f},
				tag, kInk, 14.0f,
				mitiru::Screen::TextAlignH::Left,
				mitiru::Screen::TextAlignV::Top);
		}
	}

	// Render the first-divergent-frame A/B state (cursor x,y) numerically so
	// the divergence is legible, not just a colour bar.
	void drawPaths(mitiru::Screen& screen)
	{
		if (!m_div.diverged) { return; }
		if (m_div.firstDivStateA.size() < sizeof(float) * 2 ||
		    m_div.firstDivStateB.size() < sizeof(float) * 2)
		{
			return;
		}
		float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
		std::memcpy(&ax, m_div.firstDivStateA.data(), sizeof(float));
		std::memcpy(&ay, m_div.firstDivStateA.data() + sizeof(float), sizeof(float));
		std::memcpy(&bx, m_div.firstDivStateB.data(), sizeof(float));
		std::memcpy(&by, m_div.firstDivStateB.data() + sizeof(float), sizeof(float));

		char buf[160];
		std::snprintf(buf, sizeof(buf),
			"state@first   A=(%.1f, %.1f)   B=(%.1f, %.1f)",
			ax, ay, bx, by);
		screen.drawTextInRect(
			sgc::Rectf{24.0f, m_screenH * 0.58f, m_screenW - 48.0f, 22.0f},
			buf, kInk, 18.0f,
			mitiru::Screen::TextAlignH::Left,
			mitiru::Screen::TextAlignV::Top);
	}

	static constexpr sgc::Colorf kMatchBar{0.85f, 0.85f, 0.85f, 1.0f};

	std::string                                  m_pathA;
	std::string                                  m_pathB;
	mitiru::replay::Player::StateDivergence      m_div;
	int    m_frame{0};
	bool   m_wantScreenshot{false};
	bool   m_screenshotted{false};
	float  m_screenW{800.0f};
	float  m_screenH{500.0f};
};

// Hex-encode up to `maxBytes` of a blob for the JSON detail file.
std::string hexExcerpt(const std::vector<std::uint8_t>& blob, std::size_t maxBytes)
{
	static const char* kHex = "0123456789abcdef";
	const std::size_t  n    = (blob.size() < maxBytes) ? blob.size() : maxBytes;
	std::string        out;
	out.reserve(n * 2);
	for (std::size_t i = 0; i < n; ++i)
	{
		out.push_back(kHex[(blob[i] >> 4) & 0xF]);
		out.push_back(kHex[blob[i] & 0xF]);
	}
	return out;
}

// Write the structured detail file (frame match column + first-divergent
// A/B state hex) so an AI can ingest the full picture, not just the summary.
void writeDivergenceJson(const mitiru::replay::Player::StateDivergence& d,
                         const std::string& pathA, const std::string& pathB)
{
	std::ofstream f("evidence/divergence_detail.json",
	                std::ios::binary | std::ios::trunc);
	if (!f.is_open()) { return; }

	const bool noDiv =
		d.firstDivergentFrame == mitiru::replay::Player::kNoDivergence;

	f << "{\n";
	f << "  \"diverged\": " << (d.diverged ? "true" : "false") << ",\n";
	f << "  \"firstFrame\": " << (noDiv ? -1 : static_cast<long long>(d.firstDivergentFrame)) << ",\n";
	f << "  \"total\": " << d.totalFrames << ",\n";
	f << "  \"inputMatch\": " << (d.inputMatchesAll ? "true" : "false") << ",\n";
	f << "  \"verdict\": \""
	  << (!d.diverged ? "identical"
	      : (d.inputMatchesAll ? "code-caused" : "input-caused")) << "\",\n";
	f << "  \"runA\": \"" << pathA << "\",\n";
	f << "  \"runB\": \"" << pathB << "\",\n";
	f << "  \"firstDivStateA\": \"" << hexExcerpt(d.firstDivStateA, 32) << "\",\n";
	f << "  \"firstDivStateB\": \"" << hexExcerpt(d.firstDivStateB, 32) << "\",\n";
	f << "  \"frameMatch\": [";
	for (std::size_t i = 0; i < d.frameMatch.size(); ++i)
	{
		if (i != 0) { f << ","; }
		f << static_cast<int>(d.frameMatch[i]);
	}
	f << "]\n";
	f << "}\n";
}

// ── Headless test mode ────────────────────────────────────────────────────
// Reads a v3 .mtrr file, re-runs the same deterministic applyInput logic
// without opening a window or touching CEF, and emits final-state JSON to
// stdout. If --expect <json> is supplied, compares key-by-key and exits
// non-zero on mismatch.
//
// JSON shape: {"cursorX":<int>,"cursorY":<int>,"seed":<uint64>,"frames":<uint64>}

// Minimal JSON parser for the four expected keys. Returns false on any parse
// error, sets output params. Tolerates extra whitespace and key order.
static bool parseExpectJson(const std::string& path,
                            int&      outX, int&      outY,
                            long long& outSeed, long long& outFrames)
{
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) { return false; }
	std::string txt((std::istreambuf_iterator<char>(f)),
	                 std::istreambuf_iterator<char>());

	// Extract a numeric value following "key": in the JSON string.
	auto extract = [&](const char* key, long long& out) -> bool
	{
		const std::string needle = std::string("\"") + key + "\"";
		const auto pos = txt.find(needle);
		if (pos == std::string::npos) { return false; }
		auto colon = txt.find(':', pos + needle.size());
		if (colon == std::string::npos) { return false; }
		char* end = nullptr;
		out = std::strtoll(txt.c_str() + colon + 1, &end, 10);
		return end != txt.c_str() + colon + 1;
	};

	long long x = 0, y = 0, seed = 0, frames = 0;
	if (!extract("cursorX", x)   || !extract("cursorY", y) ||
	    !extract("seed",    seed) || !extract("frames",  frames))
	{
		return false;
	}
	outX      = static_cast<int>(x);
	outY      = static_cast<int>(y);
	outSeed   = seed;
	outFrames = frames;
	return true;
}

// Headless replay-test: open file, iterate all frames, apply the same
// deterministic applyInput arithmetic, emit final-state JSON to stdout.
// Returns 0 on PASS, non-zero on FAIL (mismatch or I/O error).
static int runHeadlessTest(const std::string& replayPath,
                           const std::string& expectPath)
{
	mitiru::replay::Player player;
	if (!player.open(replayPath))
	{
		std::fprintf(stderr, "test: cannot open %s\n", replayPath.c_str());
		return 3;
	}

	// Reproduce the same initial cursor position used during record.
	// The recorded game starts at (screenW*0.25, screenH*0.25) = (200, 125)
	// for the default 800x500 window; we replicate that constant here.
	constexpr float kInitX = 800.0f * 0.25f;
	constexpr float kInitY = 500.0f * 0.25f;

	constexpr std::uint8_t kVkLeft  = 0x25;
	constexpr std::uint8_t kVkUp    = 0x26;
	constexpr std::uint8_t kVkRight = 0x27;
	constexpr std::uint8_t kVkDown  = 0x28;

	float cursorX = kInitX;
	float cursorY = kInitY;

	mitiru::module::InputSnapshot     snap{};
	std::vector<std::uint8_t>         state;
	std::uint32_t                     frameIdx = 0;

	while (player.readNextWithState(snap, state, frameIdx))
	{
		// Mirror ReplaySampleGame::applyInput exactly (kCursorSpeed = 2.0f for
		// horizontal; m_cursorSpeed = kCursorSpeed for vertical in normal record).
		if (snap.keysDown[kVkLeft])  { cursorX -= kCursorSpeed; }
		if (snap.keysDown[kVkRight]) { cursorX += kCursorSpeed; }
		if (snap.keysDown[kVkUp])    { cursorY -= kCursorSpeed; }
		if (snap.keysDown[kVkDown])  { cursorY += kCursorSpeed; }
	}

	const std::uint64_t framesRead = player.framesRead();
	const std::uint64_t seed       = player.rngSeed();

	// Emit final-state JSON to stdout.
	std::printf("{\"cursorX\":%d,\"cursorY\":%d,\"seed\":%llu,\"frames\":%llu}\n",
		static_cast<int>(cursorX), static_cast<int>(cursorY),
		static_cast<unsigned long long>(seed),
		static_cast<unsigned long long>(framesRead));
	std::fflush(stdout);

	if (expectPath.empty()) { return 0; }

	// Compare against expected JSON.
	int       expX = 0, expY = 0;
	long long expSeed = 0, expFrames = 0;
	if (!parseExpectJson(expectPath, expX, expY, expSeed, expFrames))
	{
		std::fprintf(stderr, "test: cannot parse expect file %s\n",
		             expectPath.c_str());
		return 4;
	}

	bool pass = true;
	const int    actX      = static_cast<int>(cursorX);
	const int    actY      = static_cast<int>(cursorY);
	const long long actSeed   = static_cast<long long>(seed);
	const long long actFrames = static_cast<long long>(framesRead);

	auto check = [&](const char* key, long long actual, long long expected)
	{
		if (actual != expected)
		{
			std::fprintf(stderr, "FAIL  %-10s  actual=%-12lld  expected=%lld\n",
			             key, actual, expected);
			pass = false;
		}
		else
		{
			std::fprintf(stderr, "PASS  %-10s  %lld\n", key, actual);
		}
	};

	check("cursorX", actX,      expX);
	check("cursorY", actY,      expY);
	check("seed",    actSeed,   expSeed);
	check("frames",  actFrames, expFrames);

	return pass ? 0 : 1;
}

void printUsage()
{
	std::fprintf(stderr,
		"usage: mitiru_subsys_replay --record <path> | --record-variant <path>"
		" | --replay <path> | --diff <fileA> <fileB>"
		" | --state-divergence <runA> <runB>"
		" | --test <path> [--expect <json>]\n");
}

}  // namespace

int main(int argc, char* argv[])
{
	if (argc < 2) { printUsage(); return 2; }

	// ── Headless test mode: --test <path> [--expect <json>] ────────────────
	if (std::strcmp(argv[1], "--test") == 0)
	{
		if (argc < 3) { printUsage(); return 2; }
		std::string replayPath = argv[2];
		std::string expectPath;
		for (int i = 3; i < argc - 1; ++i)
		{
			if (std::strcmp(argv[i], "--expect") == 0)
			{
				expectPath = argv[i + 1];
				break;
			}
		}
		return runHeadlessTest(replayPath, expectPath);
	}

	if (argc < 3) { printUsage(); return 2; }

	// ── Diff mode: --diff <fileA> <fileB> ──────────────────────────────────
	if (std::strcmp(argv[1], "--diff") == 0)
	{
		if (argc < 4) { printUsage(); return 2; }
		ReplayDiffGame diffGame(argv[2], argv[3]);
		if (!diffGame.computeDiff())
		{
			std::fprintf(stderr,
				"diff: could not read one of the files (bad header / v1?)\n");
			return 3;
		}

		mitiru::Engine        engine;
		mitiru::EngineConfig  cfg;
		cfg.title                = "mitiru_subsys_replay [diff]";
		cfg.windowWidth          = 800;
		cfg.windowHeight         = 500;
		cfg.vsync                = true;
		cfg.enableCef            = false;
		cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
		cfg.useLogicalWindowSize = true;
		cfg.backgroundColor      = kPaperBg;
		cfg.onFrameStart = [&diffGame](mitiru::Engine& e) {
			diffGame.onFrameStartHook(e);
		};

		engine.run(diffGame, cfg);

		std::fprintf(stderr, "[diff] %llu/%llu frames match  A=%s B=%s\n",
			static_cast<unsigned long long>(diffGame.matches()),
			static_cast<unsigned long long>(diffGame.total()),
			argv[2], argv[3]);
		return 0;
	}

	// ── State-divergence mode: --state-divergence <runA> <runB> ────────────
	if (std::strcmp(argv[1], "--state-divergence") == 0)
	{
		if (argc < 4) { printUsage(); return 2; }
		ReplayDivergenceGame divGame(argv[2], argv[3]);
		if (!divGame.compute())
		{
			std::fprintf(stderr,
				"state-divergence: could not read one of the files"
				" (bad header / pre-v3?)\n");
			return 3;
		}

		const auto& d = divGame.result();
		const bool  noDiv =
			d.firstDivergentFrame == mitiru::replay::Player::kNoDivergence;

		// Structured one-line JSON to stdout (AI-readable).
		std::printf(
			"{\"diverged\":%s,\"firstFrame\":%lld,\"total\":%u,"
			"\"inputMatch\":%s,\"verdict\":\"%s\"}\n",
			d.diverged ? "true" : "false",
			noDiv ? -1LL : static_cast<long long>(d.firstDivergentFrame),
			d.totalFrames,
			d.inputMatchesAll ? "true" : "false",
			!d.diverged ? "identical"
			            : (d.inputMatchesAll ? "code-caused" : "input-caused"));
		std::fflush(stdout);

		// Detailed JSON to file (frame match column + first-divergent hex).
		writeDivergenceJson(d, argv[2], argv[3]);

		// Visual timeline window.
		mitiru::Engine        engine;
		mitiru::EngineConfig  cfg;
		cfg.title                = "mitiru_subsys_replay [state-divergence]";
		cfg.windowWidth          = 800;
		cfg.windowHeight         = 500;
		cfg.vsync                = true;
		cfg.enableCef            = false;
		cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
		cfg.useLogicalWindowSize = true;
		cfg.backgroundColor      = kPaperBg;
		cfg.onFrameStart = [&divGame](mitiru::Engine& e) {
			divGame.onFrameStartHook(e);
		};

		engine.run(divGame, cfg);
		return 0;
	}

	ReplaySampleGame::Mode mode = ReplaySampleGame::Mode::Record;
	float cursorSpeed = kCursorSpeed;
	if      (std::strcmp(argv[1], "--record") == 0)
	{
		mode = ReplaySampleGame::Mode::Record;
	}
	else if (std::strcmp(argv[1], "--record-variant") == 0)
	{
		// Simulate "a code change": same deterministic input, different speed.
		mode        = ReplaySampleGame::Mode::Record;
		cursorSpeed = kCursorSpeed + 0.5f;
	}
	else if (std::strcmp(argv[1], "--replay") == 0)
	{
		mode = ReplaySampleGame::Mode::Replay;
	}
	else { printUsage(); return 2; }

	ReplaySampleGame game(mode, argv[2], cursorSpeed);
	if (!game.openSink())
	{
		std::fprintf(stderr, "failed to open %s\n", argv[2]);
		return 3;
	}

	mitiru::Engine        engine;
	mitiru::EngineConfig  cfg;
	cfg.title                = (mode == ReplaySampleGame::Mode::Record)
	                           ? "mitiru_subsys_replay [record]"
	                           : "mitiru_subsys_replay [replay]";
	cfg.windowWidth          = 800;
	cfg.windowHeight         = 500;
	cfg.vsync                = true;
	cfg.enableCef            = false;
	cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
	cfg.useLogicalWindowSize = true;
	cfg.backgroundColor      = kPaperBg;
	// Run our screenshot logic AFTER the previous frame has presented but
	// BEFORE the next update begins — that is exactly what onFrameStart
	// provides. Calling capture() from inside Game::draw() would read a
	// not-yet-presented back buffer on DX11 and return an empty/clear frame.
	cfg.onFrameStart = [&game](mitiru::Engine& e) { game.onFrameStartHook(e); };

	engine.run(game, cfg);

	const char* modeLabel = (mode == ReplaySampleGame::Mode::Record) ? "record" : "replay";
	const auto  total     = (mode == ReplaySampleGame::Mode::Record)
	                        ? game.framesRec() : game.framesPlayed();
	std::fprintf(stderr, "[%s] frames=%llu file=%s\n",
		modeLabel,
		static_cast<unsigned long long>(total),
		argv[2]);
	return 0;
}
