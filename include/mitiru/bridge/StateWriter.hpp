#pragma once

/// @file StateWriter.hpp
/// @brief UI 向け state push ヘルパー (signal-only bridge 規約準拠)
///
/// 値は文字列として statePush の kind=4 で送る。オブジェクトと配列は JSON にする:
///   w.set("view.points", 42);
///   w.object("view.boss").set("active", true).set("pct", 88);
///   w.array("view.hand").obj().set("name", "slime");
///
/// 受け手: UI (RmlUi) の data model。`[` か `{` で始まる文字列は JSON として読まれる (docs/UI_RMLUI.md)
/// @see mitiru/module/ModuleApi.hpp  (FrameIntents, StatePushItem)

#include <cstdio>
#include <cstring>
#include <string>
#include "mitiru/module/ModuleApi.hpp"
#include "mitiru/observe/JsonEscape.hpp"

namespace mitiru::bridge
{

// ── 内部ユーティリティ ────────────────────────────────────────────────────

namespace detail
{

/// @brief double をコンパクトな文字列に変換 (整数値は小数点なし)
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
/// @return true なら key と value が両方そのまま収まった。false は push 枠が満杯、
///         または key / value が容量を超えて切り詰められた場合。呼び出し側で
///         検知できるよう bool を返す。
inline bool pushString(mitiru::module::FrameIntents* it,
                       const char* key, const std::string& value)
{
    if (!it || it->statePushCount >= 64) return false;

    auto& s = it->statePushes[it->statePushCount++];
    // key が枠 (96B) に収まるか。収まらなければ切り詰められるので false を返す。
    const bool keyFits = std::strlen(key) < sizeof(s.key);
    std::memset(&s, 0, sizeof(s));
    std::strncpy(s.key, key, sizeof(s.key) - 1);
    s.kind = 4;
    const std::size_t cap = sizeof(s.strVal) - 1;
    std::strncpy(s.strVal, value.c_str(), cap);
    return keyFits && value.size() <= cap;
}

} // namespace detail

// ── ObjectBuilder — fluent JSON オブジェクトビルダー ────────────────────

/// @brief StateWriter::object() で得るメソッドチェーン用ビルダー。
///        スコープ終了 (デストラクタ) で自動的に push される。
///        commit() を呼ぶと即時 push し、以降の破棄を無効化する。
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
        m_body += observe::jsonEscape(v);
        m_body += '"';
        return *this;
    }

    ObjectBuilder& set(const char* field, const std::string& v)
    {
        return set(field, v.c_str());
    }

    /// @brief 即時 push し、以降の自動 push を無効にする
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

// ── ArrayBuilder — JSON オブジェクト配列ビルダー ───────────────────────────

/// @brief StateWriter::array() で得る、オブジェクトの JSON 配列ビルダー。
///        動的リスト UI (手札 / インベントリ / ショップ / 選択肢) を
///        RML の data-for へ渡す典型的な形。文字列は自動で JSON エスケープされる
///        ので、カード名や説明に " や \ が混ざっても不正な形式にならない (手書き snprintf
///        における最大の危険を構造によって防ぐ)。
///
///        strVal の容量を超える分は **不正な JSON を生成せずに** 切り詰め、有効な
///        部分配列を push したうえで overflowed() を true にする。受け手
///        (UI) が何も示さず空表示するより、呼び出し側で検知できる方が
///        安全という判断。signal-only は不変。
///
/// @example
///   auto a = w.array("view.hand");
///   for (int i = 0; i < handSize; ++i)
///       a.obj().set("i", i).set("name", cardName).set("cost", cost);
///   // a のスコープ終了時に push。a.overflowed() で切り詰めを検知できる。
class ArrayBuilder;

/// @brief ArrayBuilder::obj() が返す 1 要素のビルダー。自前のバッファに {…} を
///        組み立て、デストラクタで親の ArrayBuilder へ受け渡す。
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
        m_body += observe::jsonEscape(v); m_body += '"';
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

    /// @brief 新しい要素を開始する。返されたビルダーのスコープ終了時に配列へ確定される。
    [[nodiscard]] ArrayElement obj() { return ArrayElement(this); }

    /// @brief 即時 push して以降の自動 push を無効にする。
    void commit()
    {
        if (m_committed) return;
        m_committed = true;
        m_body += ']';
        if (!detail::pushString(m_intents, m_key.c_str(), m_body)) m_overflowed = true;
    }

    /// @brief 容量超過で要素が追加されなかった、または push が切り詰められた場合は true。
    [[nodiscard]] bool overflowed() const { return m_overflowed; }

private:
    // ArrayElement のデストラクタから呼ばれる。完成した {…} を容量チェックして
    // 追記する。超える場合は不正な JSON を生成せず、要素を追加せずに overflowed を true にする。
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

/// @brief FrameIntents へのスカラー / オブジェクト / 配列の push を提供する
///        ラッパークラス。on_update() の intents 引数を渡して構築する。
///
/// @example
///   StateWriter w(intents);
///   w.set("view.points", mem->points);
///   w.set("view.title", "クリッカー");
///   w.object("view.boss").set("active", true).set("pct", 62);
class StateWriter
{
public:
    explicit StateWriter(mitiru::module::FrameIntents* intents)
        : m_intents(intents) {}

    // コピー / ムーブは不要 (フレームローカルな用途を想定)
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

    /// @brief double 値をコンパクトな文字列に変換して push する
    void set(const char* key, double v)
    {
        detail::pushString(m_intents, key, detail::fmtDouble(v));
    }

    /// @brief 文字列をそのまま (JSON エスケープなし) push する
    ///        UI は先頭が { / [ ではない値をスカラーとして扱う
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

    /// @brief メソッドチェーン用の JSON オブジェクトビルダーを返す。
    ///        戻り値のスコープ終了時に自動で push される。
    [[nodiscard]] ObjectBuilder object(const char* key)
    {
        return ObjectBuilder(m_intents, key);
    }


    // ── オブジェクト配列 push ─────────────────────────────────────────────

    /// @brief JSON オブジェクト配列ビルダーを返す。RML の data-for 用。
    ///        文字列フィールドは自動でエスケープされ、容量超過は overflowed() で検知できる。
    [[nodiscard]] ArrayBuilder array(const char* key)
    {
        return ArrayBuilder(m_intents, key);
    }

private:
    mitiru::module::FrameIntents* m_intents;
};

} // namespace mitiru::bridge
