#pragma once

/// @file StateWriter.hpp
/// @brief CEF bridge 向け state push ヘルパー (signal-only bridge 規約準拠)
///
/// 2 つのペイロード形式を提供する:
///   1. **Scalar / Object** — JSON 文字列として statePush の kind=4 で送る
///      例: w.set("view.points", 42);
///          w.object("view.boss").set("active", true).set("pct", 88);
///
///   2. **Hot list** — セミコロン区切りのコンパクト文字列 (非 JSON)
///      列名は HTML 側 data-m-fields で宣言する。
///      例: auto L = w.list("view.scene");
///          L.item().field(2).field(80).field(120).field(1);
///          // L の破棄時に自動 push
///
/// 受け手: web/mitiru_runtime/mitiru_bind.js
/// @see mitiru/module/ModuleApi.hpp  (FrameIntents, StatePushItem)

#include <cstdio>
#include <cstring>
#include <string>
#include "mitiru/module/ModuleApi.hpp"

namespace mitiru::bridge
{

// ── 内部ユーティリティ ────────────────────────────────────────────────────

namespace detail
{

/// @brief JSON 文字列エスケープ (\\ と " と制御文字)
inline std::string jsonEscape(const char* s)
{
    std::string out;
    out.reserve(std::strlen(s) + 4);
    for (const char* p = s; *p; ++p)
    {
        unsigned char c = static_cast<unsigned char>(*p);
        if      (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c < 0x20)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else { out += static_cast<char>(c); }
    }
    return out;
}

/// @brief double を compact な文字列に変換 (整数値は小数点なし)
inline std::string fmtDouble(double v)
{
    char buf[32];
    if (v == static_cast<double>(static_cast<long long>(v)))
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else
        std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

/// @brief FrameIntents に kind=4 の push を 1 件追記する
inline void pushString(mitiru::module::FrameIntents* it,
                       const char* key, const std::string& value)
{
    if (!it || it->statePushCount >= 64) return;

    auto& s = it->statePushes[it->statePushCount++];
    std::memset(&s, 0, sizeof(s));
    std::strncpy(s.key, key, sizeof(s.key) - 1);
    s.kind = 4;
    std::strncpy(s.strVal, value.c_str(), sizeof(s.strVal) - 1);
}

} // namespace detail

// ── ObjectBuilder — fluent JSON オブジェクトビルダー ────────────────────

/// @brief StateWriter::object() で得る fluent builder。
///        スコープ終了 (デストラクタ) で自動的に push される。
///        commit() を呼ぶと即時 push して以降の破棄を無効化する。
///
/// @example
///   w.object("view.boss").set("active", true).set("pct", 88);
class ObjectBuilder
{
public:
    ObjectBuilder(mitiru::module::FrameIntents* intents, std::string key)
        : m_intents(intents), m_key(std::move(key)), m_body("{") {}

    ~ObjectBuilder() { commit(); }

    ObjectBuilder(const ObjectBuilder&)            = delete;
    ObjectBuilder& operator=(const ObjectBuilder&) = delete;
    ObjectBuilder(ObjectBuilder&& o) noexcept
        : m_intents(o.m_intents), m_key(std::move(o.m_key)),
          m_body(std::move(o.m_body)), m_committed(o.m_committed)
    { o.m_committed = true; } // 移動元はデストラクタで push しない

    ObjectBuilder& set(const char* field, bool v)
    {
        appendSep();
        m_body += '"'; m_body += field; m_body += (v ? "\":true" : "\":false");
        return *this;
    }

    ObjectBuilder& set(const char* field, int v)
    {
        appendSep();
        char buf[48];
        std::snprintf(buf, sizeof(buf), "\"%s\":%d", field, v);
        m_body += buf;
        return *this;
    }

    ObjectBuilder& set(const char* field, double v)
    {
        appendSep();
        m_body += '"'; m_body += field; m_body += "\":";
        m_body += detail::fmtDouble(v);
        return *this;
    }

    ObjectBuilder& set(const char* field, const char* v)
    {
        appendSep();
        m_body += '"'; m_body += field; m_body += "\":\"";
        m_body += detail::jsonEscape(v);
        m_body += '"';
        return *this;
    }

    ObjectBuilder& set(const char* field, const std::string& v)
    {
        return set(field, v.c_str());
    }

    /// @brief 即時 push して以降の自動 push を無効にする
    void commit()
    {
        if (m_committed) return;
        m_committed = true;
        m_body += '}';
        detail::pushString(m_intents, m_key.c_str(), m_body);
    }

private:
    void appendSep() { if (m_body.size() > 1) m_body += ','; }

    mitiru::module::FrameIntents* m_intents;
    std::string m_key;
    std::string m_body;
    bool        m_committed = false;
};

// ── RowBuilder — ListBuilder 内の 1 行ビルダー ──────────────────────────

/// @brief ListBuilder::item() で得る fluent 行ビルダー。
///        field() でカンマ区切りフィールドを積み、flush() 時に親バッファへ書き込む。
///        デストラクタで自動 flush される。
class RowBuilder
{
public:
    explicit RowBuilder(std::string& target) : m_target(target) {}

    ~RowBuilder() { flush(); }

    RowBuilder(const RowBuilder&)            = delete;
    RowBuilder& operator=(const RowBuilder&) = delete;

    RowBuilder& field(int v)
    {
        if (!m_first) m_row += ',';
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d", v);
        m_row += buf;
        m_first = false;
        return *this;
    }

    RowBuilder& field(double v)
    {
        if (!m_first) m_row += ',';
        m_row += detail::fmtDouble(v);
        m_first = false;
        return *this;
    }

    RowBuilder& field(const char* v)
    {
        if (!m_first) m_row += ',';
        m_row += v;
        m_first = false;
        return *this;
    }

    void flush()
    {
        if (m_flushed) return;
        m_flushed = true;
        m_target += m_row;
        m_target += ';';
    }

private:
    std::string& m_target;
    std::string  m_row;
    bool         m_first   = true;
    bool         m_flushed = false;
};

// ── ListBuilder — ホットリストビルダー ──────────────────────────────────

/// @brief StateWriter::list() で得るリストビルダー。
///        item() で RowBuilder を返す (デストラクタで行確定)。
///        デストラクタでリスト全体を push する。
///
/// @example
///   auto L = w.list("view.scene");
///   L.item().field(2).field(80).field(120).field(1);
///   L.item().field(3).field(200).field(100).field(2);
///   // L のスコープ終了時に push
class ListBuilder
{
public:
    ListBuilder(mitiru::module::FrameIntents* intents, std::string key)
        : m_intents(intents), m_key(std::move(key)) {}

    ~ListBuilder() { commit(); }

    ListBuilder(const ListBuilder&)            = delete;
    ListBuilder& operator=(const ListBuilder&) = delete;
    ListBuilder(ListBuilder&& o) noexcept
        : m_intents(o.m_intents), m_key(std::move(o.m_key)),
          m_body(std::move(o.m_body)), m_committed(o.m_committed)
    { o.m_committed = true; }

    /// @brief 新しい行を開始して RowBuilder を返す。
    ///        RowBuilder のスコープ終了で行がバッファに確定される。
    [[nodiscard]] RowBuilder item() { return RowBuilder(m_body); }

    /// @brief 整数フィールドのみの行を一括追加する便利オーバーロード
    void row(int a, int b, int c, int d)
    {
        auto r = item();
        r.field(a).field(b).field(c).field(d);
    }

    void row(int a, int b, int c)
    {
        auto r = item();
        r.field(a).field(b).field(c);
    }

    void row(int a, int b)
    {
        auto r = item();
        r.field(a).field(b);
    }

    /// @brief 即時 push して以降の自動 push を無効にする
    void commit()
    {
        if (m_committed) return;
        m_committed = true;
        detail::pushString(m_intents, m_key.c_str(), m_body);
    }

private:
    mitiru::module::FrameIntents* m_intents;
    std::string m_key;
    std::string m_body;
    bool        m_committed = false;
};

// ── StateWriter — メインエントリポイント ─────────────────────────────────

/// @brief FrameIntents へのスカラー / オブジェクト / リスト push を提供する
///        ラッパークラス。on_update() の intents 引数を渡して構築する。
///
/// @example
///   StateWriter w(intents);
///   w.set("view.points", mem->points);
///   w.set("view.title", "クリッカー");
///   w.object("view.boss").set("active", true).set("pct", 62);
///   {
///       auto L = w.list("view.scene");
///       L.item().field(2).field(80).field(120).field(1);
///   }
class StateWriter
{
public:
    explicit StateWriter(mitiru::module::FrameIntents* intents)
        : m_intents(intents) {}

    // コピー / ムーブ不要 (フレームローカルな用途を想定)
    StateWriter(const StateWriter&)            = delete;
    StateWriter& operator=(const StateWriter&) = delete;

    // ── スカラー push ────────────────────────────────────────────────────

    /// @brief 整数値を文字列化して push する
    void set(const char* key, int v)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d", v);
        detail::pushString(m_intents, key, buf);
    }

    /// @brief double 値を compact 文字列化して push する
    void set(const char* key, double v)
    {
        detail::pushString(m_intents, key, detail::fmtDouble(v));
    }

    /// @brief 文字列をそのまま (JSON エスケープなし) push する
    ///        mitiru_bind.js は非 { / [ 先頭をスカラーとして扱う
    void set(const char* key, const char* v)
    {
        detail::pushString(m_intents, key, v);
    }

    void set(const char* key, const std::string& v)
    {
        detail::pushString(m_intents, key, v);
    }

    /// @brief bool を "true" / "false" 文字列として push する
    void set(const char* key, bool v)
    {
        detail::pushString(m_intents, key, v ? "true" : "false");
    }

    // ── オブジェクト push ─────────────────────────────────────────────────

    /// @brief fluent JSON オブジェクトビルダーを返す。
    ///        戻り値のスコープ終了時に自動 push される。
    [[nodiscard]] ObjectBuilder object(const char* key)
    {
        return ObjectBuilder(m_intents, key);
    }

    // ── ホットリスト push ─────────────────────────────────────────────────

    /// @brief セミコロン区切りリストビルダーを返す。
    ///        戻り値のスコープ終了時に自動 push される。
    [[nodiscard]] ListBuilder list(const char* key)
    {
        return ListBuilder(m_intents, key);
    }

private:
    mitiru::module::FrameIntents* m_intents;
};

} // namespace mitiru::bridge
