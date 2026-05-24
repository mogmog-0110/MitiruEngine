#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/asset/SpriteSheet.hpp>

using namespace sgc::asset;

TEST_CASE("SpriteSheet - addFrame and frame access", "[asset]")
{
	SpriteSheet sheet;
	sheet.addFrame("idle_0", SpriteFrame{0, 0, 32, 32});
	sheet.addFrame("idle_1", SpriteFrame{32, 0, 32, 32});

	REQUIRE(sheet.frameCount() == 2);
	REQUIRE(sheet.frame(0).x == 0);
	REQUIRE(sheet.frame(1).x == 32);
}

TEST_CASE("SpriteSheet - frameByName", "[asset]")
{
	SpriteSheet sheet;
	sheet.addFrame("walk_0", SpriteFrame{0, 64, 48, 48});

	const auto frame = sheet.frameByName("walk_0");
	REQUIRE(frame.has_value());
	REQUIRE(frame->width == 48);
	REQUIRE(frame->y == 64);

	REQUIRE_FALSE(sheet.frameByName("nonexistent").has_value());
}

TEST_CASE("SpriteSheet - indexByName", "[asset]")
{
	SpriteSheet sheet;
	sheet.addFrame("a", SpriteFrame{0, 0, 16, 16});
	sheet.addFrame("b", SpriteFrame{16, 0, 16, 16});

	REQUIRE(sheet.indexByName("a").value() == 0);
	REQUIRE(sheet.indexByName("b").value() == 1);
	REQUIRE_FALSE(sheet.indexByName("c").has_value());
}

TEST_CASE("SpriteFrame - default pivot is center", "[asset]")
{
	SpriteFrame frame{0, 0, 32, 32};
	REQUIRE_THAT(frame.pivotX, Catch::Matchers::WithinAbs(0.5, 0.001));
	REQUIRE_THAT(frame.pivotY, Catch::Matchers::WithinAbs(0.5, 0.001));
}

TEST_CASE("SpriteAnimation - totalDuration", "[asset]")
{
	SpriteAnimation anim;
	anim.frameIndices = {0, 1, 2, 3};
	anim.frameDuration = 0.1f;

	REQUIRE_THAT(anim.totalDuration(), Catch::Matchers::WithinAbs(0.4, 0.001));
}

TEST_CASE("SpriteAnimator - play and update", "[asset]")
{
	SpriteSheet sheet;
	sheet.addFrame("f0", SpriteFrame{0, 0, 32, 32});
	sheet.addFrame("f1", SpriteFrame{32, 0, 32, 32});
	sheet.addFrame("f2", SpriteFrame{64, 0, 32, 32});

	SpriteAnimation anim;
	anim.name = "walk";
	anim.frameIndices = {0, 1, 2};
	anim.frameDuration = 0.1f;
	anim.loop = true;

	SpriteAnimator animator(sheet);
	animator.play(anim);

	REQUIRE(animator.currentIndex() == 0);
	REQUIRE(animator.currentFrame().x == 0);

	animator.update(0.1f);
	REQUIRE(animator.currentIndex() == 1);
	REQUIRE(animator.currentFrame().x == 32);

	animator.update(0.1f);
	REQUIRE(animator.currentIndex() == 2);
	REQUIRE(animator.currentFrame().x == 64);

	// ループ：3フレーム目で先頭に戻る
	animator.update(0.1f);
	REQUIRE(animator.currentIndex() == 0);
}

TEST_CASE("SpriteAnimator - non-loop animation finishes", "[asset]")
{
	SpriteSheet sheet;
	sheet.addFrame("f0", SpriteFrame{0, 0, 16, 16});
	sheet.addFrame("f1", SpriteFrame{16, 0, 16, 16});

	SpriteAnimation anim;
	anim.frameIndices = {0, 1};
	anim.frameDuration = 0.1f;
	anim.loop = false;

	SpriteAnimator animator(sheet);
	animator.play(anim);
	REQUIRE_FALSE(animator.isFinished());

	animator.update(0.25f); // 2.5フレーム分 → 最終フレームで停止
	REQUIRE(animator.isFinished());
	REQUIRE(animator.currentIndex() == 1);
}

TEST_CASE("SpriteAnimator - reset restores initial state", "[asset]")
{
	SpriteSheet sheet;
	sheet.addFrame("f0", SpriteFrame{0, 0, 16, 16});
	sheet.addFrame("f1", SpriteFrame{16, 0, 16, 16});

	SpriteAnimation anim;
	anim.frameIndices = {0, 1};
	anim.frameDuration = 0.1f;
	anim.loop = false;

	SpriteAnimator animator(sheet);
	animator.play(anim);
	animator.update(0.3f);
	REQUIRE(animator.isFinished());

	animator.reset();
	REQUIRE_FALSE(animator.isFinished());
	REQUIRE(animator.currentIndex() == 0);
	REQUIRE_THAT(animator.elapsed(), Catch::Matchers::WithinAbs(0.0, 0.001));
}
