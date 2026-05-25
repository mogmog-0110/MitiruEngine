#pragma once

/// @file BridgeViewPush.hpp
/// @brief key-value state と one-shot event を docs/BRIDGE_API_CONTRACT.md で
///        定義された `view.*` push channel に流す薄い adapter。
///
/// **動機。**
/// 各 bridge subsystem (HUD, dialogue, transition, …) は命名規約
/// `view.<subsystem>.<key>` を共有する。共通 helper が無いと各 bridge が
/// prefix を手で連結する必要があり、prefix の spec が変わると保守の落とし穴になる。
/// `BridgeViewPush` は prefix を instance ごとに一度だけ encode し、最小限の
/// `set` / `emit` 表面のみを提供する。
///
/// **設計。**
/// - `StateStore` はここで include しない。呼び出し元が 2 つの `std::function`
///   sink を inject することで、CEF 無しのあらゆる test / host 環境でも動作する。
/// - 完全な channel key `"view.<subsystem>.<key>"` は各 `set`/`emit` 呼び出しで
///   組み立てる。呼び出しごとの `std::string` 1 回の allocation は、hot path で
///   ない bridge traffic では許容できる。zero-alloc dispatch が必要な subsystem は
///   hot path で `StateStore` を直接使うべき。
/// - `m_keyPrefix` (`"view.<subsystem>."`) は ctor で一度だけ構築するので、
///   繰り返される prefix 部分は再計算されない。
///
/// **使用例:**
/// ```cpp
/// // 実際の StateStore への配線 (ここでは nlohmann/json 不要):
/// BridgeViewPush hud(
///     "hud",
///     [&store](std::string_view k, std::string_view v)
///         { store.set(k, store.json::parse(v)); },   // または typed helper
///     [&store](std::string_view k, std::string_view v)
///         { store.emit(k, nlohmann::json::parse(v)); }
/// );
///
/// hud.set("hp", "80");          // → store.set("view.hud.hp", …)
/// hud.emit("damage", "{\"x\":1}"); // → store.emit("view.hud.damage", …)
/// ```
///
/// 推奨される glue (事前 parse 済み JSON variant) は
/// docs/BRIDGE_API_CONTRACT.md §3 を参照。

#include <functional>
#include <string>
#include <string_view>

namespace mitiru::bridge
{

/// @brief `set`/`emit` 呼び出しを正規の `view.<sub>.<key>` channel に流す。
///
/// Thread-safety: 複数 thread から呼ぶ場合、sink 自体が thread-safe である必要が
/// ある。`BridgeViewPush` 自身は同期を一切追加しない。
class BridgeViewPush
{
public:
    /// `set(key, jsonValue)` から呼ばれる sink。
    /// @param key       完全な channel key (例: `"view.hud.hp"`)。
    /// @param jsonValue JSON-encoded された値文字列 (例: `"80"` や `"\"red\""`)。
    using SetSink  = std::function<void(std::string_view key,
                                        std::string_view jsonValue)>;

    /// `emit(key, jsonPayload)` から呼ばれる sink。
    /// @param key         完全な channel key (例: `"view.hud.damage"`)。
    /// @param jsonPayload JSON-encoded された payload 文字列 (例: `"{\"x\":1}"`)。
    using EmitSink = std::function<void(std::string_view key,
                                        std::string_view jsonPayload)>;

    /// @brief subsystem 名と 2 つの push sink で構築する。
    ///
    /// @param subsystem  短い subsystem 識別子 (例: `"hud"`, `"dialog"`,
    ///                   `"transition"`)。空文字列も受け付け、その場合
    ///                   `"view..<key>"` 形式の key になる。
    /// @param setSink    `set()` から呼ばれる。この object の lifetime の間、
    ///                   有効であり続ける必要がある。
    /// @param emitSink   `emit()` から呼ばれる。この object の lifetime の間、
    ///                   有効であり続ける必要がある。
    BridgeViewPush(std::string    subsystem,
                   SetSink        setSink,
                   EmitSink       emitSink)
        : m_subsystem(std::move(subsystem))
        , m_keyPrefix("view." + m_subsystem + ".")
        , m_setSink(std::move(setSink))
        , m_emitSink(std::move(emitSink))
    {}

    // copy 不可。sink は move には適するが、束縛した lambda を copy すると
    // captured state を黙って複製しかねない。move なら問題ない。
    BridgeViewPush(const BridgeViewPush&)            = delete;
    BridgeViewPush& operator=(const BridgeViewPush&) = delete;
    BridgeViewPush(BridgeViewPush&&)                 = default;
    BridgeViewPush& operator=(BridgeViewPush&&)      = default;

    /// @brief 保持される key-value state 更新を push する。
    ///
    /// `setSink("view.<subsystem>.<key>", jsonValue)` を呼ぶ。
    ///
    /// @param key       subsystem 内の短い key (例: `"hp"`)。
    /// @param jsonValue 値の事前 serialize 済み JSON 文字列。
    ///
    /// @note 完全な channel key を組み立てるため、呼び出しごとに `std::string`
    ///       1 回の allocation が発生する。bridge traffic では許容できるが、
    ///       tight loop では避けること。
    void set(std::string_view key, std::string_view jsonValue)
    {
        if (m_setSink)
        {
            m_setSink(buildKey(key), jsonValue);
        }
    }

    /// @brief one-shot event を発火する。
    ///
    /// `emitSink("view.<subsystem>.<key>", jsonPayload)` を呼ぶ。
    ///
    /// @param key         短い event key (例: `"damage"`)。
    /// @param jsonPayload 事前 serialize 済み JSON payload 文字列。
    void emit(std::string_view key, std::string_view jsonPayload)
    {
        if (m_emitSink)
        {
            m_emitSink(buildKey(key), jsonPayload);
        }
    }

    /// @brief 構築時に渡された subsystem 名。
    [[nodiscard]] std::string_view subsystem() const noexcept
    {
        return m_subsystem;
    }

    /// @brief 算出された key prefix (例: `"view.hud."`)。
    ///
    /// 主に test / logging 用に公開している。production code で呼び出し元が
    /// 必要とすることはないはず。
    [[nodiscard]] std::string_view keyPrefix() const noexcept
    {
        return m_keyPrefix;
    }

private:
    /// `key` の前に `m_keyPrefix` を付けて完全な channel key を返す。
    [[nodiscard]] std::string buildKey(std::string_view key) const
    {
        std::string full;
        full.reserve(m_keyPrefix.size() + key.size());
        full.append(m_keyPrefix);
        full.append(key);
        return full;
    }

    std::string m_subsystem;   ///< 例: "hud"
    std::string m_keyPrefix;   ///< 例: "view.hud." — ctor で事前計算
    SetSink     m_setSink;
    EmitSink    m_emitSink;
};

} // namespace mitiru::bridge
