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

/// @brief FrameIntents に kind=4 の push を 1 件追記する。
/// @return true なら value 全体が収まった。false は push 枠満杯、または
///         value が strVal 容量を超えて truncate された (= 受け手で壊れた
///         JSON になりうる)。呼び出し側で検知できるよう bool を返す。
inline bool pushString(mitiru::module::FrameIntents* it,
                       const char* key, const std::string& value)
{
    if (!it || it->statePushCount >= 64) return false;

    auto& s = it->statePushes[it->statePushCount++];
    std::memset(&s, 0, sizeof(s));
    std::strncpy(s.key, key, sizeof(s.key) - 1);
    s.kind = 4;
    const std::size_t cap = sizeof(s.strVal) - 1;
    std::strncpy(s.strVal, value.c_str(), cap);
    return value.size() <= cap;
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

// ── ArrayBuilder — JSON オブジェクト配列ビルダー ───────────────────────────

/// @brief StateWriter::array() で得る、オブジェクトの JSON 配列ビルダー。
///        動的リスト UI (手札 / インベントリ / ショップ / 選択肢) を
///        data-m-repeat へ渡す典型形。文字列は自動で JSON エスケープされる
///        ので、カード名や説明に " や \ が混ざっても壊れない (手書き snprintf
///        の最大の footgun を構造で潰す)。
///
///        strVal 容量を超える分は **不正な JSON を吐かずに** 切り詰め、valid な
///        部分配列を push したうえで overflowed() を true にする。受け手
///        (mitiru_bind.js) が黙って空表示するより、呼び出し側で検知できる方が
///        安全という判断。signal-only (ADR 0005) は不変。
///
/// @example
///   auto a = w.array("view.hand");
///   for (int i = 0; i < handSize; ++i)
///       a.obj().set("i", i).set("name", cardName).set("cost", cost);
///   // a のスコープ終了で push。a.overflowed() で切り詰めを検知できる。
class ArrayBuilder;

/// @brief ArrayBuilder::obj() が返す 1 要素ビルダー。自前バッファに {…} を
///        組み立て、デストラクタで親 ArrayBuilder へ受け渡す。
class ArrayElement
{
public:
    explicit ArrayElement(ArrayBuilder* owner) : m_owner(owner), m_body("{") {}
    ~ArrayElement();

    ArrayElement(const ArrayElement&)            = delete;
    ArrayElement& operator=(const ArrayElement&) = delete;
    ArrayElement(ArrayElement&& o) noexcept
        : m_owner(o.m_owner), m_body(std::move(o.m_body)) { o.m_owner = nullptr; }

    ArrayElement& set(const char* field, int v)
    {
        sep(); m_body += '"'; m_body += field; m_body += "\":";
        char buf[24]; std::snprintf(buf, sizeof(buf), "%d", v); m_body += buf;
        return *this;
    }
    ArrayElement& set(const char* field, double v)
    {
        sep(); m_body += '"'; m_body += field; m_body += "\":";
        m_body += detail::fmtDouble(v);
        return *this;
    }
    ArrayElement& set(const char* field, bool v)
    {
        sep(); m_body += '"'; m_body += field; m_body += (v ? "\":true" : "\":false");
        return *this;
    }
    ArrayElement& set(const char* field, const char* v)
    {
        sep(); m_body += '"'; m_body += field; m_body += "\":\"";
        m_body += detail::jsonEscape(v); m_body += '"';
        return *this;
    }
    ArrayElement& set(const char* field, const std::string& v) { return set(field, v.c_str()); }

private:
    void sep() { if (m_body.size() > 1) m_body += ','; }

    ArrayBuilder* m_owner;
    std::string   m_body;
};

class ArrayBuilder
{
public:
    ArrayBuilder(mitiru::module::FrameIntents* intents, std::string key)
        : m_intents(intents), m_key(std::move(key)), m_body("[") {}

    ~ArrayBuilder() { commit(); }

    ArrayBuilder(const ArrayBuilder&)            = delete;
    ArrayBuilder& operator=(const ArrayBuilder&) = delete;
    ArrayBuilder(ArrayBuilder&& o) noexcept
        : m_intents(o.m_intents), m_key(std::move(o.m_key)), m_body(std::move(o.m_body)),
          m_committed(o.m_committed), m_overflowed(o.m_overflowed)
    { o.m_committed = true; }

    /// @brief 新しい要素を開始する。返ったビルダーのスコープ終了で配列へ確定。
    [[nodiscard]] ArrayElement obj() { return ArrayElement(this); }

    /// @brief 即時 push して以降の自動 push を無効にする。
    void commit()
    {
        if (m_committed) return;
        m_committed = true;
        m_body += ']';
        if (!detail::pushString(m_intents, m_key.c_str(), m_body)) m_overflowed = true;
    }

    /// @brief 容量超過で要素が落ちた or push が truncate された場合 true。
    [[nodiscard]] bool overflowed() const { return m_overflowed; }

private:
    // ArrayElement のデストラクタから呼ばれる。完成した {…} を容量チェックして
    // 追記する。超えるなら不正 JSON を出さず、要素を落として overflowed を立てる。
    void appendElement(const std::string& objBody)
    {
        if (m_committed || m_intents == nullptr) { m_overflowed = true; return; }
        const std::size_t cap  = sizeof(m_intents->statePushes[0].strVal) - 1;
        const std::size_t need = m_body.size() + (m_body.size() > 1 ? 1u : 0u)
                               + objBody.size() + 1u /* trailing ] */;
        if (need > cap) { m_overflowed = true; return; }
        if (m_body.size() > 1) m_body += ',';
        m_body += objBody;
    }

    mitiru::module::FrameIntents* m_intents;
    std::string m_key;
    std::string m_body;
    bool        m_committed  = false;
    bool        m_overflowed = false;
    friend class ArrayElement;
};

inline ArrayElement::~ArrayElement()
{
    if (m_owner == nullptr) return;  // 移動済み (moved-from)
    m_body += '}';
    m_owner->appendElement(m_body);
}

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

    // ── オブジェクト配列 push ─────────────────────────────────────────────

    /// @brief JSON オブジェクト配列ビルダーを返す。data-m-repeat 用。
    ///        文字列フィールドは自動エスケープ、容量超過は overflowed() で検知。
    [[nodiscard]] ArrayBuilder array(const char* key)
    {
        return ArrayBuilder(m_intents, key);
    }

private:
    mitiru::module::FrameIntents* m_intents;
};

} // namespace mitiru::bridge
