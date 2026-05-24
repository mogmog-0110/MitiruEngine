#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "sgc/resource/IAssetLoader.hpp"

using namespace sgc;

/// @brief テスト用のモックローダー（int型アセット）
struct MockIntLoader final : public ITypedAssetLoader<int>
{
	int valueToReturn{42};
	bool shouldFail{false};
	std::string supportedExt{".dat"};

	[[nodiscard]] Result<int> load(const std::string& /*path*/) override
	{
		if (shouldFail)
		{
			return {ERROR_TAG, Error{"Mock load failed"}};
		}
		return valueToReturn;
	}

	[[nodiscard]] bool canLoad(const std::string& extension) const override
	{
		return extension == supportedExt;
	}

	[[nodiscard]] std::string loaderName() const override { return "MockIntLoader"; }
};

/// @brief テスト用の別モックローダー（int型、.bin対応）
struct MockBinLoader final : public ITypedAssetLoader<int>
{
	[[nodiscard]] Result<int> load(const std::string& /*path*/) override
	{
		return 99;
	}

	[[nodiscard]] bool canLoad(const std::string& extension) const override
	{
		return extension == ".bin";
	}

	[[nodiscard]] std::string loaderName() const override { return "MockBinLoader"; }
};

TEST_CASE("ITypedAssetLoader - mock loader success", "[resource][IAssetLoader]")
{
	MockIntLoader loader;
	auto result = loader.load("test.dat");
	REQUIRE(result.hasValue());
	REQUIRE(result.value() == 42);
}

TEST_CASE("ITypedAssetLoader - mock loader failure", "[resource][IAssetLoader]")
{
	MockIntLoader loader;
	loader.shouldFail = true;
	auto result = loader.load("test.dat");
	REQUIRE(result.hasError());
}

TEST_CASE("ITypedAssetLoader - canLoad checks extension", "[resource][IAssetLoader]")
{
	MockIntLoader loader;
	REQUIRE(loader.canLoad(".dat"));
	REQUIRE_FALSE(loader.canLoad(".png"));
	REQUIRE_FALSE(loader.canLoad(".bin"));
}

TEST_CASE("ITypedAssetLoader - loaderName returns name", "[resource][IAssetLoader]")
{
	MockIntLoader loader;
	REQUIRE(loader.loaderName() == "MockIntLoader");
}

TEST_CASE("CompositeAssetLoader - empty composite fails", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	REQUIRE(composite.loaderCount() == 0);

	auto result = composite.load("test.dat");
	REQUIRE(result.hasError());
}

TEST_CASE("CompositeAssetLoader - single loader success", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	composite.addLoader(std::make_unique<MockIntLoader>());
	REQUIRE(composite.loaderCount() == 1);

	auto result = composite.load("data.dat");
	REQUIRE(result.hasValue());
	REQUIRE(result.value() == 42);
}

TEST_CASE("CompositeAssetLoader - routes by extension", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	composite.addLoader(std::make_unique<MockIntLoader>());
	composite.addLoader(std::make_unique<MockBinLoader>());
	REQUIRE(composite.loaderCount() == 2);

	auto datResult = composite.load("file.dat");
	REQUIRE(datResult.hasValue());
	REQUIRE(datResult.value() == 42);

	auto binResult = composite.load("file.bin");
	REQUIRE(binResult.hasValue());
	REQUIRE(binResult.value() == 99);
}

TEST_CASE("CompositeAssetLoader - unknown extension fails", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	composite.addLoader(std::make_unique<MockIntLoader>());

	auto result = composite.load("file.png");
	REQUIRE(result.hasError());
}

TEST_CASE("CompositeAssetLoader - canLoad aggregates loaders", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	composite.addLoader(std::make_unique<MockIntLoader>());
	composite.addLoader(std::make_unique<MockBinLoader>());

	REQUIRE(composite.canLoad(".dat"));
	REQUIRE(composite.canLoad(".bin"));
	REQUIRE_FALSE(composite.canLoad(".png"));
}

TEST_CASE("CompositeAssetLoader - fallback on first loader failure", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;

	auto failLoader = std::make_unique<MockIntLoader>();
	failLoader->shouldFail = true;
	composite.addLoader(std::move(failLoader));

	// 同じ拡張子の別ローダーを追加
	auto successLoader = std::make_unique<MockIntLoader>();
	successLoader->valueToReturn = 100;
	composite.addLoader(std::move(successLoader));

	auto result = composite.load("test.dat");
	REQUIRE(result.hasValue());
	REQUIRE(result.value() == 100);
}

TEST_CASE("CompositeAssetLoader - null loader ignored", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	composite.addLoader(nullptr);
	REQUIRE(composite.loaderCount() == 0);
}

TEST_CASE("CompositeAssetLoader - loaderName", "[resource][IAssetLoader]")
{
	CompositeAssetLoader<int> composite;
	REQUIRE(composite.loaderName() == "CompositeAssetLoader");
}
