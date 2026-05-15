#pragma once
/// @file Box2DBridge.hpp
/// @brief Box2D v3 物理エンジンブリッジ
/// @details Box2D v3 を MitiruEngine の物理システムに統合するためのブリッジ。
///          Box2D v3 は C API なので、C++ ラッパーを提供する。
///
/// @note Box2D v3 のビルドが必要。CMakeLists.txt で add_subdirectory(external/box2d) を追加。

#include <sgc/math/Vec2.hpp>
#include <cstdint>
#include <vector>

// Box2D v3 がビルドされている場合のみ有効
#ifdef MITIRU_HAS_BOX2D

#include <box2d/box2d.h>

namespace mitiru::physics {

/// @brief Begin-contact event between two bodies (polled from world each step).
struct Box2DContactBegin {
    b2BodyId a;
    b2BodyId b;
};

/// @brief Per-frame body transform sample (id + position + angle).
struct Box2DBodyMove {
    b2BodyId body;
    float x;
    float y;
    float angle;
};

/// @brief Box2D ワールドラッパー
class Box2DWorld {
public:
    Box2DWorld(float gravityX = 0.0f, float gravityY = -9.81f) {
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {gravityX, gravityY};
        m_world = b2CreateWorld(&worldDef);
    }

    ~Box2DWorld() {
        if (b2World_IsValid(m_world))
            b2DestroyWorld(m_world);
    }

    // Non-copyable, movable
    Box2DWorld(const Box2DWorld&) = delete;
    Box2DWorld& operator=(const Box2DWorld&) = delete;
    Box2DWorld(Box2DWorld&& other) noexcept : m_world(other.m_world) {
        other.m_world = b2_nullWorldId;
    }
    Box2DWorld& operator=(Box2DWorld&& other) noexcept {
        if (this != &other) {
            if (b2World_IsValid(m_world))
                b2DestroyWorld(m_world);
            m_world = other.m_world;
            other.m_world = b2_nullWorldId;
        }
        return *this;
    }

    void step(float dt, int subStepCount = 4) {
        b2World_Step(m_world, dt, subStepCount);
    }

    b2WorldId worldId() const noexcept { return m_world; }

    /// @brief 静的ボディを作成する
    b2BodyId createStaticBody(float x, float y) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.position = {x, y};
        bodyDef.type = b2_staticBody;
        return b2CreateBody(m_world, &bodyDef);
    }

    /// @brief 動的ボディを作成する
    b2BodyId createDynamicBody(float x, float y) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.position = {x, y};
        bodyDef.type = b2_dynamicBody;
        return b2CreateBody(m_world, &bodyDef);
    }

    /// @brief ボックスシェイプを追加する
    void addBox(b2BodyId body, float halfW, float halfH, float density = 1.0f) {
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = density;
        b2Polygon box = b2MakeBox(halfW, halfH);
        b2CreatePolygonShape(body, &shapeDef, &box);
    }

    /// @brief 円シェイプを追加する
    void addCircle(b2BodyId body, float radius, float density = 1.0f,
                   float restitution = 0.0f, float friction = 0.3f) {
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density     = density;
        shapeDef.restitution = restitution;
        shapeDef.friction    = friction;
        shapeDef.enableContactEvents  = true;
        b2Circle circle = {{0, 0}, radius};
        b2CreateCircleShape(body, &shapeDef, &circle);
    }

    /// @brief 静的 edge (線分) ボディを作成する。端点はワールド座標。
    b2BodyId createStaticEdge(float x1, float y1, float x2, float y2,
                              float friction = 0.3f, float restitution = 0.0f) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.position = {0.0f, 0.0f};
        bodyDef.type     = b2_staticBody;
        b2BodyId body = b2CreateBody(m_world, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.friction    = friction;
        shapeDef.restitution = restitution;
        shapeDef.enableContactEvents  = true;
        b2Segment seg = {{x1, y1}, {x2, y2}};
        b2CreateSegmentShape(body, &shapeDef, &seg);
        return body;
    }

    /// @brief ボディを破棄する。無効な id は no-op。
    void destroyBody(b2BodyId body) {
        if (b2Body_IsValid(body)) {
            b2DestroyBody(body);
        }
    }

    /// @brief 線形速度を設定する。
    void setLinearVelocity(b2BodyId body, float vx, float vy) {
        b2Body_SetLinearVelocity(body, {vx, vy});
    }

    /// @brief 線形速度を取得する。
    sgc::Vec2f getLinearVelocity(b2BodyId body) const {
        b2Vec2 v = b2Body_GetLinearVelocity(body);
        return {v.x, v.y};
    }

    /// @brief ボディの位置を取得する
    sgc::Vec2f getPosition(b2BodyId body) const {
        b2Vec2 pos = b2Body_GetPosition(body);
        return {pos.x, pos.y};
    }

    /// @brief ボディの回転角度を取得する
    float getAngle(b2BodyId body) const {
        b2Rot rot = b2Body_GetRotation(body);
        return b2Rot_GetAngle(rot);
    }

    /// @brief この step で移動したボディ一覧を返す (bodies:update 用)。
    std::vector<Box2DBodyMove> drainMoveEvents() const {
        std::vector<Box2DBodyMove> out;
        b2BodyEvents ev = b2World_GetBodyEvents(m_world);
        out.reserve(static_cast<size_t>(ev.moveCount));
        for (int i = 0; i < ev.moveCount; ++i) {
            const b2BodyMoveEvent& m = ev.moveEvents[i];
            b2Rot rot = m.transform.q;
            out.push_back({
                m.bodyId,
                m.transform.p.x,
                m.transform.p.y,
                b2Rot_GetAngle(rot)
            });
        }
        return out;
    }

    /// @brief この step で発生した begin-contact 一覧を返す。
    std::vector<Box2DContactBegin> drainBeginContacts() const {
        std::vector<Box2DContactBegin> out;
        b2ContactEvents ev = b2World_GetContactEvents(m_world);
        out.reserve(static_cast<size_t>(ev.beginCount));
        for (int i = 0; i < ev.beginCount; ++i) {
            const b2ContactBeginTouchEvent& be = ev.beginEvents[i];
            b2BodyId a = b2Shape_GetBody(be.shapeIdA);
            b2BodyId b = b2Shape_GetBody(be.shapeIdB);
            out.push_back({a, b});
        }
        return out;
    }

private:
    b2WorldId m_world;
};

} // namespace mitiru::physics

#else
// Box2D が利用不可の場合のスタブ
namespace mitiru::physics {
struct Box2DContactBegin {};
struct Box2DBodyMove {};
class Box2DWorld {
public:
    Box2DWorld(float = 0, float = -9.81f) {}
    void step(float, int = 4) {}
    std::vector<Box2DBodyMove>     drainMoveEvents()     const { return {}; }
    std::vector<Box2DContactBegin> drainBeginContacts()  const { return {}; }
};
} // namespace mitiru::physics
#endif
