#pragma once

/// @file BridgeActionRouter.hpp
/// @brief CEF bridge signal → gameplay action ルータ
/// @details CEF 側から届く文字列 signal (例: "ui.button.fire") を、
///          事前登録した ActionHandler に dispatch する純粋な
///          ルーティングテーブル。
///
///          - CEF / StateStore 依存なし (`MITIRU_HAS_CEF` 不要)
///          - dispatch() はゼロアロケーション (transparent lookup)
///          - InputMapper との結合なし: consumer が action 名を
///            InputMapper / EventBus / 任意の handler に渡す
///
/// @code
/// mitiru::input::BridgeActionRouter router;
///
/// router.registerHandler("ui.button.fire", [](std::string_view payload) {
///     // payload は signal の付加データ (空の場合もある)
///     handleFireButton(payload);
/// });
///
/// // CEF bridge コールバック内で:
/// bool handled = router.dispatch("ui.button.fire", "slot=3");
/// @endcode

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mitiru::input {

namespace detail {
/// Transparent hasher: `is_transparent` を持つことで unordered_map が
/// `std::string` 以外 (`std::string_view` / `const char*`) からも
/// std::string を構築せずに lookup できるようになる。
struct StringTransparentHash
{
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
    {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }
    [[nodiscard]] std::size_t operator()(const char* s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }
};
} // namespace detail

/// @brief CEF bridge signal → gameplay action ルータ
/// @details signal 名 (string) を ActionHandler に紐づけ、
///          bridge から到着した signal を O(1) で dispatch する。
///
///          signal 名フォーマットは BRIDGE_API_CONTRACT.md に準拠
///          (例: "ui.button.fire", "input.pointer.drag-end") するが、
///          本クラス自体はフォーマット検証を行わない。
///          consumer が任意の signal 名キーを使用できる。
///
///          同一 signal 名への二重登録は上書き (last-write-wins)。
class BridgeActionRouter {
public:
    /// @brief signal が dispatch されたときに呼ばれるコールバック型
    /// @param payload signal に付随するデータ (空の場合もある)
    using ActionHandler = std::function<void(std::string_view payload)>;

    // ----------------------------------------------------------------
    // Registration
    // ----------------------------------------------------------------

    /// @brief signal 名と handler を紐づける
    /// @details 同一 signal 名を二度 registerHandler すると後勝ちで上書き。
    ///          signal 名は payload を含まない識別子のみ (例: "ui.button.fire")。
    /// @param signalName signal の識別子
    /// @param handler    dispatch 時に呼び出すコールバック
    void registerHandler(std::string signalName, ActionHandler handler)
    {
        m_handlers.insert_or_assign(std::move(signalName), std::move(handler));
    }

    /// @brief signal 名の登録を解除する
    /// @details 未登録の signal 名を指定しても何もしない (no-op)。
    /// @param signalName 解除する signal の識別子
    void unregisterHandler(std::string_view signalName)
    {
        const auto it = m_handlers.find(signalName);
        if (it != m_handlers.end()) {
            m_handlers.erase(it);
        }
    }

    // ----------------------------------------------------------------
    // Dispatch
    // ----------------------------------------------------------------

    /// @brief bridge から到着した signal を登録済み handler に dispatch する
    /// @details 未登録の signal 名は静かに無視する (例外なし)。
    ///          内部での std::string 構築なし — transparent lookup を使用。
    /// @param signalName dispatch する signal の識別子
    /// @param payload    signal に付随するデータ (省略可、既定は空)
    /// @return 登録済み handler を呼び出した場合 true、未登録なら false
    bool dispatch(std::string_view signalName, std::string_view payload = {})
    {
        const auto it = m_handlers.find(signalName);
        if (it == m_handlers.end()) {
            return false;
        }
        it->second(payload);
        return true;
    }

    // ----------------------------------------------------------------
    // Inspection
    // ----------------------------------------------------------------

    /// @brief 現在登録されている handler の件数を返す
    [[nodiscard]] std::size_t handlerCount() const noexcept
    {
        return m_handlers.size();
    }

    /// @brief 全登録を消去する
    void clear()
    {
        m_handlers.clear();
    }

private:
    /// @brief signal 名 → handler テーブル
    /// @details transparent hasher (`detail::StringTransparentHash` —
    ///          `is_transparent` タグ付き) + `std::equal_to<>` による
    ///          heterogeneous lookup で、dispatch() / unregisterHandler() 時に
    ///          std::string を構築せずに string_view のまま検索できる。
    std::unordered_map<
        std::string,
        ActionHandler,
        detail::StringTransparentHash,
        std::equal_to<>
    > m_handlers;
};

} // namespace mitiru::input
