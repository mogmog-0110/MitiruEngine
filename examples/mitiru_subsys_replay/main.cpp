// mitiru_subsys_replay — 軸4 (determinism + replay) P4 成果物。
//
// 1 binary、5 モード:
//   --record <path>        : ダミーゲームを 300 frame 実行し、InputSnapshot +
//                            frame ごとの game-state blob (cursor x,y) と
//                            RNG seed を v3 ファイルへ記録。
//   --record-variant <path>: --record と同じだが cursor 速度を微妙に変える —
//                            「コード変更」を模す。input は同一 deterministic
//                            だが state trace は分岐。divergence の run B 用。
//   --replay <path>        : 記録した InputSnapshot stream を同じゲームへ流す。
//                            HUD は v3 header から読んだ seed + totalFrames を表示。
//   --diff <fileA> <fileB> : 2 つの replay ファイルを frame 単位で INPUT 比較し、
//                            timeline を描画 (銀=一致、赤=相違)。
//   --state-divergence <A> <B> : 2 run の記録 STATE trace を比較。game state が
//                            分岐した最初の frame と、input が一致したか
//                            (→ code 起因 vs input 起因) を報告 (構造化 JSON を
//                            stdout + detail ファイルへ)。timeline 窓を描画。
//
// ゲームの挙動:
//   - 「cursor」位置 (x,y) を保持。InputSnapshot で対応キーが "down" の frame、
//     cursor は左/右/上/下へ 2 px 移動。
//   - record モードでは合成した矢印キー入力で cursor を駆動
//     (Right 80 frame → Down 60 → Left 80 → Up 60)。
//   - replay モードではファイルから読んだ InputSnapshot で cursor を駆動。
//     同一キー列 → 同一軌道。
//   - frame 250 で engine が evidence/ へ PNG screenshot を撮影。
//   - frame 300 で engine 停止を要求。
//
// 意義 (軸4 / "Deterministic + 自動リプレイ"):
//   ADR 0005 により InputSnapshot は game logic への唯一の input 経路。
//   InputSnapshot stream を忠実に記録 + 再生すれば、gameplay 側の replay 足場
//   なしに record/replay 間で可視出力が bit-exact に一致することが保証される。

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

// ── Saturn (銀灰 + 落ち着いた赤 + ink) palette — 他の subsys demo と揃える ──
constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8 銀
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 ink 枠
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a
constexpr sgc::Colorf kAmberAccent {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c Saturn 赤
constexpr sgc::Colorf kCursorBody  {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 ink 本体

constexpr int kTotalFrames       = 300;
constexpr int kScreenshotFrame   = 250;
constexpr float kCursorSpeed     = 2.0f;
constexpr float kCursorSize      = 36.0f;

// record 時に v2 header へ格納する deterministic RNG seed。固定値なので
// 2 回の record run が byte 同一のファイルを生む (軸4 demo)。
constexpr std::uint64_t kRecordSeed = 0xC8002C2026ULL;

// frame index から deterministic な InputSnapshot を合成。cursor を L 字経路
// (右 → 下 → 左 → 上) で駆動する。
mitiru::module::InputSnapshot synthInput(int frameIdx)
{
	mitiru::module::InputSnapshot snap{};

	// VK code 定数 (Win32 — DLL InputSnapshot は VK index を使う)
	constexpr std::uint8_t kVkLeft  = 0x25;
	constexpr std::uint8_t kVkUp    = 0x26;
	constexpr std::uint8_t kVkRight = 0x27;
	constexpr std::uint8_t kVkDown  = 0x28;

	std::uint8_t held = 0;
	if      (frameIdx <  80) { held = kVkRight; }
	else if (frameIdx < 140) { held = kVkDown;  }
	else if (frameIdx < 220) { held = kVkLeft;  }
	else if (frameIdx < 280) { held = kVkUp;    }
	// frame 280..299: キー無し — cursor 停止。screenshot frame (250) が
	// "left" 区間内に収まり、開始位置との見分けがつくようにする。

	if (held != 0) { snap.keysDown[held] = 1; }
	return snap;
}

class ReplaySampleGame final : public mitiru::Game
{
public:
	enum class Mode : std::uint8_t { Record, Replay };

	// cursorSpeed の既定は kCursorSpeed。variant record は「コードが変わった」を
	// 模すため微妙に違う値を渡す。
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
		// 1. この frame の input snapshot を取得 — 合成 vs ファイル。
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
				// EOF / 破損 — snap を 0 のまま (cursor 停止)。
			}
		}
		m_lastSnap = snap;

		// 2. input を cursor へ適用 — 両モード共通の code path。
		applyInput(snap);

		// 3. record モードでは (input + update 後 state) を記録。state blob =
		//    cursor 位置 = 観測可能な全 game state。applyInput の後に記録すると
		//    trace がこの frame の logic の効果を反映 — divergence が比較する対象そのもの。
		if (m_mode == Mode::Record)
		{
			const GameStateBlob st{m_cursorX, m_cursorY};
			(void)m_rec.record(static_cast<std::uint32_t>(m_frame), snap,
			                   &st, sizeof(st));
		}

		// 4. deterministic な checkpoint frame で screenshot を撮影。
		//    フラグは 1 frame 前倒しで立てる — engine の onFrameStart は前 frame の
		//    present 完了後 (back buffer が populated でコピー可) に発火するため、
		//    frame N-1 で要求すると次 frame 開始時に frame N の内容を PNG 化する。
		if (m_frame + 1 == kScreenshotFrame && !m_screenshotted)
		{
			m_wantScreenshot = true;
		}

		// 5. 予定 session 終了で engine を停止。screenshot が実際に書かれるまで待つ。
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

	/// EngineConfig::onFrameStart から呼ばれる — その時点で前 frame の
	/// present が完了しており back buffer が読める。
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
		// synthInput と同じ VK code。replay path が snapshot byte だけで cursor を
		// 駆動できるよう、ここでも読み直す。
		constexpr std::uint8_t kVkLeft  = 0x25;
		constexpr std::uint8_t kVkUp    = 0x26;
		constexpr std::uint8_t kVkRight = 0x27;
		constexpr std::uint8_t kVkDown  = 0x28;

		// 水平移動は常に base speed、垂直移動は m_cursorSpeed を使う。variant run は
		// m_cursorSpeed だけ揺らすので、初期 "right" 区間 (frame 0..79) では state trace
		// が同一で、"down" 区間に入って初めて分岐する — 本物の code regression のような
		// 途中分岐 frame が得られる。
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
		// PNG diff で cursor が明瞭になるよう本体 + amber 縁取り。
		// `sgc::Rectf{x, y, w, h}` は 4 引数 constructor。member は
		// `.position` / `.size` であり `.x/.y/.w/.h` ではない。
		const float bx = m_cursorX - kCursorSize * 0.5f;
		const float by = m_cursorY - kCursorSize * 0.5f;
		const float bw = kCursorSize;
		const float bh = kCursorSize;
		screen.drawRect(sgc::Rectf{bx, by, bw, bh}, kCursorBody);
		// 4 本の帯で 2 px の amber 縁。
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

	// v3 record へ書く frame ごとの game-state blob。recorder が memcpy できるよう
	// POD に保つ。engine が中身を解釈することはない。
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

// ── Diff モードゲーム ──────────────────────────────────────────────────────
// 2 つの replay ファイルを frame 単位で比較し timeline を描画:
//   銀の縦バー     = InputSnapshot byte 一致
//   Saturn 赤バー  = 不一致
// 上部 HUD: "N/M frames match"。同じ checkpoint frame で screenshot。
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

		// timeline band は画面中央を占める。各 frame は 1 本の縦列で、
		// 列は最低 1 px、band 幅いっぱいに並ぶ。
		const float bandX = 24.0f;
		const float bandY = m_screenH * 0.30f;
		const float bandW = m_screenW - 48.0f;
		const float bandH = m_screenH * 0.40f;

		// 全銀でも band が見えるよう backing track (ink 枠)。
		screen.drawRect(sgc::Rectf{bandX - 2.0f, bandY - 2.0f,
		                           bandW + 4.0f, bandH + 4.0f}, kPaperEdge);
		screen.drawRect(sgc::Rectf{bandX, bandY, bandW, bandH}, kPaperBg);

		const std::size_t n = m_diffs.size();
		const float colW = bandW / static_cast<float>(n);
		for (std::size_t i = 0; i < n; ++i)
		{
			const float x = bandX + colW * static_cast<float>(i);
			// 一致 (銀) は bg に溶けるので薄い ink tick を描く。不一致は
			// 目立つよう全高の Saturn 赤列を描く。
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

	// "all match" の銀 track でも見えるよう bg より僅かに暗く。
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

// ── State-divergence モードゲーム ──────────────────────────────────────────
// 2 run の記録 STATE trace を比較し timeline を描画:
//   銀バー     = この frame は state 一致
//   Saturn 赤バー = この frame は state 分岐
// 縦の ink 線が最初の分岐 frame を示す。上部 HUD は
// "DIVERGED at frame N / input MATCHES → code-caused" (または "IDENTICAL") を要約。
class ReplayDivergenceGame final : public mitiru::Game
{
public:
	ReplayDivergenceGame(std::string pathA, std::string pathB)
		: m_pathA(std::move(pathA)), m_pathB(std::move(pathB)) {}

	bool compute()
	{
		m_div = mitiru::replay::Player::diffState(m_pathA, m_pathB);
		// totalFrames==0 は header が読めなかった (比較不能) を意味する。
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

		// 最初の分岐 frame に縦の ink 線。
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

	// 分岐が色バーだけでなく読めるよう、最初の分岐 frame の A/B state
	// (cursor x,y) を数値で描画。
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

// JSON detail ファイル用に blob を最大 `maxBytes` まで hex encode。
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

// AI が summary だけでなく全体像を取り込めるよう、構造化 detail ファイル
// (frame match 列 + 最初の分岐 A/B state の hex) を書く。
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

// ── Headless test モード ───────────────────────────────────────────────────
// v3 .mtrr ファイルを読み、窓を開かず CEF にも触れずに同じ deterministic な
// applyInput logic を再実行し、最終 state JSON を stdout へ出す。--expect <json>
// が与えられれば key 単位で比較し、不一致なら非 0 で終了。
//
// JSON 形式: {"cursorX":<int>,"cursorY":<int>,"seed":<uint64>,"frames":<uint64>}

// 期待 4 key 用の最小 JSON parser。parse error なら false を返し出力 param を
// 設定。余分な whitespace と key 順序を許容。
static bool parseExpectJson(const std::string& path,
                            int&      outX, int&      outY,
                            long long& outSeed, long long& outFrames)
{
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) { return false; }
	std::string txt((std::istreambuf_iterator<char>(f)),
	                 std::istreambuf_iterator<char>());

	// JSON 文字列中の "key": に続く数値を抽出。
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

// Headless replay-test: ファイルを開き全 frame を反復し、同じ deterministic な
// applyInput 演算を適用、最終 state JSON を stdout へ出す。
// PASS で 0、FAIL (不一致 or I/O error) で非 0 を返す。
static int runHeadlessTest(const std::string& replayPath,
                           const std::string& expectPath)
{
	mitiru::replay::Player player;
	if (!player.open(replayPath))
	{
		std::fprintf(stderr, "test: cannot open %s\n", replayPath.c_str());
		return 3;
	}

	// record 時と同じ初期 cursor 位置を再現。記録ゲームは既定 800x500 窓で
	// (screenW*0.25, screenH*0.25) = (200, 125) から開始するので、その定数をここで複製。
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
		// ReplaySampleGame::applyInput を厳密に複製 (水平は kCursorSpeed = 2.0f、
		// 通常 record では垂直も m_cursorSpeed = kCursorSpeed)。
		if (snap.keysDown[kVkLeft])  { cursorX -= kCursorSpeed; }
		if (snap.keysDown[kVkRight]) { cursorX += kCursorSpeed; }
		if (snap.keysDown[kVkUp])    { cursorY -= kCursorSpeed; }
		if (snap.keysDown[kVkDown])  { cursorY += kCursorSpeed; }
	}

	const std::uint64_t framesRead = player.framesRead();
	const std::uint64_t seed       = player.rngSeed();

	// 最終 state JSON を stdout へ出す。
	std::printf("{\"cursorX\":%d,\"cursorY\":%d,\"seed\":%llu,\"frames\":%llu}\n",
		static_cast<int>(cursorX), static_cast<int>(cursorY),
		static_cast<unsigned long long>(seed),
		static_cast<unsigned long long>(framesRead));
	std::fflush(stdout);

	if (expectPath.empty()) { return 0; }

	// 期待 JSON と比較。
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

	// ── Headless test モード: --test <path> [--expect <json>] ──────────────
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

	// ── Diff モード: --diff <fileA> <fileB> ────────────────────────────────
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

	// ── State-divergence モード: --state-divergence <runA> <runB> ──────────
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

		// 構造化 1 行 JSON を stdout へ (AI 可読)。
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

		// 詳細 JSON をファイルへ (frame match 列 + 最初の分岐の hex)。
		writeDivergenceJson(d, argv[2], argv[3]);

		// 視覚 timeline 窓。
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
		// 「コード変更」を模す: input は同一 deterministic、speed だけ違う。
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
	// screenshot logic は前 frame の present 後・次 update 開始前に走らせる —
	// これがまさに onFrameStart の提供するタイミング。Game::draw() 内で capture()
	// を呼ぶと DX11 では未 present の back buffer を読み、空/clear frame を返す。
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
