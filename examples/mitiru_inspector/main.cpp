// mitiru_inspector — 軸 5 (modular sub-window architecture) の種。
//
// 別 OS プロセスの独立窓。動作中の MitiruEngine ゲームの state を監視しライブ描画する。
// CEF / audio / ECS なし — renderer + file-mtime polling のみ。別モニタに drag すれば、
// main window のゲームを止めずに本物の sub-window inspector が手に入る。
//
// producer (gameplay) は
//   %TEMP%\mitiru_inspector_<pid>.json
// に書く (include/mitiru/observe/SharedSnapshot.hpp 参照)。inspector は ~30 Hz で読み描画。
//
// Usage:
//   mitiru_inspector <pid>          # その pid のプロセスを監視
//   mitiru_inspector --file <path>  # 特定ファイルを直接監視
//
// なぜこの design か (CEF multi-process でなく):
//   - 現状 CEF は engine 内で single-process mode 動作 (V8 proxy resolver の制約;
//     docs/adr/0004-modular-sub-window-architecture.md 参照)
//   - 別プロセスの native sub-window なら multi-monitor / クラッシュ隔離が容易で、
//     hot reload で既に使う file-mtime polling 以上の IPC が要らない

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
#include <mitiru/observe/TimeTravelMarkers.hpp>
#include <vector>

namespace {

// json value を 1 行に整形。長すぎる array/object は切り詰め、float は小数 2 桁に丸める
// (453.33353764648438 のような UI を散らかすゴミを出さないため)。
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

// inspector が snapshot のどの subset を描画するか。`--panel <name>` CLI arg で選ぶ。
// 同じ exe を無関係なデータで画面を膨らませずに focused viewer (例: input のみ) にできる。
//
// --panel state|input|log は固定の built-in layout を指す。
// --inspectable <name> は producer 登録の Inspectable (mitiru::debug::registerInspectable /
//   LocalInspectable) を指す。指定時は snapshot["<name>"]["state"] を読み generic な
//   key/value tree として描画。title は snapshot["<name>"]["title"] から取る。
enum class Panel
{
    State,         ///< gameplay + input + debugLog (旧 schema 後方互換)
    Input,         ///< input section のみ、全画面
    Log,           ///< debug log のみ、全画面
    Inspectable,   ///< snapshot[m_inspectable]["state"] のみ描画
    TimeTravel,    ///< P2 差別化: scrub bar + HP graph (--inspectable timetravel で
                   ///  自動選択)
    Events,        ///< Event timeline — %TEMP%\mitiru_events_<pid>.jsonl を読む
                   ///  (append-only JSONL、AI から dual-readable)。pid 必須。
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
        case Panel::Inspectable: return "";  // inspectable 自身の title で上書きされる
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

        // ローカル view-only scrub: 直近描画の HP graph rect 内の左クリックを、inspector が
        // 既に受け取った history への LOCAL array offset へ写像する。ゲームへは何も送らない —
        // game window は純粋な gameplay で freeze しない。scrub はこの観察窓内で完結する。
        // producer の協力不要なので source (pid / file) を問わず動く。
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

        // Mitiru Saturn — シルバーグレー基調 (#c8c8c8)。
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
    /// 狭い窓では 1 段小さい (atlas 整合) font を返す。SDF を滲ませずに行を多く収め、
    /// 文字が "..." へ切り詰められるのを避ける。閾値より広ければ wide をそのまま使う。
    [[nodiscard]] float scaledFont(float wide, float narrow) const noexcept
    {
        return m_screenW < 420.0f ? narrow : wide;
    }

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
            catch (...) { /* 読み取り途中で truncate されていた; リトライ */ }
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

    /// append-only JSONL event timeline を poll。source は pid
    /// (→ %TEMP%\mitiru_events_<pid>.jsonl) か明示的な --file path。
    /// reader は最新 N 件を保持し、新規読み取りで counter を進める。
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
        // 高さは選択 font (16px header text + padding) が余裕で収まる寸法。SDF atlas は
        // 32px 生成なので 16 = きれいな 0.5x downscale → このサイズでも crisp。
        // Saturn: silver bg + 1px hairline divider
        // (launcher / hello_game / companion の surface 処理と揃える)。
        const float h = 36.0f;
        // 下端 1px hairline separator (反転帯なし)。
        screen.drawRect(
            sgc::Rectf{0.0f, h - 1.0f, m_screenW, 1.0f},
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});  // #101010 ink ボーダー

        // 狭い窓ではフル title が "..." に切れるので、情報量の多い 1 つ
        // (panel 名 or inspectable) だけに絞る。prefix と source は広い時のみ。
        std::string title;
        if (m_screenW < 520.0f)
        {
            title = !m_currentTitle.empty() ? m_currentTitle
                  : (!m_inspectable.empty() ? m_inspectable : "inspector");
        }
        else
        {
            title = "MitiruEngine — inspector";
            if (!m_currentTitle.empty())     { title += "  ·  " + m_currentTitle; }
            else if (!m_inspectable.empty()) { title += "  ·  " + m_inspectable; }
            title += "  ·  " + sourceLabel();
        }

        screen.drawTextInRect(
            sgc::Rectf{12.0f, 8.0f, m_screenW - 24.0f, h - 12.0f},
            title.c_str(),
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},  // silver 上の ink
            scaledFont(16.0f, 12.0f),
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawJsonBody(mitiru::Screen& screen)
    {
        // 以下の font size は全て SDF atlas size (32px) のきれいな分数に揃える:
        // 24 = 0.75x, 16 = 0.5x。半端なサイズ (18, 19, 20) は細い glyph (l, i, .) を
        // SDF 閾値以下に痩せさせ、隙間として見える原因になる。
        // Header bar は 36px。最初の section title が
        // "MitiruEngine — inspector · pid" 行に詰まらないよう余白を取って body 開始。
        const float top = 48.0f;

        // Event timeline: SharedSnapshot の m_state とは独立 — EventLog::Reader 経由で
        // append-only JSONL を読む。m_state が無くても描画する。
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

        // TimeTravel: リッチな scrub bar + HP graph。DLL は inspectable 名 "timetravel" で
        // raw history のみ export: { capacity, hpMax, hpHistory[], xHistory[] }。scrub state はローカル。
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

        // --inspectable <name> は全てに優先: 指定 Inspectable の state のみ描画。
        // producer の wire format は
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
            // この snapshot に Inspectable が無い — 待機表示を出す。
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
        // top-level key は各 Inspectable。wrapper でなく `state` を展開し、
        // user に実データ (hp, x, y, ...) を見せる。
        const float pad        = 14.0f;
        const float bodyWidth  = m_screenW - pad * 2.0f;
        const float bottomGuard = 36.0f;  // footer の高さ
        float y                = top;
        bool  drewAnything     = false;
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (y + 28.0f > m_screenH - bottomGuard) { break; }
            if (!it.value().is_object()) { continue; }

            std::string title = it.value().value("title", it.key());
            const bool hasStateKey = it.value().contains("state");

            // Section header — 強調用に 24px (0.75x atlas)。狭い窓は 16px へ。
            screen.drawTextInRect(
                sgc::Rectf{pad, y, bodyWidth, 28.0f},
                title.c_str(),
                sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f},  // #c8002c Saturn red
                scaledFont(24.0f, 16.0f),
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            y += 32.0f;

            // Section body — `state` subtree を優先 (wrapper 規約)。
            // wrap されてない entry は object 自体に fallback。
            const nlohmann::json& body = hasStateKey ? it.value()["state"] : it.value();

            if (it.key() == "input" && body.is_object())
            {
                // 専用 compact renderer (held keys / mouse / counts)。
                // 長い press-count list が次 section に溢れないよう 220px band に制限する。
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
                const float kvFont = scaledFont(16.0f, 12.0f);
                for (auto sIt = body.begin(); sIt != body.end(); ++sIt)
                {
                    if (y + rowH > m_screenH - bottomGuard) { break; }
                    screen.drawTextInRect(
                        sgc::Rectf{pad, y, keyColW, rowH},
                        sIt.key().c_str(),
                        sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                        kvFont,
                        mitiru::Screen::TextAlignH::Left,
                        mitiru::Screen::TextAlignV::Top);
                    screen.drawTextInRect(
                        sgc::Rectf{pad + keyColW + 8.0f, y, bodyWidth - keyColW - 8.0f, rowH},
                        compactValue(sIt.value()).c_str(),
                        sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},
                        kvFont,
                        mitiru::Screen::TextAlignH::Left,
                        mitiru::Screen::TextAlignV::Top);
                    y += rowH + 2.0f;
                }
                y += 12.0f;  // section 間の gap
            }
            else
            {
                // primitive — 1 行で大きく
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
            // 旧 / 未知 schema への fallback — compact kv で dump。
            drawKvSection(screen, "", obj, pad, top, bodyWidth);
        }
    }

    /// ローカル view-only scrub: HP graph rect 内の左クリックを、この inspector が既に持つ
    /// history への LOCAL array-index offset へ写像。ゲームへは何も送らず freeze もしない。
    /// graph は array 上で oldest-left .. newest-right に並ぶ (len = m_hpGraphCapacity)。
    /// m_ttOffset は newest から逆算 (0 = newest = 右端) なので
    ///   offset = (len-1) - round((clickX - graphX) / graphW * (len-1))
    /// 右端 (または offset 0) のクリックで live mode に戻る。
    void handleGraphClick()
    {
        if (m_panel != Panel::TimeTravel) { return; }
        if (!m_hpGraphValid || m_hpGraphCapacity < 1) { return; }

        const auto [mx, my] = input().mousePosition();
        if (mx < m_hpGraphX || mx > m_hpGraphX + m_hpGraphW) { return; }
        if (my < m_hpGraphY || my > m_hpGraphY + m_hpGraphH) { return; }

        const int len = m_hpGraphCapacity;  // frame capacity でなく array 長
        float frac = m_hpGraphW > 0.0f ? (mx - m_hpGraphX) / m_hpGraphW : 0.0f;
        frac = std::clamp(frac, 0.0f, 1.0f);
        const int idxFromOldest =
            static_cast<int>(std::lround(frac * static_cast<float>(len - 1)));
        int offset = (len - 1) - idxFromOldest;  // 右端 → 0 (newest)
        offset = std::clamp(offset, 0, len - 1);

        // marker があれば最寄り節目へ snap (「HP が落ちたフレームへ一発で飛ぶ」)。
        // nearestMarker は header の関数 (TestTimeTravelMarkers が網羅) を再利用。
        if (const auto* mk = mitiru::observe::nearestMarker(
                m_ttMarkers, static_cast<std::size_t>(offset)))
        {
            offset = static_cast<int>(mk->offsetFromNewest);
        }

        m_ttOffset = offset;
        // 右端 (または算出 newest) は live に戻す。それ以外は scrub。
        m_ttScrubbing = !(frac > 0.97f || offset == 0);
    }

    /// Time-travel scrub panel — LOCAL view-only scrub。producer は raw history のみ
    /// export し、inspector が scrub state を自分で計算。ゲームへは何も返さない
    /// (freeze なし、逆方向 channel なし)。
    /// producer JSON schema (新契約):
    ///   { capacity, hpMax, hpHistory[]  (oldest→newest, downsampled),
    ///     xHistory[] (oldest→newest, downsampled) }
    /// ローカル scrub state は m_ttScrubbing / m_ttOffset に持つ (array-index 単位;
    /// offset 0 = newest = 右端)。
    void drawTimeTravelBody(mitiru::Screen& screen,
                            const nlohmann::json& s,
                            float x, float y, float w)
    {
        // capacity = 記録された総 frame 数 (情報表示のみ)。
        const int   capacity = s.value("capacity", 0);
        const int   hpMax    = s.value("hpMax", 100);

        const sgc::Colorf muteCol{0.290f, 0.290f, 0.290f, 1.0f};       // #4a4a4a 中間グレー
        const sgc::Colorf accentCol{0.784f, 0.0f, 0.173f, 1.0f};       // #c8002c Saturn red
        const sgc::Colorf dangerCol{0.784f, 0.0f, 0.173f, 1.0f};       // #c8002c Saturn red (単一 accent)
        const sgc::Colorf xAxisCol{0.063f, 0.063f, 0.063f, 1.0f};      // #101010 ink — 副系列
        const sgc::Colorf borderCol{0.063f, 0.063f, 0.063f, 1.0f};     // #101010 ink ボーダー

        const auto& hist  = s.value("hpHistory", nlohmann::json::array());
        const auto& xHist = s.value("xHistory",  nlohmann::json::array());
        const int   n     = static_cast<int>(hist.size());
        const int   nx    = static_cast<int>(xHist.size());

        // 節目 marker を parse。offsetFromNewest は hpHistory と同一 index 空間なので
        // bar と同じ式で位置でき、click 時の nearestMarker() snap にもそのまま使える。
        m_ttMarkers.clear();
        if (s.contains("markers") && s["markers"].is_array())
        {
            for (const auto& e : s["markers"])
            {
                if (!e.is_object()) { continue; }
                mitiru::observe::Marker mk;
                mk.offsetFromNewest = static_cast<std::size_t>(e.value("o", 0));
                mk.value            = e.value("v", 0.0);
                mk.kind             = static_cast<mitiru::observe::MarkerKind>(e.value("k", 0));
                m_ttMarkers.push_back(mk);
            }
        }

        // ローカル scrub offset を現在の array 長に clamp (history はゲーム進行で増え、
        // 縮小/reset もあり得る)。offset 0 = newest。
        if (n > 0 && m_ttOffset > n - 1) { m_ttOffset = std::max(0, n - 1); }
        if (n <= 0) { m_ttScrubbing = false; }

        // 既に持つ array から scrub 値をローカル計算。
        // newest = 末尾要素。scrub 中は m_ttOffset 分だけ遡る。
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

        // ── ステータス header ─────────────────────────────────────
        char hdr[160];
        const bool narrow = m_screenW < 420.0f;
        if (m_ttScrubbing)
        {
            if (narrow)
            {
                std::snprintf(hdr, sizeof(hdr),
                    "SCRUB -%d/%d  HP %d/%d",
                    m_ttOffset, std::max(0, n - 1), hpAtScrub, hpMax);
            }
            else
            {
                std::snprintf(hdr, sizeof(hdr),
                    "SCRUBBING   sample -%d/%d   HP: %d/%d   X: %.1f",
                    m_ttOffset, std::max(0, n - 1), hpAtScrub, hpMax, xAtScrub);
            }
        }
        else
        {
            std::snprintf(hdr, sizeof(hdr), narrow
                ? "(live — click to scrub)"
                : "(live — click graph to scrub · click right edge = live)");
        }
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 24.0f}, hdr,
            m_ttScrubbing ? accentCol : muteCol,
            scaledFont(16.0f, 12.0f),
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        y += 30.0f;

        // 縦の空きを 2 段 graph に分割し、間に label strip + gap を挟む。
        // 下部の hint footer 用に余白を確保。
        const float hintReserve = 28.0f;
        const float labelStrip  = 22.0f;
        const float gap         = 8.0f;
        const float totalAvail  = std::max(80.0f, m_screenH - y - hintReserve - 14.0f);
        const float graphH      = std::max(40.0f, (totalAvail - labelStrip - gap) * 0.5f);

        // ── HP graph (上半分、bar) ────────────────────────────────
        screen.drawTextInRect(
            sgc::Rectf{x, y - 22.0f, w, 18.0f}, "HP",
            muteCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        // click-to-scrub の hit test 用に HP graph rect を記録 (update() は draw() と
        // 別タイミングで走るので直近 geometry を退避)。
        // m_hpGraphCapacity は ARRAY 長を保持 (ローカル scrub は real frame でなく
        // array index に click を写像)。
        m_hpGraphX        = x;
        m_hpGraphY        = y;
        m_hpGraphW        = w;
        m_hpGraphH        = graphH;
        m_hpGraphCapacity = n;
        m_hpGraphValid    = (n > 0);

        if (n > 0 && hpMax > 0)
        {
            // box border
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

            // ── 節目 marker tick ──────────────────────────────────────
            // producer 抽出の marker を bar と同じ index 空間で重ねる。
            // offsetFromNewest o → 古い順 index = (n-1)-o。kind で色分け。
            for (const auto& mk : m_ttMarkers)
            {
                const int idxFromOldest = (n - 1) - static_cast<int>(mk.offsetFromNewest);
                if (idxFromOldest < 0 || idxFromOldest >= n) { continue; }
                const float       tx   = x + barW * (static_cast<float>(idxFromOldest) + 0.5f);
                const sgc::Colorf tcol = markerColor(mk.kind);
                screen.drawRect(sgc::Rectf{tx - 0.5f, y, 1.5f, graphH},
                                sgc::Colorf{tcol.r, tcol.g, tcol.b, 0.9f});
                // 上端に小さなノッチ — tick をグラフ上で視認しやすく。
                screen.drawRect(sgc::Rectf{tx - 2.5f, y, 5.0f, 4.0f}, tcol);
            }

            // ローカル cursor — array-index 基準。cursorIdx は oldest (左) から数え、
            // bar 中央に配置。scrub 中のみ表示。
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

        // ── PLAYER X ラベル帯 ──────────────────────────────────────
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, labelStrip}, "PLAYER X",
            xAxisCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        y += labelStrip;

        // ── player_x graph (下半分、bar; xMin..xMax に auto-fit) ──
        if (nx > 0)
        {
            // box border
            screen.drawRect(sgc::Rectf{x, y, w, 1.0f}, borderCol);
            screen.drawRect(sgc::Rectf{x, y + graphH, w, 1.0f}, borderCol);
            screen.drawRect(sgc::Rectf{x, y, 1.0f, graphH}, borderCol);
            screen.drawRect(sgc::Rectf{x + w - 1.0f, y, 1.0f, graphH}, borderCol);

            // auto-fit: 範囲を scan し、各 sample を [0,1] に正規化。
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
            const bool  flatish = xRange < 1.0f;  // player 静止 → 広がりなし

            const float barW = w / static_cast<float>(nx);
            for (int i = 0; i < nx; ++i)
            {
                float v = 0.0f;
                try { v = xHist[i].get<float>(); } catch (...) {}
                // flat data → 50% 中央で描画。空 box でなく「データはある、ただ不変」
                // と user に伝える。
                const float pct = flatish ? 0.5f
                                          : std::clamp((v - xMin) / xRange, 0.0f, 1.0f);
                const float h   = (graphH - 2.0f) * pct;
                const float bx  = x + barW * static_cast<float>(i);
                const float by  = y + graphH - 1.0f - h;
                screen.drawRect(sgc::Rectf{bx, by, std::max(1.0f, barW - 0.5f), h},
                                sgc::Colorf{xAxisCol.r, xAxisCol.g, xAxisCol.b, 0.55f});
            }

            // ローカル cursor — array-index 基準、HP graph と対称。
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

        // ── ヒント footer ──────────────────────────────────────────
        char hintBuf[160];
        if (!m_ttMarkers.empty())
        {
            std::snprintf(hintBuf, sizeof(hintBuf), narrow
                ? "%zu markers · click = snap"
                : "%zu markers   ·   click = snap to nearest   ·   click right edge = live",
                m_ttMarkers.size());
        }
        else
        {
            std::snprintf(hintBuf, sizeof(hintBuf), narrow
                ? "click = scrub · right edge = live"
                : "click graph = scrub   ·   click right edge = live   ·   (observes game, never freezes it)");
        }
        (void)capacity;  // header context でのみ使用; 将来用に保持
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, hintBuf,
            muteCol, scaledFont(14.0f, 12.0f),
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    /// Event timeline — producer の append-only JSONL (%TEMP%\mitiru_events_<pid>.jsonl)
    /// から読む、時系列順の疎な gameplay マイルストーン一覧。同じファイルは AI agent からも
    /// tail/Read で機械可読 — 窓と agent が単一 substrate を観察する (dual-readable)。
    /// 最新が下。type が swatch 色を決め、invariant_violation は強調 (行全体を赤)。
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

        // ステータス header。
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "%zu events  (newest at bottom)",
                      events.size());
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, hdr, muteCol, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        y += 26.0f;

        const float rowH      = 24.0f;
        const float bottom    = m_screenH - 36.0f;  // footer ガード
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

            // violation は目立つよう行全体を tint。
            if (isViolation)
            {
                screen.drawRect(sgc::Rectf{x, y, w, rowH - 2.0f},
                                sgc::Colorf{redCol.r, redCol.g, redCol.b, 0.16f});
            }

            // 左 swatch — type で色分け。
            const sgc::Colorf swatch = eventColor(e.type);
            screen.drawRect(sgc::Rectf{x, y + 3.0f, swatchW, rowH - 8.0f}, swatch);

            // frame # 列。
            char frameBuf[24];
            std::snprintf(frameBuf, sizeof(frameBuf), "f%u", e.frame);
            screen.drawTextInRect(
                sgc::Rectf{x + swatchW + 8.0f, y, frameColW, rowH},
                frameBuf, muteCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);

            // type 列。
            screen.drawTextInRect(
                sgc::Rectf{x + swatchW + 8.0f + frameColW, y, typeColW, rowH},
                e.type.c_str(),
                isViolation ? redCol : inkCol, 16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);

            // data 列 — payload の compact dump。
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

    /// event type を Saturn パレットの swatch 色に写像。
    static sgc::Colorf eventColor(const std::string& type)
    {
        if (type == "hit")                 { return {0.784f, 0.0f, 0.173f, 1.0f}; } // Saturn red
        if (type == "invariant_violation") { return {0.784f, 0.0f, 0.173f, 1.0f}; } // Saturn red
        if (type == "enemy_death")         { return {0.063f, 0.063f, 0.063f, 1.0f}; } // ink
        if (type == "game_over")           { return {0.063f, 0.063f, 0.063f, 1.0f}; } // ink
        return {0.290f, 0.290f, 0.290f, 1.0f};  // 中間グレー (restart / その他)
    }

    /// marker kind を Saturn パレットの tick 色へ写像。
    /// danger 閾値の下抜けだけ red で強調、他は ink / 中間グレー。
    static sgc::Colorf markerColor(mitiru::observe::MarkerKind k)
    {
        using K = mitiru::observe::MarkerKind;
        switch (k)
        {
            case K::ThresholdDown: return {0.784f, 0.0f, 0.173f, 1.0f};  // Saturn red — danger 突入
            case K::ThresholdUp:   return {0.063f, 0.063f, 0.063f, 1.0f}; // ink
            case K::LocalMin:
            case K::LocalMax:      return {0.290f, 0.290f, 0.290f, 1.0f}; // 中間グレー
            case K::Edge:
            default:               return {0.063f, 0.063f, 0.063f, 1.0f}; // ink
        }
    }

    /// 下半分の "Debug Log" — 最新行が下、スクロールで折り返す。
    /// 行は producer の mitiru::debug::println / printf 呼び出し由来。
    void drawDebugLog(mitiru::Screen& screen,
                      const nlohmann::json& lines,
                      float x, float y, float w, float h)
    {
        const float headerH = 24.0f;
        // Header bar — silver ページ上の ink (#101010)
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
        // Body — silver ページに対し薄いグレー (#d8d8d8) の inset
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
                sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},  // 薄いグレー上の ink
                16.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            row += rowH;
        }
    }

    /// 汎用 flat key/value renderer。
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
        const float kvFont  = scaledFont(16.0f, 12.0f);
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            if (y + rowH > m_screenH - 36.0f) { break; }
            screen.drawTextInRect(
                sgc::Rectf{x, y, keyColW, rowH},
                it.key().c_str(),
                sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},
                kvFont,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            screen.drawTextInRect(
                sgc::Rectf{x + keyColW + 6.0f, y, w - keyColW - 6.0f, rowH},
                compactValue(it.value()).c_str(),
                sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f},
                kvFont,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
            y += rowH + 2.0f;
        }
    }

    /// producer 登録 Inspectable の `state` JSON を描画。よくある形を処理:
    /// object → key/value list; array → index 付き list; primitive → 中央大テキスト。
    /// inspectable 名がヒントになる場合は専用 renderer (input table / log scroller) が働く。
    void drawInspectableState(mitiru::Screen& screen,
                              const nlohmann::json& state,
                              float x, float y, float w)
    {
        // 名前で opt-in できる built-in 特殊化。
        if (m_inspectable == "input" && state.is_object())
        {
            // 専用 Input panel — 全画面高を使える。
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
                    sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f}, 16.0f,  // index は Saturn red
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

    /// 専用 input section: held keys (chips)、mouse pos+buttons、
    /// key 別 press counter、最近の event history。
    /// @return 描画後の最終 y (caller の縦フローを正しく進めるため)。
    /// @param bottomLimit この y を超える行は描画しない (section overlap 防止)。
    float drawInputSection(mitiru::Screen& screen,
                          const nlohmann::json& obj,
                          float x, float y, float w, float bottomLimit)
    {
        // held keys — カンマ区切り名を 1 行で。
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

        // mouse 行。
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

        // press count。
        screen.drawTextInRect(
            sgc::Rectf{x, y, w, 22.0f}, "press counts",
            sgc::Colorf{0.784f, 0.0f, 0.173f, 1.0f}, 16.0f,  // Saturn red
            mitiru::Screen::TextAlignH::Left, mitiru::Screen::TextAlignV::Top);
        y += 24.0f;
        if (obj.contains("stats") && obj["stats"].is_array())
        {
            const float rowH  = 22.0f;
            // caller の band と footer guard の、厳しい方に従う。
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
        // Saturn: silver bg + 上端 1px hairline divider (header と揃える)。
        const float h = 32.0f;
        const float y = m_screenH - h;
        screen.drawRect(
            sgc::Rectf{0.0f, y, m_screenW, 1.0f},
            sgc::Colorf{0.063f, 0.063f, 0.063f, 1.0f});  // #101010 ink ボーダー

        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "updates %d  ·  %.1fs  ·  [ESC] quit",
            m_updateCount, m_elapsed);
        screen.drawTextInRect(
            sgc::Rectf{12.0f, y + 8.0f, m_screenW - 24.0f, h - 12.0f},
            buf,
            sgc::Colorf{0.290f, 0.290f, 0.290f, 1.0f},  // silver 上の text-mute
            scaledFont(16.0f, 12.0f),
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

    // ── ローカル view-only scrub (P2) ─────────────────────────────────────
    // 直近描画の HP graph geometry。update() が click を hit-test できるよう
    // drawTimeTravelBody() が退避する。m_hpGraphCapacity は ARRAY 長
    // (oldest-left .. newest-right) を保持。click は array index に写像する。
    float                                                    m_hpGraphX{0.0f};
    float                                                    m_hpGraphY{0.0f};
    float                                                    m_hpGraphW{0.0f};
    float                                                    m_hpGraphH{0.0f};
    int                                                      m_hpGraphCapacity{0};
    bool                                                     m_hpGraphValid{false};

    // ローカル scrub state — inspector が完全に所有。ゲームには一切伝えない
    // (純観察; 逆方向 channel なし、freeze なし)。
    bool                                                     m_ttScrubbing{false};  // false = live (newest)
    int                                                      m_ttOffset{0};         // 0 = newest(右), len-1 = oldest(左)

    // producer が publish する「節目」marker (offsetFromNewest は hpHistory と同一 index
    // 空間)。draw 時に parse し、click 時に nearestMarker() で最寄り節目へ snap する。
    std::vector<mitiru::observe::Marker>                     m_ttMarkers;
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
            // 特殊名 "timetravel" は自動で TimeTravel panel に振り分ける。
            // producer は inspectable 名をそう付けるだけで (DLL 側変更のみ)
            // リッチな scrub UI に opt-in できる。
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
    cfg.title           = title.c_str();  // Engine が std::string で保持; 安全
    // Sub-window default: 1080p モニタで game window の隣に圧迫せず収まる程度に
    // コンパクト。user は自由に resize 可。
    // 密な inspector データは狭い縦スクロール列の方が読みやすいので、wide+short より
    // "縦 sidebar" geometry を default にする。
    cfg.windowWidth     = 360;
    cfg.windowHeight    = 720;
    // resize 安全: これ未満には縮められない (WM_GETMINMAXINFO 強制)。文字が潰れて
    // "..." だらけになる極端な縮小を防ぐ。狭い側は scaledFont() で font も 1 段下げる。
    cfg.minWindowWidth  = 320;
    cfg.minWindowHeight = 360;
    cfg.vsync           = true;
    cfg.enableCef       = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;
    // DPI awareness — これが無いと 125%/150% scale モニタ上の 720x540 は
    // 720x540 物理 back buffer に描画され、OS が bilinear upscale 表示 = ぼやけた文字。
    // useLogicalWindowSize=true で engine が back buffer を先に物理 px へ scale し、
    // high-DPI でも文字が crisp に保たれる。
    cfg.useLogicalWindowSize = true;
    // Mitiru Saturn シルバーグレー基調 — inspector が hello_game / launcher / companion と
    // 視覚的に統一されるよう host に合わせる必須。
    // draw() 内の `screen.clear(...)` は Screen 側の clear 値を設定するだけ。
    // 実際の device ClearRenderTargetView はこの config field を使う
    // (Engine_Frame.hpp 参照)。これが無いと back buffer は device の default
    // (通常黒) になり、theme が台無しになる。
    cfg.backgroundColor = sgc::Colorf{0.784f, 0.784f, 0.784f, 1.0f};

    engine.run(game, cfg);
    return 0;
}
