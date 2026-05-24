#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/scene/Transform.hpp"

using Catch::Approx;

TEST_CASE("Transform default values", "[scene][transform]")
{
	sgc::Transformf t;
	REQUIRE(t.position().x == 0.0f);
	REQUIRE(t.position().y == 0.0f);
	REQUIRE(t.position().z == 0.0f);
	REQUIRE(t.scale().x == 1.0f);
	REQUIRE(t.scale().y == 1.0f);
	REQUIRE(t.scale().z == 1.0f);
}

TEST_CASE("Transform setPosition", "[scene][transform]")
{
	sgc::Transformf t;
	t.setPosition({10.0f, 20.0f, 30.0f});
	REQUIRE(t.position().x == 10.0f);
	REQUIRE(t.position().y == 20.0f);
	REQUIRE(t.position().z == 30.0f);
}

TEST_CASE("Transform worldPosition without parent", "[scene][transform]")
{
	sgc::Transformf t;
	t.setPosition({5.0f, 3.0f, 1.0f});
	auto wp = t.worldPosition();
	REQUIRE(wp.x == Approx(5.0f));
	REQUIRE(wp.y == Approx(3.0f));
	REQUIRE(wp.z == Approx(1.0f));
}

TEST_CASE("Transform parent-child hierarchy", "[scene][transform]")
{
	sgc::Transformf parent;
	parent.setPosition({10.0f, 0.0f, 0.0f});

	sgc::Transformf child;
	child.setParent(&parent);
	child.setPosition({5.0f, 0.0f, 0.0f});

	auto wp = child.worldPosition();
	REQUIRE(wp.x == Approx(15.0f));
	REQUIRE(wp.y == Approx(0.0f));
}

TEST_CASE("Transform reparenting", "[scene][transform]")
{
	sgc::Transformf parent1;
	parent1.setPosition({10.0f, 0.0f, 0.0f});

	sgc::Transformf parent2;
	parent2.setPosition({-5.0f, 0.0f, 0.0f});

	sgc::Transformf child;
	child.setPosition({1.0f, 0.0f, 0.0f});
	child.setParent(&parent1);

	REQUIRE(child.worldPosition().x == Approx(11.0f));

	child.setParent(&parent2);
	REQUIRE(child.worldPosition().x == Approx(-4.0f));
	REQUIRE(parent1.children().empty());
	REQUIRE(parent2.children().size() == 1);
}

TEST_CASE("Transform unparent", "[scene][transform]")
{
	sgc::Transformf parent;
	parent.setPosition({10.0f, 0.0f, 0.0f});

	sgc::Transformf child;
	child.setParent(&parent);
	child.setPosition({1.0f, 0.0f, 0.0f});

	child.setParent(nullptr);
	REQUIRE(child.worldPosition().x == Approx(1.0f));
	REQUIRE(parent.children().empty());
}

TEST_CASE("Transform scale affects world matrix", "[scene][transform]")
{
	sgc::Transformf t;
	t.setPosition({0.0f, 0.0f, 0.0f});
	t.setScale({2.0f, 2.0f, 2.0f});

	auto mat = t.worldMatrix();
	REQUIRE(mat.m[0][0] == Approx(2.0f));
	REQUIRE(mat.m[1][1] == Approx(2.0f));
	REQUIRE(mat.m[2][2] == Approx(2.0f));
}

TEST_CASE("Transform dirty flag propagates to children", "[scene][transform]")
{
	sgc::Transformf parent;
	sgc::Transformf child;
	child.setParent(&parent);
	child.setPosition({1.0f, 0.0f, 0.0f});

	// Force world matrix calculation
	(void)child.worldPosition();

	// Move parent should dirty child
	parent.setPosition({5.0f, 0.0f, 0.0f});
	auto wp = child.worldPosition();
	REQUIRE(wp.x == Approx(6.0f));
}

TEST_CASE("Transform localMatrix is TRS order", "[scene][transform]")
{
	sgc::Transformf t;
	t.setPosition({1.0f, 2.0f, 3.0f});
	t.setScale({1.0f, 1.0f, 1.0f});

	auto mat = t.localMatrix();
	// 行優先: 平行移動は m[row][3] に格納
	REQUIRE(mat.m[0][3] == Approx(1.0f));
	REQUIRE(mat.m[1][3] == Approx(2.0f));
	REQUIRE(mat.m[2][3] == Approx(3.0f));
}

TEST_CASE("Transform deep hierarchy", "[scene][transform]")
{
	sgc::Transformf root;
	root.setPosition({1.0f, 0.0f, 0.0f});

	sgc::Transformf mid;
	mid.setParent(&root);
	mid.setPosition({2.0f, 0.0f, 0.0f});

	sgc::Transformf leaf;
	leaf.setParent(&mid);
	leaf.setPosition({3.0f, 0.0f, 0.0f});

	auto wp = leaf.worldPosition();
	REQUIRE(wp.x == Approx(6.0f)); // 1 + 2 + 3
}
