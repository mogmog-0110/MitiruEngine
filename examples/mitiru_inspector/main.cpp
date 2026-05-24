// mitiru_inspector — axis 5 (modular sub-window architecture) seed.
//
// A standalone, separate-OS-process window that watches another running
// MitiruEngine game's state and renders it live. No CEF, no audio, no
// ECS — just the renderer + file-mtime polling. Drag it to a second monitor
// and you get a real sub-window inspector while gameplay continues
// uninterrupted in the main window.
//
// The producer (gameplay) writes to
//   %TEMP%\mitiru_inspector_<pid>.json
// (see include/mitiru/observe/SharedSnapshot.hpp). The inspector reads
// from there at ~30 Hz, rendering whatever it finds.
//
// Usage:
//   mitiru_inspector <pid>          # watch process with that pid
//   mitiru_inspector --file <path>  # watch a specific file directly
//
// Why this design (instead of CEF multi-process):
//   - CEF runs in single-process mode in the engine today (V8 proxy
//     resolver blocker; see docs/adr/0004-modular-sub-window-architecture.md)
//   - Native sub-window via separate process trivially gives us
//     multi-monitor support, isolated crashes, and no IPC ceremony beyond
//     the file-mtime polling already used for hot reload

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

#include <mitiru/Mitiru.hpp>
#include <mitiru/observe/EventLog.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>

namespace {

// Pretty-print a json value as a single line; trims very long arrays/objects
// and rounds floats to 2 decimal places so the inspector doesn't show
// junk like 453.33353764648438 that just clutters the UI.
std::string compactValue(const nlohmann::json& v)
{
    if (v.is_number_float())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v.get<double>());
        return buf;
    }
    if (v.is_string())
    {
        return v.get<std::string>();
    }
    std::string s = v.dump();
    if (s.size() > 96) { s = s.substr(0, 93) + "..."; }
    return s;
}

// Which subset of the snapshot the inspector should render. Chosen via the
// `--panel <name>` CLI arg so the same exe can act as a focused viewer
// (e.g. input only) without bloating the screen with unrelated data.
//
// --panel state|input|log refers to FIXED built-in layouts.
// --inspectable <name> refers to a producer-registered Inspectable (from
//   mitiru::debug::registerInspectable / LocalInspectable). When set, the
//   inspector reads snapshot["<name>"]["state"] and renders it as a generic
//   key/value tree, with the title coming from snapshot["<name>"]["title"].
enum class Panel
{
    State,         ///< gameplay + input + debugLog (back-compat with old schema)
    Input,         ///< only the input section, full window
    Log,           ///< only the debug log, full window
    Inspectable,   ///< render snapshot[m_inspectable]["state"] only
    TimeTravel,    ///< P2 differentiator: scrub bar + HP graph (auto-selected
                   ///  when --inspectable timetravel)
    Events,        ///< Event timeline — reads %TEMP%\mitiru_events_<pid>.jsonl
                   ///  (append-only JSONL, dual-readable by AI). Needs a pid.
};

inline Panel parsePanel(const std::string& s)
{
    if (s == "input")      { return Panel::Input; }
    if (s == "log")        { return Panel::Log; }
    if (s == "timetravel" || s == "tt") { return Panel::TimeTravel; }
    if (s == "events" || s == "timeline") { return Panel::Events; }
    return Panel::State;
}

inline const char* panelTitleSuffix(Panel p)
{
    switch (p)
    {
        case Panel::Input:       return " · input";
        case Panel::Log:         return " · log";
        case Panel::Inspectable: return "";  // overridden with inspectable's own title
        case Panel::TimeTravel:  return " · time travel";
        case Panel::Events:      return " · events";
        default:                 return "";
    }
}

class Inspector final : public mitiru::Game
{
public:
    Inspector(std::optional<int> producerPid,
              std::optional<std::string> filePath,
              Panel panel,
              std::string inspectable = {})
        : m_producerPid(producerPid),
          m_panel(panel),
          m_inspectable(std::move(inspectable))
    {
        if (filePath)
        {
            m_overridePath = std::filesystem::path(*filePath);
        }
    }

    void update(float dt) override
    {
        m_elapsed += dt;
        m_pollAccum += dt;

        if (hasInput() && input().isKeyJustPressed(mitiru::KeyCode::Escape))
        {
            if (auto* eng = engine()) { eng->requestStop(); }
            return;
        }

        // Local view-only scrub: a left click inside the last-drawn HP graph
        // rect maps the click x → a LOCAL array offset into the history the
        // inspector already received. Nothing is sent back to the game — the
        // game window is pure gameplay and never freezes. Scrubbing happens
        // entirely inside this observation window. Works regardless of source
        // (pid or file) since no producer cooperation is needed.
        if (hasInput() &&
            input().isMouseButtonJustPressed(mitiru::MouseButton::Left))
        {
            handleGraphClick();
        }

        if (m_pollAccum < 1.0f / 30.0f) { return; }
        m_pollAccum = 0.0f;
        if (m_panel == Panel::Events) { pollEvents(); }
        else { poll(); }
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());

        // Mitiru Saturn — silver gray base (#c8c8c8).
        screen.clear(sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f});

        drawHeader(screen);
        drawJsonBody(screen);
        drawFooter(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void poll()
    {
        if (m_overridePath)
        {
            try
            {
                if (!std::filesystem::exists(*m_overridePath))
                {
                    m_state = std::nullopt;
                    return;
                }
                auto mt = std::filesystem::last_write_time(*m_overridePath);
                if (m_haveMtime && mt == m_lastMtime) { return; }
                std::ifstream in(*m_overridePath, std::ios::binary);
                if (!in) { return; }
                m_state = nlohmann::json::parse(in);
                m_lastMtime = mt;
                m_haveMtime = true;
                ++m_updateCount;
            }
            catch (...) { /* truncated mid-read; try again */ }
            return;
        }

        if (!m_reader && m_producerPid)
        {
            m_reader.emplace(*m_producerPid);
        }
        if (!m_reader) { return; }

        if (auto j = m_reader->tryRead())
        {
            m_state = std::move(*j);
            ++m_updateCount;
        }
    }

    /// Poll the append-only JSONL event timeline. Source is either a pid
    /// (→ %TEMP%\mitiru_events_<pid>.jsonl) or an explicit --file path. The
    /// reader keeps the most recent N events; a fresh read bumps the counter.
    void pollEvents()
    {
        if (!m_eventReader)
        {
            if (m_overridePath)
            {
                m_eventReader.emplace(*m_overridePath);
            }
            else if (m_producerPid)
            {
                m_eventReader.emplace(*m_producerPid);
            }
            else { return; }
        }
        if (m_eventReader->poll(m_eventKeep)) { ++m_updateCount; }
    }

    void drawHeader(mitiru::Screen& screen)
    {
        // Heights are sized to comfortably fit the chosen font (16px header
        // text + padding). SDF atlas is built at 32px so 16 = clean 0.5x
        // downscale → crisp glyphs even at this size.
        // Saturn: silver bg + 1px hairline divider (matches the
        // launcher / hello_game / companion surface treatment).
        const float h = 36.0f;
        // 1px bottom hairline separator (no inverted strip).
        screen.drawRect(
            sgc::Rectf{0.0f, h - 1.0f, m_screenW, 1.0f},
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});  // #101010 ink border

        std::string title = "MitiruEngine — inspector";
        if (!m_currentTitle.empty())
        {
            title += "  ·  " + m_currentTitle;
        }
        else if (!m_inspectable.empty())
        {
            title += "  ·  " + m_inspectable;
        }
        title += "  ·  " + sourceLabel();

        screen.drawTextInRect(
            sgc::Rectf{12.0f, 8.0f, m_screenW - 24.0f, h - 12.0f},
            title.c_str(),
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},  // ink on silver
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawJsonBody(mitiru::Screen& screen)
    {
        // All font sizes below are snapped to clean fractions of the SDF
        // atlas size (32px): 24 = 0.75x, 16 = 0.5x. Off-fraction sizes
        // (18, 19, 20) cause thin glyphs (l, i, .) to thin out below the
        // SDF threshold and appear as gaps.
        // Header bar is 36px; start the body with clearance so the first
        // section title doesn't crowd the "MitiruEngine — inspector · pid" line.
        const float top = 48.0f;

        // Event timeline: independent of the SharedSnapshot m_state — reads the
        // append-only JSONL via EventLog::Reader. Render even with no m_state.
        if (m_panel == Panel::Events)
        {
            drawEventTimeline(screen, 14.0f, top, m_screenW - 28.0f);
            return;
        }

        if (!m_state)
        {
            const std::string msg = m_overridePath || m_producerPid
                ? "waiting for producer..."
                : "no source specified — pass <pid> or --file <path>";
            screen.drawTextInRect(
                sgc::Rectf{12.0f, top, m_screenW - 24.0f, 28.0f},
                msg.c_str(),
                sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            return;
        }

        const auto& obj = *m_state;
        if (!obj.is_object())
        {
            screen.drawTextInRect(
                sgc::Rectf{12.0f, top, m_screenW - 24.0f, 22.0f},
                compactValue(obj).c_str(),
                sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            return;
        }

        // TimeTravel: rich scrub bar + HP graph. DLL exports under
        // inspectable name "timetravel" with raw history only:
        // { capacity, hpMax, hpHistory[], xHistory[] }. Scrub state is local.
        if (m_panel == Panel::TimeTravel)
        {
            const std::string key = m_inspectable.empty() ? "timetravel" : m_inspectable;
            if (obj.contains(key))
            {
                const auto& entry = obj[key];
                if (entry.is_object() && entry.contains("title"))
                {
                    m_currentTitle = entry["title"].get<std::string>();
                }
                if (entry.is_object() && entry.contains("state"))
                {
                    drawTimeTravelBody(screen, entry["state"],
                        14.0f, top, m_screenW - 28.0f);
                    return;
                }
            }
            screen.drawTextInRect(
                sgc::Rectf{12.0f, top, m_screenW - 24.0f, 28.0f},
                ("waiting for time-travel data ('" + key + "')...").c_str(),
                sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            return;
        }

        // --inspectable <name> overrides everything: render only the named
        // Inspectable's state. Producer's wire format is
        //   { "<name>": {"title": "...", "state": {...}} , ... }
        if (m_panel == Panel::Inspectable && !m_inspectable.empty())
        {
            if (obj.contains(m_inspectable))
            {
                const auto& entry = obj[m_inspectable];
                if (entry.is_object() && entry.contains("title"))
                {
                    m_currentTitle = entry["title"].get<std::string>();
                }
                if (entry.is_object() && entry.contains("state"))
                {
                    drawInspectableState(screen, entry["state"],
                        12.0f, top, m_screenW - 24.0f);
                    return;
                }
            }
            // Inspectable not present in this snapshot — show a waiting note.
            screen.drawTextInRect(
                sgc::Rectf{12.0f, top, m_screenW - 24.0f, 28.0f},
                ("waiting for inspectable '" + m_inspectable + "'...").c_str(),
                sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            return;
        }

        // ADR 0005 snapshot schema:
        //   { "<name>": {"title": "...", "state": {...}}, ... }
        // Each top-level key is an Inspectable; we expand its `state` so
        // the user sees actual data (hp, x, y, ...) rather than the wrapper.
        const float pad        = 14.0f;
        const float bodyWidth  = m_screenW - pad * 2.0f;
        const float bottomGuard = 36.0f;  // footer height
        float y                = top;
        bool  drewAnything     = false;
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (y + 28.0f > m_screenH - bottomGuard) { break; }
            if (!it.value().is_object()) { continue; }

            std::string title = it.value().value("title", it.key());
            const bool hasStateKey = it.value().contains("state");

            // Section header — 24px (0.75x atlas) for emphasis
            screen.drawTextInRect(
                sgc::Rectf{pad, y, bodyWidth, 28.0f},
                title.c_str(),
                sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f},  // #c8002c Saturn red
                24.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            y += 32.0f;

            // Section body — prefer the `state` subtree (wrapper convention);
            // fall back to the object itself for any non-wrapped entries.
            const nlohmann::json& body = hasStateKey ? it.value()["state"] : it.value();

            if (it.key() == "input" && body.is_object())
            {
                // Specialised compact renderer (held keys / mouse / counts).
                // Bound to a 220px band so a long press-count list doesn't
                // overflow into the next section (was causing header overlap).
                const float band  = std::min(m_screenH - y - bottomGuard, 220.0f);
                const float endY  = drawInputSection(
                    screen, body, pad, y, bodyWidth, y + band);
                y = endY + 12.0f;
            }
            else if (it.key() == "log" && body.is_array())
            {
                const float h = std::min(m_screenH - y - bottomGuard, 200.0f);
                drawDebugLog(screen, body, pad, y, bodyWidth, h);
                y += h + 12.0f;
            }
            else if (body.is_object())
            {
                const float rowH    = 22.0f;
                const float keyColW = std::min(bodyWidth * 0.42f, 220.0f);
                for (auto sIt = body.begin(); sIt != body.end(); ++sIt)
                {
                    if (y + rowH > m_screenH - bottomGuard) { break; }
                    screen.drawTextInRect(
                        sgc::Rectf{pad, y, keyColW, rowH},
                        sIt.key().c_str(),
                        sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                        16.0f,
                        mitiru::Screen::TextAlignH::Left,
                        mitiru::Screen::TextAlignV::Top);
                    screen.drawTextInRect(
                        sgc::Rectf{pad + keyColW + 8.0f, y, bodyWidth - keyColW - 8.0f, rowH},
                        compactValue(sIt.value()).c_str(),
                        sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},
                        16.0f,
                        mitiru::Screen::TextAlignH::Left,
                        mitiru::Screen::TextAlignV::Top);
                    y += rowH + 2.0f;
                }
                y += 12.0f;  // gap between sections
            }
            else
            {
                // primitive — single big line
                screen.drawTextInRect(
                    sgc::Rectf{pad, y, bodyWidth, 24.0f},
                    compactValue(body).c_str(),
                    sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},
                    16.0f,
                    mitiru::Screen::TextAlignH::Left,
                    mitiru::Screen::TextAlignV::Top);
                y += 28.0f;
            }
            drewAnything = true;
        }

        if (!drewAnything)
        {
            // Fallback for legacy or unrecognised schemas — dump as compact kv.
            drawKvSection(screen, "", obj, pad, top, bodyWidth);
        }
    }

    /// Local view-only scrub: map a left-click inside the HP graph rect to a
    /// LOCAL array-index offset into the history this inspector already holds.
    /// Nothing is sent to the game — the game never freezes. The graph is laid
    /// out oldest-left .. newest-right over the array (len = m_hpGraphCapacity),
    /// and m_ttOffset counts back from newest (0 = newest = right edge), so
    ///   offset = (len-1) - round((clickX - graphX) / graphW * (len-1))
    /// A click on the right edge (or offset 0) returns to live mode.
    void handleGraphClick()
    {
        if (m_panel != Panel::TimeTravel) { return; }
        if (!m_hpGraphValid || m_hpGraphCapacity < 1) { return; }

        const auto [mx, my] = input().mousePosition();
        if (mx < m_hpGraphX || mx > m_hpGraphX + m_hpGraphW) { return; }
        if (my < m_hpGraphY || my > m_hpGraphY + m_hpGraphH) { return; }

        const int len = m_hpGraphCapacity;  // array length, not frame capacity
        float frac = m_hpGraphW > 0.0f ? (mx - m_hpGraphX) / m_hpGraphW : 0.0f;
        frac = std::clamp(frac, 0.0f, 1.0f);
        const int idxFromOldest =
            static_cast<int>(std::lround(frac * static_cast<float>(len - 1)));
        int offset = (len - 1) - idxFromOldest;  // right edge → 0 (newest)
        offset = std::clamp(offset, 0, len - 1);

        m_ttOffset = offset;
        // Right edge (or computed newest) snaps back to live; otherwise scrub.
        m_ttScrubbing = !(frac > 0.97f || offset == 0);
    }

    /// Time-travel scrub panel — LOCAL view-only scrub. The producer exports
    /// only raw history; the inspector computes scrub state itself and never
    /// sends anything back to the game (no freeze, no reverse channel).
    /// Producer JSON schema (new contract):
    ///   { capacity, hpMax, hpHistory[]  (oldest→newest, downsampled),
    ///     xHistory[] (oldest→newest, downsampled) }
    /// Local scrub state lives in m_ttScrubbing / m_ttOffset (array-index units;
    /// offset 0 = newest = right edge).
    void drawTimeTravelBody(mitiru::Screen& screen,
                            const nlohmann::json& s,
                            float x, float y, float w)
    {
        // capacity = total frames recorded (informational only).
        const int   capacity = s.value("capacity", 0);
        const int   hpMax    = s.value("hpMax", 100);

        const sgc::Colorf muteCol{0.290f, 0.290f, 0.290f, 1.0f};       // #4a4a4a mid gray
        const sgc::Colorf accentCol{0.784f, 0.0f, 0.173f, 1.0f};       // #c8002c Saturn red
        const sgc::Colorf dangerCol{0.784f, 0.0f, 0.173f, 1.0f};       // #c8002c Saturn red (single accent)
        const sgc::Colorf xAxisCol{0.063f, 0.063f, 0.063f, 1.0f};      // #101010 ink — secondary series
        const sgc::Colorf borderCol{0.063f, 0.063f, 0.063f, 1.0f};     // #101010 ink border

        const auto& hist  = s.value("hpHistory", nlohmann::json::array());
        const auto& xHist = s.value("xHistory",  nlohmann::json::array());
        const int   n     = static_cast<int>(hist.size());
        const int   nx    = static_cast<int>(xHist.size());

        // Clamp local scrub offset to the current array length (history grows
        // as the game runs, and may shrink/reset). offset 0 = newest.
        if (n > 0 && m_ttOffset > n - 1) { m_ttOffset = std::max(0, n - 1); }
        if (n <= 0) { m_ttScrubbing = false; }

        // Locally compute scrubbed values from the arrays we already hold.
        // newest = last element; scrubbing reads back by m_ttOffset.
        int   hpAtScrub = 0;
        float xAtScrub  = 0.0f;
        if (n > 0)
        {
            const int idx = m_ttScrubbing ? (n - 1 - m_ttOffset) : (n - 1);
            try { hpAtScrub = hist[std::clamp(idx, 0, n - 1)].get<int>(); } catch (...) {}
        }
        if (nx > 0)
        {
            const int idx = m_ttScrubbing ? (nx - 1 - m_ttOffset) : (nx - 1);
            try { xAtScrub = xHist[std::clamp(idx, 0, nx - 1)].get<float>(); } catch (...) {}
        }

        // ── Status header ─────────────────────────────────────────
        char hdr[160];
        if (m_ttScrubbing)
        {
            std::snprintf(hdr, sizeof(hdr),
                "SCRUBBING   sample -%d/%d   HP: %d/%d   X: %.1f",
                m_ttOffset, std::max(0, n - 1), hpAtScrub, hpMax, xAtScrub);
        }
        else
        {
            std::snprintf(hdr, sizeof(hdr),
                "(live — click graph to scrub · click right edge = live)");
        }
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 24.0f}, hdr,
            m_ttScrubbing ? accentCol : muteCol,
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        y += 30.0f;

        // Split available vertical space into two stacked graphs with a label
        // strip + gap between them. Reserve room for the hint footer below.
        const float hintReserve = 28.0f;
        const float labelStrip  = 22.0f;
        const float gap         = 8.0f;
        const float totalAvail  = std::max(80.0f, m_screenH - y - hintReserve - 14.0f);
        const float graphH      = std::max(40.0f, (totalAvail - labelStrip - gap) * 0.5f);

        // ── HP graph (top half, bars) ─────────────────────────────
        screen.drawTextInRect(
            sgc::Rectf{x, y - 22.0f, w, 18.0f}, "HP",
            muteCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        // Record the HP graph rect for click-to-scrub hit testing (update()
        // runs separately from draw(), so we stash the last-drawn geometry).
        // m_hpGraphCapacity now holds the ARRAY length (local scrub maps clicks
        // to array indices, not real frame units).
        m_hpGraphX        = x;
        m_hpGraphY        = y;
        m_hpGraphW        = w;
        m_hpGraphH        = graphH;
        m_hpGraphCapacity = n;
        m_hpGraphValid    = (n > 0);

        if (n > 0 && hpMax > 0)
        {
            // Box border
            screen.drawRect(sgc::Rectf{x, y, w, 1.0f}, borderCol);
            screen.drawRect(sgc::Rectf{x, y + graphH, w, 1.0f}, borderCol);
            screen.drawRect(sgc::Rectf{x, y, 1.0f, graphH}, borderCol);
            screen.drawRect(sgc::Rectf{x + w - 1.0f, y, 1.0f, graphH}, borderCol);

            const float barW = w / static_cast<float>(n);
            for (int i = 0; i < n; ++i)
            {
                int hp = 0;
                try { hp = hist[i].get<int>(); } catch (...) {}
                const float pct = std::clamp(hp / static_cast<float>(hpMax), 0.0f, 1.0f);
                const float h   = (graphH - 2.0f) * pct;
                const float bx  = x + barW * static_cast<float>(i);
                const float by  = y + graphH - 1.0f - h;
                const auto col  = pct <= 0.35f ? dangerCol : accentCol;
                screen.drawRect(sgc::Rectf{bx, by, std::max(1.0f, barW - 0.5f), h},
                                sgc::Colorf{col.r, col.g, col.b, 0.65f});
            }

            // Local cursor — array-index based. cursorIdx counts from oldest
            // (left), centred in its bar. Only shown while scrubbing.
            if (m_ttScrubbing)
            {
                const int   cursorIdx = std::clamp(n - 1 - m_ttOffset, 0, n - 1);
                const float cx = x + barW * (static_cast<float>(cursorIdx) + 0.5f);
                screen.drawRect(sgc::Rectf{cx - 1.0f, y, 2.0f, graphH}, accentCol);
            }
        }
        else
        {
            screen.drawTextInRect(
                sgc::Rectf{x, y, w, 24.0f},
                "(no history yet — play for a few seconds)",
                muteCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
        }
        y += graphH + gap;

        // ── PLAYER X label strip ──────────────────────────────────
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, labelStrip}, "PLAYER X",
            xAxisCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        y += labelStrip;

        // ── player_x graph (bottom half, bars; auto-fit to xMin..xMax) ──
        if (nx > 0)
        {
            // Box border
            screen.drawRect(sgc::Rectf{x, y, w, 1.0f}, borderCol);
            screen.drawRect(sgc::Rectf{x, y + graphH, w, 1.0f}, borderCol);
            screen.drawRect(sgc::Rectf{x, y, 1.0f, graphH}, borderCol);
            screen.drawRect(sgc::Rectf{x + w - 1.0f, y, 1.0f, graphH}, borderCol);

            // Auto-fit: scan range, then normalize each sample into [0,1].
            float xMin = std::numeric_limits<float>::infinity();
            float xMax = -std::numeric_limits<float>::infinity();
            for (int i = 0; i < nx; ++i)
            {
                float v = 0.0f;
                try { v = xHist[i].get<float>(); } catch (...) {}
                if (v < xMin) { xMin = v; }
                if (v > xMax) { xMax = v; }
            }
            const float xRange  = xMax - xMin;
            const bool  flatish = xRange < 1.0f;  // player stationary → no spread

            const float barW = w / static_cast<float>(nx);
            for (int i = 0; i < nx; ++i)
            {
                float v = 0.0f;
                try { v = xHist[i].get<float>(); } catch (...) {}
                // Flat data → render at 50% center so the user still sees
                // "here's the data, it's just unchanged" instead of an empty box.
                const float pct = flatish ? 0.5f
                                          : std::clamp((v - xMin) / xRange, 0.0f, 1.0f);
                const float h   = (graphH - 2.0f) * pct;
                const float bx  = x + barW * static_cast<float>(i);
                const float by  = y + graphH - 1.0f - h;
                screen.drawRect(sgc::Rectf{bx, by, std::max(1.0f, barW - 0.5f), h},
                                sgc::Colorf{xAxisCol.r, xAxisCol.g, xAxisCol.b, 0.55f});
            }

            // Local cursor — array-index based, mirrors the HP graph.
            if (m_ttScrubbing)
            {
                const int   cursorIdx = std::clamp(nx - 1 - m_ttOffset, 0, nx - 1);
                const float cx = x + barW * (static_cast<float>(cursorIdx) + 0.5f);
                screen.drawRect(sgc::Rectf{cx - 1.0f, y, 2.0f, graphH}, accentCol);
            }
        }
        else
        {
            screen.drawTextInRect(
                sgc::Rectf{x, y, w, 24.0f},
                "(no player_x history yet)",
                muteCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
        }
        y += graphH + 8.0f;

        // ── Hint footer ───────────────────────────────────────────
        char hintBuf[160];
        std::snprintf(hintBuf, sizeof(hintBuf),
            "click graph = scrub   ·   click right edge = live   ·   (observes game, never freezes it)");
        (void)capacity;  // shown in header context only; kept for future use
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, hintBuf,
            muteCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    /// Event timeline — a time-ordered list of sparse gameplay milestones read
    /// from the producer's append-only JSONL (%TEMP%\mitiru_events_<pid>.jsonl).
    /// The SAME file is machine-readable by an AI agent via tail/Read — the
    /// window and the agent observe one substrate (dual-readable). Newest at
    /// the bottom; type drives the swatch colour, invariant_violation is
    /// emphasised (full red row).
    void drawEventTimeline(mitiru::Screen& screen, float x, float y, float w)
    {
        const sgc::Colorf inkCol{0.063f, 0.063f, 0.063f, 1.0f};
        const sgc::Colorf muteCol{0.290f, 0.290f, 0.290f, 1.0f};
        const sgc::Colorf redCol{0.784f, 0.0f, 0.173f, 1.0f};

        if (!m_eventReader || m_eventReader->events().empty())
        {
            const char* msg = (m_producerPid || m_overridePath)
                ? "waiting for events... (play the game — hit / death / game_over)"
                : "no source — pass <pid> or --file <events.jsonl>";
            screen.drawTextInRect(
                sgc::Rectf{x, y, w, 24.0f}, msg, muteCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            return;
        }

        const auto& events = m_eventReader->events();

        // Status header.
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "%zu events  (newest at bottom)",
                      events.size());
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, hdr, muteCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        y += 26.0f;

        const float rowH      = 24.0f;
        const float bottom    = m_screenH - 36.0f;  // footer guard
        const std::size_t maxRows =
            static_cast<std::size_t>(std::max(0.0f, (bottom - y) / rowH));
        const std::size_t start =
            events.size() > maxRows ? events.size() - maxRows : 0;

        const float swatchW = 4.0f;
        const float frameColW = 64.0f;
        const float typeColW  = 150.0f;

        for (std::size_t i = start; i < events.size(); ++i)
        {
            if (y + rowH > bottom) { break; }
            const auto& e = events[i];
            const bool isViolation = (e.type == "invariant_violation");

            // Whole-row tint for violations so they jump out.
            if (isViolation)
            {
                screen.drawRect(sgc::Rectf{x, y, w, rowH - 2.0f},
                                sgc::Colorf{redCol.r, redCol.g, redCol.b, 0.16f});
            }

            // Left swatch — colour by type.
            const sgc::Colorf swatch = eventColor(e.type);
            screen.drawRect(sgc::Rectf{x, y + 3.0f, swatchW, rowH - 8.0f}, swatch);

            // frame # column.
            char frameBuf[24];
            std::snprintf(frameBuf, sizeof(frameBuf), "f%u", e.frame);
            screen.drawTextInRect(
                sgc::Rectf{x + swatchW + 8.0f, y, frameColW, rowH},
                frameBuf, muteCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);

            // type column.
            screen.drawTextInRect(
                sgc::Rectf{x + swatchW + 8.0f + frameColW, y, typeColW, rowH},
                e.type.c_str(),
                isViolation ? redCol : inkCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);

            // data column — compact dump of the payload.
            const float dataX = x + swatchW + 8.0f + frameColW + typeColW;
            std::string data = e.data.is_null() ? "" : e.data.dump();
            if (data.size() > 64) { data = data.substr(0, 61) + "..."; }
            screen.drawTextInRect(
                sgc::Rectf{dataX, y, std::max(40.0f, x + w - dataX), rowH},
                data.c_str(), muteCol, 14.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);

            y += rowH;
        }
    }

    /// Map an event type to a Saturn-palette swatch colour.
    static sgc::Colorf eventColor(const std::string& type)
    {
        if (type == "hit")                 { return {0.784f, 0.0f, 0.173f, 1.0f}; } // Saturn red
        if (type == "invariant_violation") { return {0.784f, 0.0f, 0.173f, 1.0f}; } // Saturn red
        if (type == "enemy_death")         { return {0.063f, 0.063f, 0.063f, 1.0f}; } // ink
        if (type == "game_over")           { return {0.063f, 0.063f, 0.063f, 1.0f}; } // ink
        return {0.290f, 0.290f, 0.290f, 1.0f};  // mid gray (restart / other)
    }

    /// Bottom-half "Debug Log" — newest lines at the bottom, wraps as it scrolls.
    /// Lines come from the producer's mitiru::debug::println / printf calls.
    void drawDebugLog(mitiru::Screen& screen,
                      const nlohmann::json& lines,
                      float x, float y, float w, float h)
    {
        const float headerH = 24.0f;
        // Header bar — ink (#101010) on silver page
        screen.drawRect(
            sgc::Rectf{x, y, w, headerH},
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});
        screen.drawTextInRect(
            sgc::Rectf{x + 6.0f, y + 3.0f, w - 12.0f, headerH - 6.0f},
            "Debug Log",
            sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f},  // #c8002c Saturn red
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        const float bodyY = y + headerH + 2.0f;
        const float bodyH = h - headerH - 2.0f;
        // Body — light gray (#d8d8d8) inset against silver page
        screen.drawRect(
            sgc::Rectf{x, bodyY, w, bodyH},
            sgc::Colorf{0.847f, 0.847f, 0.847f, 1.0f});

        if (lines.empty())
        {
            screen.drawTextInRect(
                sgc::Rectf{x + 8.0f, bodyY + 6.0f, w - 16.0f, 18.0f},
                "(no log entries yet)",
                sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            return;
        }

        const float rowH       = 20.0f;
        const std::size_t maxRows = static_cast<std::size_t>((bodyH - 8.0f) / rowH);
        const std::size_t start = lines.size() > maxRows
            ? lines.size() - maxRows
            : 0;

        float row = bodyY + 4.0f;
        for (std::size_t i = start; i < lines.size(); ++i)
        {
            if (!lines[i].is_string()) { continue; }
            screen.drawTextInRect(
                sgc::Rectf{x + 8.0f, row, w - 16.0f, rowH},
                lines[i].get_ref<const std::string&>().c_str(),
                sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},  // ink on light gray
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            row += rowH;
        }
    }

    /// Generic flat key/value renderer.
    void drawKvSection(mitiru::Screen& screen,
                       const std::string& title,
                       const nlohmann::json& obj,
                       float x, float y, float w)
    {
        if (!title.empty())
        {
            screen.drawTextInRect(
                sgc::Rectf{x, y, w, 28.0f},
                title.c_str(),
                sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f},  // Saturn red
                24.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            y += 32.0f;
        }
        const float rowH    = 22.0f;
        const float keyColW = std::min(w * 0.40f, 200.0f);
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (y + rowH > m_screenH - 36.0f) { break; }
            screen.drawTextInRect(
                sgc::Rectf{x, y, keyColW, rowH},
                it.key().c_str(),
                sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            screen.drawTextInRect(
                sgc::Rectf{x + keyColW + 6.0f, y, w - keyColW - 6.0f, rowH},
                compactValue(it.value()).c_str(),
                sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            y += rowH + 2.0f;
        }
    }

    /// Render a producer-registered Inspectable's `state` JSON. Handles the
    /// common shapes: object → key/value list; array → indexed bullet list;
    /// primitive → centred big text. Specialised renderers (input table, log
    /// scroller) kick in when the inspectable's name hints at them.
    void drawInspectableState(mitiru::Screen& screen,
                              const nlohmann::json& state,
                              float x, float y, float w)
    {
        // Built-in specialisations the user can opt into by naming.
        if (m_inspectable == "input" && state.is_object())
        {
            // Dedicated Input panel — full window height available.
            drawInputSection(screen, state, x, y, w, m_screenH - 36.0f);
            return;
        }
        if (m_inspectable == "log" && state.is_array())
        {
            drawDebugLog(screen, state, x, y, w, m_screenH - y - 24.0f);
            return;
        }

        if (state.is_array())
        {
            const float rowH = 22.0f;
            float ry = y;
            for (std::size_t i = 0; i < state.size(); ++i)
            {
                if (ry + rowH > m_screenH - 36.0f) { break; }
                char idx[16];
                std::snprintf(idx, sizeof(idx), "[%zu]", i);
                screen.drawTextInRect(
                    sgc::Rectf{x, ry, 48.0f, rowH}, idx,
                    sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f}, 16.0f,  // Saturn red index
                    mitiru::Screen::TextAlignH::Left,
                    mitiru::Screen::TextAlignV::Top);
                screen.drawTextInRect(
                    sgc::Rectf{x + 52.0f, ry, w - 52.0f, rowH},
                    compactValue(state[i]).c_str(),
                    sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f}, 16.0f,
                    mitiru::Screen::TextAlignH::Left,
                    mitiru::Screen::TextAlignV::Top);
                ry += rowH + 2.0f;
            }
            return;
        }

        if (state.is_object())
        {
            drawKvSection(screen, "", state, x, y, w);
            return;
        }

        // primitive
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 32.0f}, compactValue(state).c_str(),
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f}, 24.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    /// Purpose-built input section: held keys (chips), mouse pos+buttons,
    /// per-key press counters, recent event history.
    /// @return 描画後の最終 y (caller の縦フローを正しく進めるため)。
    /// @param bottomLimit この y を超える行は描画しない (section overlap 防止)。
    float drawInputSection(mitiru::Screen& screen,
                          const nlohmann::json& obj,
                          float x, float y, float w, float bottomLimit)
    {
        // Held keys — single line of comma-joined names.
        std::string heldLine = "held: ";
        if (obj.contains("held") && obj["held"].is_array() && !obj["held"].empty())
        {
            for (std::size_t i = 0; i < obj["held"].size(); ++i)
            {
                if (i > 0) { heldLine += ", "; }
                heldLine += obj["held"][i].get<std::string>();
            }
        }
        else { heldLine += "(none)"; }
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, heldLine.c_str(),
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f}, 16.0f,
            mitiru::Screen::TextAlignH::Left, mitiru::Screen::TextAlignV::Top);
        y += 24.0f;

        // Mouse line.
        char mouseLine[160];
        std::string btns;
        if (obj.contains("mouseBtns") && obj["mouseBtns"].is_array())
        {
            for (std::size_t i = 0; i < obj["mouseBtns"].size(); ++i)
            {
                if (!btns.empty()) { btns += " "; }
                btns += obj["mouseBtns"][i].get<std::string>();
            }
        }
        std::snprintf(mouseLine, sizeof(mouseLine),
            "mouse: (%d, %d) %s",
            static_cast<int>(obj.value("mouseX", 0.0f)),
            static_cast<int>(obj.value("mouseY", 0.0f)),
            btns.c_str());
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, mouseLine,
            sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f}, 16.0f,
            mitiru::Screen::TextAlignH::Left, mitiru::Screen::TextAlignV::Top);
        y += 28.0f;

        // Press counts.
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, "press counts",
            sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f}, 16.0f,  // Saturn red
            mitiru::Screen::TextAlignH::Left, mitiru::Screen::TextAlignV::Top);
        y += 24.0f;
        if (obj.contains("stats") && obj["stats"].is_array())
        {
            const float rowH  = 22.0f;
            // Respect caller's band AND the footer guard, whichever is tighter.
            const float limit = std::min(bottomLimit, m_screenH - 36.0f);
            for (const auto& s : obj["stats"])
            {
                if (y + rowH > limit) { break; }
                const std::string name = s.value("name", "");
                const int         cnt  = s.value("count", 0);
                const bool        held = s.value("held", false);
                char line[80];
                std::snprintf(line, sizeof(line),
                    "%-10s %4d%s",
                    name.c_str(), cnt, held ? "   *" : "");
                screen.drawTextInRect(
                    sgc::Rectf{x, y, w, rowH}, line,
                    cnt > 0 ? sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f}
                            : sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                    16.0f,
                    mitiru::Screen::TextAlignH::Left,
                    mitiru::Screen::TextAlignV::Top);
                y += rowH;
            }
        }
        return y;
    }

    void drawFooter(mitiru::Screen& screen)
    {
        // Saturn: silver bg + 1px hairline top divider (matches header).
        const float h = 32.0f;
        const float y = m_screenH - h;
        screen.drawRect(
            sgc::Rectf{0.0f, y, m_screenW, 1.0f},
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});  // #101010 ink border

        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "updates %d  ·  %.1fs  ·  [ESC] quit",
            m_updateCount, m_elapsed);
        screen.drawTextInRect(
            sgc::Rectf{12.0f, y + 8.0f, m_screenW - 24.0f, h - 12.0f},
            buf,
            sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},  // text-mute on silver
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    std::string sourceLabel() const
    {
        if (m_overridePath)
        {
            return std::string{"file "} + m_overridePath->filename().string();
        }
        if (m_producerPid)
        {
            return std::string{"pid "} + std::to_string(*m_producerPid);
        }
        return "no source";
    }

    std::optional<int>                                       m_producerPid;
    std::optional<std::filesystem::path>                     m_overridePath;
    Panel                                                    m_panel{Panel::State};
    std::string                                              m_inspectable;
    std::string                                              m_currentTitle;
    std::optional<mitiru::observe::SharedSnapshot::Reader>   m_reader;
    std::optional<mitiru::observe::EventLog::Reader>         m_eventReader;
    std::size_t                                              m_eventKeep{64};
    std::optional<nlohmann::json>                            m_state;
    std::filesystem::file_time_type                          m_lastMtime{};
    bool                                                     m_haveMtime{false};
    float                                                    m_screenW{960.0f};
    float                                                    m_screenH{540.0f};
    float                                                    m_elapsed{0.0f};
    float                                                    m_pollAccum{0.0f};
    int                                                      m_updateCount{0};

    // ── Local view-only scrub (P2) ───────────────────────────────────────
    // Last-drawn HP graph geometry, stashed by drawTimeTravelBody() so update()
    // can hit-test a click against it. m_hpGraphCapacity holds the ARRAY length
    // (oldest-left .. newest-right); clicks map to array indices.
    float                                                    m_hpGraphX{0.0f};
    float                                                    m_hpGraphY{0.0f};
    float                                                    m_hpGraphW{0.0f};
    float                                                    m_hpGraphH{0.0f};
    int                                                      m_hpGraphCapacity{0};
    bool                                                     m_hpGraphValid{false};

    // Local scrub state — owned entirely by the inspector. The game is never
    // told about it (pure observation; no reverse channel, no freeze).
    bool                                                     m_ttScrubbing{false};  // false = live (newest)
    int                                                      m_ttOffset{0};         // 0 = newest(right), len-1 = oldest(left)
};

void printUsage()
{
    std::fputs(
        "usage:\n"
        "  mitiru_inspector <pid> [--panel state|input|log] [--inspectable <name>]\n"
        "  mitiru_inspector --file <path> ...\n"
        "\n"
        "  --panel <name>          built-in layout (state/input/log/events)\n"
        "  --inspectable <name>    render only the producer-registered Inspectable\n",
        stderr);
}

}  // namespace

int main(int argc, char* argv[])
{
    std::optional<int>         producerPid;
    std::optional<std::string> filePath;
    Panel                      panel = Panel::State;
    std::string                inspectable;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--file" && i + 1 < argc)
        {
            filePath = argv[++i];
        }
        else if (a == "--panel" && i + 1 < argc)
        {
            panel = parsePanel(argv[++i]);
        }
        else if (a == "--inspectable" && i + 1 < argc)
        {
            inspectable = argv[++i];
            // Special name "timetravel" auto-routes to the TimeTravel panel
            // so producers can opt into the rich scrub UI just by naming
            // their inspectable accordingly (DLL-side change only).
            panel = (inspectable == "timetravel" || inspectable == "tt")
                ? Panel::TimeTravel
                : Panel::Inspectable;
        }
        else if (a == "-h" || a == "--help")
        {
            printUsage();
            return 0;
        }
        else
        {
            try
            {
                producerPid = std::stoi(a);
            }
            catch (...)
            {
                printUsage();
                return 2;
            }
        }
    }

    if (!producerPid && !filePath)
    {
        printUsage();
        return 2;
    }

    mitiru::Engine engine;
    Inspector game(producerPid, filePath, panel, inspectable);

    mitiru::EngineConfig cfg;
    std::string title = std::string{"MitiruEngine — inspector"};
    if (panel == Panel::Inspectable && !inspectable.empty())
    {
        title += " · " + inspectable;
    }
    else
    {
        title += panelTitleSuffix(panel);
    }
    cfg.title           = title.c_str();  // Engine stores std::string; safe
    // Sub-window default: compact enough to fit on a 1080p monitor next to
    // the game window without dominating it. User can resize freely.
    // Default to a "vertical sidebar" geometry that fits next to a 1080p
    // game window without dominating it. Crisper than wide+short because
    // dense inspector data reads better in a narrow scrollable column.
    cfg.windowWidth     = 360;
    cfg.windowHeight    = 720;
    cfg.vsync           = true;
    cfg.enableCef       = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;
    // DPI awareness — without this, 720x540 on a 125%/150% scaled monitor
    // renders to a 720x540 physical back buffer that the OS then bilinear-
    // upscales for display = blurry text. useLogicalWindowSize=true makes
    // the engine scale the back buffer to physical px upfront → text stays
    // crisp on high-DPI displays.
    cfg.useLogicalWindowSize = true;
    // Mitiru Saturn silver-gray base — must match the host so the
    // inspector visually unifies with hello_game / launcher / companion.
    // `screen.clear(...)` in draw() only sets the Screen-side clear value;
    // the actual device ClearRenderTargetView uses this config field
    // (see Engine_Frame.hpp). Without this, the back buffer is whatever
    // the device defaults to (typically black) — defeats the whole theme.
    cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};

    engine.run(game, cfg);
    return 0;
}
