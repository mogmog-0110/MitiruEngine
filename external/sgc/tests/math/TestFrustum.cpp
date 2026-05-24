/// @file TestFrustum.cpp
/// @brief Frustum.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/Frustum.hpp"

using Catch::Approx;

// ヘルパー: 正射影のVP行列を作成
static sgc::Mat4f makeOrthoVP()
{
	// カメラは原点、-Z方向を見る
	const auto view = sgc::Mat4f::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
	const auto proj = sgc::Mat4f::orthographic(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
	return proj * view;
}

TEST_CASE("Frustum containsPoint inside ortho", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	REQUIRE(frustum.containsPoint({0, 0, -50}));
	REQUIRE(frustum.containsPoint({5, 5, -1}));
}

TEST_CASE("Frustum containsPoint outside ortho", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	REQUIRE_FALSE(frustum.containsPoint({0, 0, 1}));      // 後ろ
	REQUIRE_FALSE(frustum.containsPoint({20, 0, -50}));    // 右外
	REQUIRE_FALSE(frustum.containsPoint({0, 0, -200}));    // 遠すぎ
}

TEST_CASE("Frustum intersectsAABB inside", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	const sgc::AABB3f aabb{{-1, -1, -10}, {1, 1, -5}};
	REQUIRE(frustum.intersectsAABB(aabb));
}

TEST_CASE("Frustum intersectsAABB outside", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	const sgc::AABB3f aabb{{50, 50, 50}, {60, 60, 60}};
	REQUIRE_FALSE(frustum.intersectsAABB(aabb));
}

TEST_CASE("Frustum intersectsSphere inside", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	const sgc::Spheref sphere{{0, 0, -50}, 5.0f};
	REQUIRE(frustum.intersectsSphere(sphere));
}

TEST_CASE("Frustum intersectsSphere outside", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	const sgc::Spheref sphere{{100, 100, 100}, 1.0f};
	REQUIRE_FALSE(frustum.intersectsSphere(sphere));
}

TEST_CASE("Frustum intersectsSphere partially overlapping", "[math][frustum]")
{
	const auto frustum = sgc::Frustumf::fromViewProjection(makeOrthoVP());
	// 球の中心は外だが半径で交差
	const sgc::Spheref sphere{{12, 0, -50}, 5.0f};
	REQUIRE(frustum.intersectsSphere(sphere));
}
