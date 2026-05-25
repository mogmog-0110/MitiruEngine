#pragma once

/// @file PhysicsBridge.hpp
/// @brief CEF ↔ C++ 間の Box2D v3 物理シミュレーション bridge (H-06)
///
/// **Why.** JS ゲームから物理シミュレーションを扱えるようにする。Box2D v3 の
/// ワールド/ボディは C++ 側が所有し、step も C++ で実行する。JS 側は各ボディの
/// 位置・角度を受け取って CSS transform / canvas で描画する (hybrid runtime §2 準拠)。
///
/// **登録される handler (JS → C++):**
/// - `physics.createWorld`       payload: `{"gravityX":n,"gravityY":n}`
///                               → `{"worldId":N}`
/// - `physics.destroyWorld`      payload: `{"worldId":N}`
///                               → `{}`
/// - `physics.createCircle`      payload: `{"worldId":N,"x":n,"y":n,"r":n,
///                                          "density":n,"restitution":n,"friction":n,
///                                          "userData":"str"}`
///                               → `{"bodyId":N}`
/// - `physics.createStaticEdge`  payload: `{"worldId":N,"x1":n,"y1":n,"x2":n,
///                                          "y2":n,"friction":n,"restitution":n}`
///                               → `{"bodyId":N}`
/// - `physics.destroyBody`       payload: `{"worldId":N,"bodyId":N}`
///                               → `{}`
/// - `physics.setLinearVelocity` payload: `{"worldId":N,"bodyId":N,"vx":n,"vy":n}`
///                               → `{}`
/// - `physics.poll`              payload: `{"worldId":N,"dtMs":n}`
///                               → `{"moves":[{"id":N,"x":n,"y":n,"angle":n},…],
///                                   "contacts":[{"a":N,"b":N,"aData":"str","bData":"str"},…]}`
///
/// **Step ownership。** `physics.poll` が step の駆動役 — JS の
/// `requestAnimationFrame` ループから呼ばれる。C++ は thread を所有しない。
///
/// **使い方:**
/// ```cpp
///   auto* ctx = engine.cefContext();
///   auto physState = mitiru::cef::bindPhysicsBridge(
///       [ctx](auto name, auto fn) {
///           ctx->registerHandler(std::move(name), std::move(fn));
///       });
///
///   // JS side:
///   //   window.cefQuery({ request: "physics.createWorld|{\"gravityX\":0,\"gravityY\":9.81}" });
///   //   window.cefQuery({ request: "physics.poll|{\"worldId\":1,\"dtMs\":16.67}" });
/// ```

#include <charconv>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <mitiru/physics/Box2DBridge.hpp>

namespace mitiru::cef
{

using json = ::nlohmann::json;

// ── RegisterHandlerFn (AudioBridge と同一の alias) ──────────────────────

/// @brief handler 登録関数のシグネチャ
/// @details `MitiruCefContext::registerHandler()` と互換。テスト時はモック関数を渡す。
using RegisterHandlerFn = std::function<void(
    std::string /*name*/,
    std::function<std::string(std::string_view /*payload*/)> /*fn*/)>;

// ── JSON payload helper ─────────────────────────────────────────────────────

namespace detail
{

/// @brief JSON 数値フィールドを float で取得。存在しない/型違いなら defaultVal を返す。
inline float getFloat(const json& j, const char* key, float defaultVal)
{
    if (!j.contains(key)) return defaultVal;
    const auto& v = j[key];
    if (v.is_number()) return v.get<float>();
    return defaultVal;
}

/// @brief JSON 数値フィールドを uint64 で取得。失敗時は 0 を返す。
inline uint64_t getU64(const json& j, const char* key)
{
    if (!j.contains(key)) return 0;
    const auto& v = j[key];
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer())  return static_cast<uint64_t>(v.get<int64_t>());
    return 0;
}

/// @brief JSON 文字列フィールドを返す。存在しない/型違いなら空文字列を返す。
inline std::string getString(const json& j, const char* key)
{
    if (!j.contains(key)) return {};
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    return {};
}

/// @brief payload 文字列を JSON パース。失敗したら空オブジェクトを返す。
inline json parsePayload(std::string_view payload)
{
    if (payload.empty()) return json::object();
    try { return json::parse(payload); }
    catch (...) { return json::object(); }
}

/// @brief invalid payload エラー JSON
inline std::string errInvalidPayload()
{
    return R"({"error":"invalid payload"})";
}

/// @brief unknown worldId エラー JSON
inline std::string errUnknownWorld()
{
    return R"({"error":"unknown worldId"})";
}

} // namespace detail

#ifdef MITIRU_HAS_BOX2D

// ── 実装本体 (Box2D が利用可能) ───────────────────────────────────

/// @brief ワールド内の 1 ボディのエントリ
struct BodyEntry
{
    b2BodyId    nativeId;   ///< Box2D v3 ネイティブ body id (値型, グローバル namespace)
    std::string userData;   ///< JS 側から付与された任意文字列
};

/// @brief 単一の物理ワールドとそのボディ一覧
struct WorldSlot
{
    physics::Box2DWorld                       world;
    std::unordered_map<uint64_t, BodyEntry>   bodies;   ///< bridgeBodyId → BodyEntry

    explicit WorldSlot(float gx, float gy) : world(gx, gy) {}

    // copy 不可 (Box2DWorld が copy 不可のため)
    WorldSlot(const WorldSlot&)            = delete;
    WorldSlot& operator=(const WorldSlot&) = delete;
    WorldSlot(WorldSlot&&)                 = default;
    WorldSlot& operator=(WorldSlot&&)      = default;
};

/// @brief PhysicsBridge の状態ホルダー
/// @details `bindPhysicsBridge()` が 1 つ生成し、handler closure が shared_ptr で保持する。
///          グローバル状態は持たない。
class PhysicsBridgeState
{
public:
    PhysicsBridgeState() = default;

    // copy 不可
    PhysicsBridgeState(const PhysicsBridgeState&)            = delete;
    PhysicsBridgeState& operator=(const PhysicsBridgeState&) = delete;

    // ── handler ───────────────────────────────────────────────────────────

    /// @brief physics.createWorld
    std::string handleCreateWorld(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object()) return detail::errInvalidPayload();

        const float gx = detail::getFloat(p, "gravityX", 0.0f);
        const float gy = detail::getFloat(p, "gravityY", -9.81f);

        const std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t wid = ++m_nextWorldId;
        m_worlds.emplace(wid, WorldSlot(gx, gy));
        return json{{"worldId", wid}}.dump();
    }

    /// @brief physics.destroyWorld
    std::string handleDestroyWorld(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object() || !p.contains("worldId")) return detail::errInvalidPayload();

        const uint64_t wid = detail::getU64(p, "worldId");

        const std::lock_guard<std::mutex> lock(m_mutex);
        m_worlds.erase(wid);
        return "{}";
    }

    /// @brief physics.createCircle
    std::string handleCreateCircle(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object()       ||
            !p.contains("worldId") ||
            !p.contains("x")       ||
            !p.contains("y")       ||
            !p.contains("r"))
        {
            return detail::errInvalidPayload();
        }

        const uint64_t wid        = detail::getU64  (p, "worldId");
        const float    x          = detail::getFloat(p, "x",           0.0f);
        const float    y          = detail::getFloat(p, "y",           0.0f);
        const float    r          = detail::getFloat(p, "r",           1.0f);
        const float    density    = detail::getFloat(p, "density",     1.0f);
        const float    restitution= detail::getFloat(p, "restitution", 0.0f);
        const float    friction   = detail::getFloat(p, "friction",    0.3f);
        const std::string userData= detail::getString(p, "userData");

        const std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_worlds.find(wid);
        if (it == m_worlds.end()) return detail::errUnknownWorld();

        WorldSlot& slot   = it->second;
        b2BodyId native = slot.world.createDynamicBody(x, y);
        slot.world.addCircle(native, r, density, restitution, friction);

        const uint64_t bid = ++m_nextBodyId;
        slot.bodies.emplace(bid, BodyEntry{native, userData});
        return json{{"bodyId", bid}}.dump();
    }

    /// @brief physics.createStaticEdge
    std::string handleCreateStaticEdge(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object()       ||
            !p.contains("worldId") ||
            !p.contains("x1")      ||
            !p.contains("y1")      ||
            !p.contains("x2")      ||
            !p.contains("y2"))
        {
            return detail::errInvalidPayload();
        }

        const uint64_t wid        = detail::getU64  (p, "worldId");
        const float    x1         = detail::getFloat(p, "x1",          0.0f);
        const float    y1         = detail::getFloat(p, "y1",          0.0f);
        const float    x2         = detail::getFloat(p, "x2",          0.0f);
        const float    y2         = detail::getFloat(p, "y2",          0.0f);
        const float    friction   = detail::getFloat(p, "friction",    0.3f);
        const float    restitution= detail::getFloat(p, "restitution", 0.0f);

        const std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_worlds.find(wid);
        if (it == m_worlds.end()) return detail::errUnknownWorld();

        WorldSlot& slot   = it->second;
        b2BodyId native = slot.world.createStaticEdge(
            x1, y1, x2, y2, friction, restitution);

        const uint64_t bid = ++m_nextBodyId;
        slot.bodies.emplace(bid, BodyEntry{native, {}});
        return json{{"bodyId", bid}}.dump();
    }

    /// @brief physics.destroyBody
    std::string handleDestroyBody(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object() || !p.contains("worldId") || !p.contains("bodyId"))
            return detail::errInvalidPayload();

        const uint64_t wid = detail::getU64(p, "worldId");
        const uint64_t bid = detail::getU64(p, "bodyId");

        const std::lock_guard<std::mutex> lock(m_mutex);
        auto wit = m_worlds.find(wid);
        if (wit == m_worlds.end()) return detail::errUnknownWorld();

        WorldSlot& slot  = wit->second;
        auto       bit   = slot.bodies.find(bid);
        if (bit == slot.bodies.end()) return "{}";   // 失効した id: no-op

        slot.world.destroyBody(bit->second.nativeId);
        slot.bodies.erase(bit);
        return "{}";
    }

    /// @brief physics.setLinearVelocity
    std::string handleSetLinearVelocity(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object()       ||
            !p.contains("worldId") ||
            !p.contains("bodyId")  ||
            !p.contains("vx")      ||
            !p.contains("vy"))
        {
            return detail::errInvalidPayload();
        }

        const uint64_t wid = detail::getU64  (p, "worldId");
        const uint64_t bid = detail::getU64  (p, "bodyId");
        const float    vx  = detail::getFloat(p, "vx", 0.0f);
        const float    vy  = detail::getFloat(p, "vy", 0.0f);

        const std::lock_guard<std::mutex> lock(m_mutex);
        auto wit = m_worlds.find(wid);
        if (wit == m_worlds.end()) return detail::errUnknownWorld();

        WorldSlot& slot = wit->second;
        auto       bit  = slot.bodies.find(bid);
        if (bit == slot.bodies.end()) return "{}";   // 失効した id: no-op

        slot.world.setLinearVelocity(bit->second.nativeId, vx, vy);
        return "{}";
    }

    /// @brief physics.poll — world を step し、move + contact イベントを返す
    std::string handlePoll(std::string_view payload)
    {
        const json p = detail::parsePayload(payload);
        if (!p.is_object() || !p.contains("worldId") || !p.contains("dtMs"))
            return detail::errInvalidPayload();

        const uint64_t wid  = detail::getU64  (p, "worldId");
        const float    dtMs = detail::getFloat(p, "dtMs",   16.667f);

        const std::lock_guard<std::mutex> lock(m_mutex);
        auto wit = m_worlds.find(wid);
        if (wit == m_worlds.end()) return detail::errUnknownWorld();

        WorldSlot& slot = wit->second;
        slot.world.step(dtMs / 1000.0f);

        return buildPollResponse(slot);
    }

private:
    mutable std::mutex                          m_mutex;
    std::unordered_map<uint64_t, WorldSlot>     m_worlds;
    uint64_t                                    m_nextWorldId{0};
    uint64_t                                    m_nextBodyId {0};

    // ── レスポンス builder ──────────────────────────────────────────────────

    /// @brief drain した move + contact イベントから poll JSON を構築する。
    /// @details b2BodyId → bridge bodyId + userData の逆引きを構築する。
    ///          m_mutex を保持した状態で呼ばれる。
    std::string buildPollResponse(WorldSlot& slot) const
    {
        // 逆引きを構築: native b2BodyId → (bridge bodyId, userData)。
        // b2BodyId は素の struct。linear scan を使う (body 数は高々数百 —
        // linear は cache に優しく、独自 hash も不要)。
        struct ReverseEntry { uint64_t bid; std::string_view userData; };
        std::vector<std::pair<b2BodyId, ReverseEntry>> rev;
        rev.reserve(slot.bodies.size());
        for (const auto& [bid, entry] : slot.bodies)
        {
            rev.push_back({entry.nativeId, {bid, entry.userData}});
        }

        // helper: native id → bridge id (0 = 見つからない)
        auto nativeToBid = [&](b2BodyId native) -> ReverseEntry
        {
            for (const auto& [n, e] : rev)
            {
                if (n.index1 == native.index1 &&
                    n.world0 == native.world0  &&
                    n.revision == native.revision)
                {
                    return e;
                }
            }
            return {0, {}};
        };

        json moves    = json::array();
        json contacts = json::array();

        for (const auto& mv : slot.world.drainMoveEvents())
        {
            const auto re = nativeToBid(mv.body);
            if (re.bid == 0) continue;   // body は既に破棄済み
            moves.push_back({
                {"id",    re.bid},
                {"x",     mv.x},
                {"y",     mv.y},
                {"angle", mv.angle},
            });
        }

        for (const auto& ct : slot.world.drainBeginContacts())
        {
            const auto ra = nativeToBid(ct.a);
            const auto rb = nativeToBid(ct.b);
            if (ra.bid == 0 || rb.bid == 0) continue;
            contacts.push_back({
                {"a",     ra.bid},
                {"b",     rb.bid},
                {"aData", std::string(ra.userData)},
                {"bData", std::string(rb.userData)},
            });
        }

        return json{{"moves", std::move(moves)}, {"contacts", std::move(contacts)}}.dump();
    }
};

#else  // !MITIRU_HAS_BOX2D

// ── stub (Box2D 未ビルド) ───────────────────────────────────────────────────

/// @brief PhysicsBridgeState の stub — どの handler もエラーを返す。
/// @details Box2D が無くても下流コードが compile できるようにする。
class PhysicsBridgeState
{
public:
    PhysicsBridgeState() = default;

    std::string handleCreateWorld      (std::string_view) const { return errNotBuilt(); }
    std::string handleDestroyWorld     (std::string_view) const { return errNotBuilt(); }
    std::string handleCreateCircle     (std::string_view) const { return errNotBuilt(); }
    std::string handleCreateStaticEdge (std::string_view) const { return errNotBuilt(); }
    std::string handleDestroyBody      (std::string_view) const { return errNotBuilt(); }
    std::string handleSetLinearVelocity(std::string_view) const { return errNotBuilt(); }
    std::string handlePoll             (std::string_view) const { return errNotBuilt(); }

private:
    static std::string errNotBuilt()
    {
        return R"({"error":"box2d not built"})";
    }
};

#endif // MITIRU_HAS_BOX2D

// ── bridge binding (実装本体と stub の両経路で共有) ──────────────────────

/// @brief Box2D 物理シミュレーションを CEF handler として expose する
/// @details 登録される handler 名は上部 Doxygen 参照。state は handler closure の
///          shared_ptr が保持するため、呼び出し側での lifetime 管理は不要。
///
/// **実 CEF 呼び出し例:**
/// ```cpp
///   auto physState = mitiru::cef::bindPhysicsBridge(
///       [ctx](auto name, auto fn) {
///           ctx->registerHandler(std::move(name), std::move(fn));
///       });
/// ```
inline std::shared_ptr<PhysicsBridgeState> bindPhysicsBridge(
    RegisterHandlerFn registerHandler)
{
    auto state = std::make_shared<PhysicsBridgeState>();

    registerHandler("physics.createWorld",
        [state](std::string_view payload) -> std::string {
            return state->handleCreateWorld(payload);
        });

    registerHandler("physics.destroyWorld",
        [state](std::string_view payload) -> std::string {
            return state->handleDestroyWorld(payload);
        });

    registerHandler("physics.createCircle",
        [state](std::string_view payload) -> std::string {
            return state->handleCreateCircle(payload);
        });

    registerHandler("physics.createStaticEdge",
        [state](std::string_view payload) -> std::string {
            return state->handleCreateStaticEdge(payload);
        });

    registerHandler("physics.destroyBody",
        [state](std::string_view payload) -> std::string {
            return state->handleDestroyBody(payload);
        });

    registerHandler("physics.setLinearVelocity",
        [state](std::string_view payload) -> std::string {
            return state->handleSetLinearVelocity(payload);
        });

    registerHandler("physics.poll",
        [state](std::string_view payload) -> std::string {
            return state->handlePoll(payload);
        });

    return state;
}

} // namespace mitiru::cef
