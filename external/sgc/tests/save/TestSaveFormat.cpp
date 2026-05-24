#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdio>
#include <string>

#include "sgc/save/ISaveFormat.hpp"

using namespace sgc;
using Approx = Catch::Approx;

/// @brief テスト用の一時ファイル管理（RAIIで自動削除）
struct TempFile
{
	std::string path;
	explicit TempFile(const std::string& name) : path(name) {}
	~TempFile() { std::remove(path.c_str()); }
};

TEST_CASE("TextSaveFormat - save and load round-trip", "[save][ISaveFormat]")
{
	TempFile tmp("test_text_save.json");

	JsonSerializer ser;
	ser.writeString("name", "Hero");
	ser.writeInt("level", 42);
	ser.writeFloat("hp", 99.5f);
	ser.writeBool("alive", true);

	TextSaveFormat format;
	REQUIRE(format.save(tmp.path, ser));
	REQUIRE(format.formatName() == "Text");

	JsonDeserializer des("");
	REQUIRE(format.load(tmp.path, des));

	REQUIRE(des.readString("name") == "Hero");
	REQUIRE(des.readInt("level") == 42);
	REQUIRE(des.readFloat("hp") == Approx(99.5f).margin(0.1f));
	REQUIRE(des.readBool("alive") == true);
}

TEST_CASE("TextSaveFormat - load nonexistent file fails", "[save][ISaveFormat]")
{
	TextSaveFormat format;
	JsonDeserializer des("");
	REQUIRE_FALSE(format.load("nonexistent_file_12345.json", des));
}

TEST_CASE("BinarySaveFormat - save and load round-trip", "[save][ISaveFormat]")
{
	TempFile tmp("test_binary_save.bin");

	JsonSerializer ser;
	ser.writeString("name", "Wizard");
	ser.writeInt("mana", 200);
	ser.writeFloat("speed", 3.5f);
	ser.writeBool("flying", false);

	BinarySaveFormat format;
	REQUIRE(format.save(tmp.path, ser));
	REQUIRE(format.formatName() == "Binary");

	JsonDeserializer des("");
	REQUIRE(format.load(tmp.path, des));

	REQUIRE(des.readString("name") == "Wizard");
	REQUIRE(des.readInt("mana") == 200);
	REQUIRE(des.readFloat("speed") == Approx(3.5f).margin(0.1f));
	REQUIRE(des.readBool("flying") == false);
}

TEST_CASE("BinarySaveFormat - load nonexistent file fails", "[save][ISaveFormat]")
{
	BinarySaveFormat format;
	JsonDeserializer des("");
	REQUIRE_FALSE(format.load("nonexistent_file_12345.bin", des));
}

TEST_CASE("BinarySaveFormat - load invalid magic fails", "[save][ISaveFormat]")
{
	TempFile tmp("test_invalid_magic.bin");

	// 不正なマジックナンバーのファイルを作成
	{
		std::ofstream ofs(tmp.path, std::ios::binary);
		uint32_t badMagic = 0xDEADBEEF;
		ofs.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
	}

	BinarySaveFormat format;
	JsonDeserializer des("");
	REQUIRE_FALSE(format.load(tmp.path, des));
}

TEST_CASE("ISaveFormat - text and binary produce same data", "[save][ISaveFormat]")
{
	TempFile tmpText("test_compare_text.json");
	TempFile tmpBin("test_compare_binary.bin");

	JsonSerializer ser;
	ser.writeString("key", "value");
	ser.writeInt("num", 123);

	TextSaveFormat textFormat;
	BinarySaveFormat binFormat;

	REQUIRE(textFormat.save(tmpText.path, ser));
	REQUIRE(binFormat.save(tmpBin.path, ser));

	JsonDeserializer desText("");
	JsonDeserializer desBin("");

	REQUIRE(textFormat.load(tmpText.path, desText));
	REQUIRE(binFormat.load(tmpBin.path, desBin));

	REQUIRE(desText.readString("key") == desBin.readString("key"));
	REQUIRE(desText.readInt("num") == desBin.readInt("num"));
}

TEST_CASE("TextSaveFormat - save to invalid path fails", "[save][ISaveFormat]")
{
	JsonSerializer ser;
	ser.writeInt("x", 1);

	TextSaveFormat format;
	// ディレクトリが存在しないパスへの保存は失敗するはず
	REQUIRE_FALSE(format.save("nonexistent_dir/deep/file.json", ser));
}
