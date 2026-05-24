#pragma once

/// @file ISaveFormat.hpp
/// @brief セーブフォーマット抽象化とファイルI/O実装
///
/// ISerializerの出力をファイルに保存・読み込みするためのインターフェース。
/// TextSaveFormat（JSON形式のテキスト）とBinarySaveFormat（バイナリ）を提供。
///
/// @code
/// sgc::JsonSerializer ser;
/// ser.writeString("name", "Hero");
/// ser.writeInt("level", 42);
///
/// sgc::TextSaveFormat format;
/// format.save("save.json", ser);
///
/// sgc::JsonDeserializer des("");
/// format.load("save.json", des);
/// @endcode

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "sgc/core/ISerializer.hpp"

namespace sgc
{

/// @brief セーブフォーマットの抽象インターフェース
///
/// シリアライズされたデータのファイル保存・読み込みを抽象化する。
class ISaveFormat
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ISaveFormat() = default;

	/// @brief シリアライズされたデータをファイルに保存する
	/// @param filename ファイルパス
	/// @param serializer シリアライザ（finalize()でデータを取得）
	/// @return 保存に成功すればtrue
	virtual bool save(std::string_view filename, const ISerializer& serializer) = 0;

	/// @brief ファイルからデータを読み込み、デシリアライザを構築する
	/// @param filename ファイルパス
	/// @param deserializer 読み込み先のデシリアライザ
	/// @return 読み込みに成功すればtrue
	virtual bool load(std::string_view filename, IDeserializer& deserializer) = 0;

	/// @brief フォーマット名を取得する
	/// @return フォーマット名文字列
	[[nodiscard]] virtual std::string formatName() const = 0;
};

/// @brief テキスト形式のセーブフォーマット（JSON互換）
///
/// シリアライザの出力をそのままテキストファイルとして保存する。
/// 人間が読める形式でデバッグに適している。
class TextSaveFormat final : public ISaveFormat
{
public:
	/// @brief テキスト形式でファイルに保存する
	/// @param filename ファイルパス
	/// @param serializer シリアライザ
	/// @return 保存に成功すればtrue
	bool save(std::string_view filename, const ISerializer& serializer) override
	{
		std::ofstream ofs(std::string(filename), std::ios::out | std::ios::trunc);
		if (!ofs.is_open()) return false;

		const std::string data = serializer.finalize();
		ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
		return ofs.good();
	}

	/// @brief テキストファイルからデータを読み込む
	/// @param filename ファイルパス
	/// @param deserializer 読み込み先のデシリアライザ（JsonDeserializerを想定）
	/// @return 読み込みに成功すればtrue
	///
	/// @note デシリアライザの再構築が必要なため、JsonDeserializerの参照を
	///       placement newで再構築する。
	bool load(std::string_view filename, IDeserializer& deserializer) override
	{
		std::ifstream ifs(std::string(filename), std::ios::in);
		if (!ifs.is_open()) return false;

		const std::string data(
			(std::istreambuf_iterator<char>(ifs)),
			std::istreambuf_iterator<char>());

		// JsonDeserializerを再構築
		auto* jsonDes = dynamic_cast<JsonDeserializer*>(&deserializer);
		if (jsonDes)
		{
			jsonDes->~JsonDeserializer();
			new (jsonDes) JsonDeserializer(data);
			return true;
		}
		return false;
	}

	/// @brief フォーマット名を取得する
	/// @return "Text"
	[[nodiscard]] std::string formatName() const override { return "Text"; }
};

/// @brief バイナリ形式のセーブフォーマット
///
/// データを長さプレフィックス付きバイナリとして保存する。
/// テキスト形式より省スペースだがデバッグ可読性は低い。
class BinarySaveFormat final : public ISaveFormat
{
public:
	/// @brief バイナリ形式でファイルに保存する
	/// @param filename ファイルパス
	/// @param serializer シリアライザ
	/// @return 保存に成功すればtrue
	bool save(std::string_view filename, const ISerializer& serializer) override
	{
		std::ofstream ofs(std::string(filename), std::ios::out | std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) return false;

		const std::string data = serializer.finalize();
		const uint32_t size = static_cast<uint32_t>(data.size());

		// マジックナンバー + バージョン + サイズ + データ
		const uint32_t magic = MAGIC_NUMBER;
		const uint32_t version = FORMAT_VERSION;
		ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
		ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
		ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
		ofs.write(data.data(), static_cast<std::streamsize>(size));
		return ofs.good();
	}

	/// @brief バイナリファイルからデータを読み込む
	/// @param filename ファイルパス
	/// @param deserializer 読み込み先のデシリアライザ
	/// @return 読み込みに成功すればtrue
	bool load(std::string_view filename, IDeserializer& deserializer) override
	{
		std::ifstream ifs(std::string(filename), std::ios::in | std::ios::binary);
		if (!ifs.is_open()) return false;

		uint32_t magic = 0;
		uint32_t version = 0;
		uint32_t size = 0;

		ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		if (magic != MAGIC_NUMBER) return false;

		ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
		if (version != FORMAT_VERSION) return false;

		ifs.read(reinterpret_cast<char*>(&size), sizeof(size));
		if (!ifs.good() || size > MAX_DATA_SIZE) return false;

		std::string data(size, '\0');
		ifs.read(data.data(), static_cast<std::streamsize>(size));
		if (!ifs.good()) return false;

		// JsonDeserializerを再構築
		auto* jsonDes = dynamic_cast<JsonDeserializer*>(&deserializer);
		if (jsonDes)
		{
			jsonDes->~JsonDeserializer();
			new (jsonDes) JsonDeserializer(data);
			return true;
		}
		return false;
	}

	/// @brief フォーマット名を取得する
	/// @return "Binary"
	[[nodiscard]] std::string formatName() const override { return "Binary"; }

private:
	static constexpr uint32_t MAGIC_NUMBER = 0x53475346;   ///< "SGSF" (ShiggyGameCore Save Format)
	static constexpr uint32_t FORMAT_VERSION = 1;           ///< フォーマットバージョン
	static constexpr uint32_t MAX_DATA_SIZE = 64 * 1024 * 1024;  ///< 最大データサイズ（64MB）
};

} // namespace sgc
