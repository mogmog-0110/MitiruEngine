#pragma once

/// @file BinarySerializer.hpp
/// @brief FlatBuffers風の軽量バイナリシリアライゼーション
///
/// 外部依存なしで型安全なバイナリ読み書きを行う。
/// 各値には1バイトの型タグが付与され、読み取り時に型チェックが行われる。
/// エンディアン: リトルエンディアン（x86/ARM ネイティブ）
///
/// @code
/// using mitiru::data::BinaryWriter;
/// using mitiru::data::BinaryReader;
///
/// BinaryWriter writer;
/// writer.writeInt(42);
/// writer.writeFloat(3.14f);
/// writer.writeString("hello");
/// writer.writeVec3(1.0f, 2.0f, 3.0f);
/// auto data = writer.finish();
///
/// BinaryReader reader(data.data(), data.size());
/// int i = reader.readInt();        // 42
/// float f = reader.readFloat();    // 3.14
/// auto s = reader.readString();    // "hello"
/// auto v = reader.readVec3();      // {1, 2, 3}
/// @endcode

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace mitiru::data
{

/// @brief バイナリフォーマットの型タグ
enum class BinaryTag : uint8_t
{
	Int32 = 0x01,
	Float32 = 0x02,
	String = 0x03,
	Bool = 0x04,
	Vec2 = 0x05,
	Vec3 = 0x06,
	Color = 0x07,
	Array = 0x08,
	Bytes = 0x09,
	Int64 = 0x0A,
	Float64 = 0x0B,
};

/// @brief Vec2構造体
struct BinVec2
{
	float x = 0.0f;
	float y = 0.0f;
};

/// @brief Vec3構造体
struct BinVec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

/// @brief Color構造体
struct BinColor
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
};

/// @brief バイナリデータライター
/// @details ヘッダ: magic("MBIN", 4バイト) + version(4バイト) + dataOffset(4バイト)
class BinaryWriter
{
public:
	/// @brief マジックバイト
	static constexpr uint32_t kMagic = 0x4E49424D; // "MBIN" リトルエンディアン

	/// @brief バージョン
	static constexpr uint32_t kVersion = 1;

	/// @brief ヘッダサイズ
	static constexpr std::size_t kHeaderSize = 12;

	/// @brief コンストラクタ
	BinaryWriter()
	{
		m_buffer.reserve(256);
		/// ヘッダ領域を予約（finishで書き込む）
		m_buffer.resize(kHeaderSize, 0);
	}

	/// @brief 32ビット整数を書き込む
	/// @param value 書き込む値
	/// @return 自身への参照
	BinaryWriter& writeInt(int32_t value)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Int32));
		writeRawLE32(static_cast<uint32_t>(value));
		return *this;
	}

	/// @brief 64ビット整数を書き込む
	/// @param value 書き込む値
	/// @return 自身への参照
	BinaryWriter& writeInt64(int64_t value)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Int64));
		writeRawLE64(static_cast<uint64_t>(value));
		return *this;
	}

	/// @brief 32ビット浮動小数点を書き込む
	/// @param value 書き込む値
	/// @return 自身への参照
	BinaryWriter& writeFloat(float value)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Float32));
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(float));
		writeRawLE32(bits);
		return *this;
	}

	/// @brief 64ビット浮動小数点を書き込む
	/// @param value 書き込む値
	/// @return 自身への参照
	BinaryWriter& writeDouble(double value)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Float64));
		uint64_t bits = 0;
		std::memcpy(&bits, &value, sizeof(double));
		writeRawLE64(bits);
		return *this;
	}

	/// @brief 文字列を書き込む
	/// @param value 書き込む文字列
	/// @return 自身への参照
	BinaryWriter& writeString(const std::string& value)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::String));
		writeRawLE32(static_cast<uint32_t>(value.size()));
		for (char c : value)
		{
			m_buffer.push_back(static_cast<uint8_t>(c));
		}
		return *this;
	}

	/// @brief 真偽値を書き込む
	/// @param value 書き込む値
	/// @return 自身への参照
	BinaryWriter& writeBool(bool value)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Bool));
		writeByte(value ? 1 : 0);
		return *this;
	}

	/// @brief Vec2を書き込む
	/// @param x X座標
	/// @param y Y座標
	/// @return 自身への参照
	BinaryWriter& writeVec2(float x, float y)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Vec2));
		writeRawFloat(x);
		writeRawFloat(y);
		return *this;
	}

	/// @brief Vec3を書き込む
	/// @param x X座標
	/// @param y Y座標
	/// @param z Z座標
	/// @return 自身への参照
	BinaryWriter& writeVec3(float x, float y, float z)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Vec3));
		writeRawFloat(x);
		writeRawFloat(y);
		writeRawFloat(z);
		return *this;
	}

	/// @brief Color(RGBA)を書き込む
	/// @param r 赤（0.0〜1.0）
	/// @param g 緑（0.0〜1.0）
	/// @param b 青（0.0〜1.0）
	/// @param a アルファ（0.0〜1.0）
	/// @return 自身への参照
	BinaryWriter& writeColor(float r, float g, float b, float a)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Color));
		writeRawFloat(r);
		writeRawFloat(g);
		writeRawFloat(b);
		writeRawFloat(a);
		return *this;
	}

	/// @brief 配列を書き込む（int32_t用）
	/// @param values 書き込む配列
	/// @return 自身への参照
	BinaryWriter& writeArray(const std::vector<int32_t>& values)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Array));
		writeByte(static_cast<uint8_t>(BinaryTag::Int32));
		writeRawLE32(static_cast<uint32_t>(values.size()));
		for (int32_t v : values)
		{
			writeRawLE32(static_cast<uint32_t>(v));
		}
		return *this;
	}

	/// @brief 配列を書き込む（float用）
	/// @param values 書き込む配列
	/// @return 自身への参照
	BinaryWriter& writeArray(const std::vector<float>& values)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Array));
		writeByte(static_cast<uint8_t>(BinaryTag::Float32));
		writeRawLE32(static_cast<uint32_t>(values.size()));
		for (float v : values)
		{
			writeRawFloat(v);
		}
		return *this;
	}

	/// @brief 配列を書き込む（string用）
	/// @param values 書き込む配列
	/// @return 自身への参照
	BinaryWriter& writeArray(const std::vector<std::string>& values)
	{
		writeByte(static_cast<uint8_t>(BinaryTag::Array));
		writeByte(static_cast<uint8_t>(BinaryTag::String));
		writeRawLE32(static_cast<uint32_t>(values.size()));
		for (const auto& s : values)
		{
			writeRawLE32(static_cast<uint32_t>(s.size()));
			for (char c : s)
			{
				m_buffer.push_back(static_cast<uint8_t>(c));
			}
		}
		return *this;
	}

	/// @brief 生バイトを書き込む
	/// @param data データへのポインタ
	/// @param size データサイズ
	/// @return 自身への参照
	BinaryWriter& writeBytes(const uint8_t* data, std::size_t size)
	{
		if (data == nullptr && size > 0)
		{
			throw std::invalid_argument("BinaryWriter::writeBytes: null data with non-zero size");
		}
		writeByte(static_cast<uint8_t>(BinaryTag::Bytes));
		writeRawLE32(static_cast<uint32_t>(size));
		m_buffer.insert(m_buffer.end(), data, data + size);
		return *this;
	}

	/// @brief シリアライズを完了し、データを返す
	/// @return シリアライズされたバイナリデータ
	[[nodiscard]] std::vector<uint8_t> finish()
	{
		/// ヘッダを書き込む
		writeHeaderLE32(0, kMagic);
		writeHeaderLE32(4, kVersion);
		writeHeaderLE32(8, static_cast<uint32_t>(kHeaderSize));

		return m_buffer;
	}

private:
	std::vector<uint8_t> m_buffer; ///< 内部バッファ

	/// @brief 1バイトを書き込む
	void writeByte(uint8_t value)
	{
		m_buffer.push_back(value);
	}

	/// @brief リトルエンディアンで32ビット値を書き込む
	void writeRawLE32(uint32_t value)
	{
		m_buffer.push_back(static_cast<uint8_t>(value & 0xFF));
		m_buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		m_buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		m_buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
	}

	/// @brief リトルエンディアンで64ビット値を書き込む
	void writeRawLE64(uint64_t value)
	{
		for (int i = 0; i < 8; ++i)
		{
			m_buffer.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
		}
	}

	/// @brief floatをリトルエンディアンで書き込む
	void writeRawFloat(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(float));
		writeRawLE32(bits);
	}

	/// @brief ヘッダ位置にリトルエンディアンで32ビット値を書き込む
	void writeHeaderLE32(std::size_t offset, uint32_t value)
	{
		m_buffer[offset] = static_cast<uint8_t>(value & 0xFF);
		m_buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
		m_buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
		m_buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
	}
};

/// @brief バイナリデータリーダー
/// @details BinaryWriterで生成されたデータを読み取る
class BinaryReader
{
public:
	/// @brief コンストラクタ
	/// @param data データへのポインタ
	/// @param size データサイズ
	BinaryReader(const uint8_t* data, std::size_t size)
		: m_data(data)
		, m_size(size)
		, m_pos(0)
	{
		if (isValid())
		{
			const uint32_t dataOffset = readRawLE32(8);
			if (dataOffset > m_size) m_pos = m_size;
			else m_pos = dataOffset;
		}
	}

	/// @brief vectorからコンストラクト
	/// @param data データ
	explicit BinaryReader(const std::vector<uint8_t>& data)
		: BinaryReader(data.data(), data.size())
	{
	}

	/// @brief マジックバイトとバージョンを検証する
	/// @return 有効なMBINデータの場合true
	[[nodiscard]] bool isValid() const noexcept
	{
		if (m_data == nullptr || m_size < BinaryWriter::kHeaderSize)
		{
			return false;
		}

		const uint32_t magic = readRawLE32(0);
		const uint32_t version = readRawLE32(4);
		return magic == BinaryWriter::kMagic && version == BinaryWriter::kVersion;
	}

	/// @brief 残りのバイト数を返す
	/// @return 未読のバイト数
	[[nodiscard]] std::size_t remaining() const noexcept
	{
		return (m_pos < m_size) ? (m_size - m_pos) : 0;
	}

	/// @brief 読み取り位置がデータ末尾に達しているかを返す
	[[nodiscard]] bool atEnd() const noexcept
	{
		return m_pos >= m_size;
	}

	/// @brief 32ビット整数を読み取る
	/// @return 読み取った値
	/// @throws std::runtime_error 型タグ不一致またはデータ不足の場合
	[[nodiscard]] int32_t readInt()
	{
		expectTag(BinaryTag::Int32);
		return static_cast<int32_t>(consumeLE32());
	}

	/// @brief 64ビット整数を読み取る
	/// @return 読み取った値
	[[nodiscard]] int64_t readInt64()
	{
		expectTag(BinaryTag::Int64);
		return static_cast<int64_t>(consumeLE64());
	}

	/// @brief 32ビット浮動小数点を読み取る
	/// @return 読み取った値
	[[nodiscard]] float readFloat()
	{
		expectTag(BinaryTag::Float32);
		const uint32_t bits = consumeLE32();
		float value = 0;
		std::memcpy(&value, &bits, sizeof(float));
		return value;
	}

	/// @brief 64ビット浮動小数点を読み取る
	/// @return 読み取った値
	[[nodiscard]] double readDouble()
	{
		expectTag(BinaryTag::Float64);
		const uint64_t bits = consumeLE64();
		double value = 0;
		std::memcpy(&value, &bits, sizeof(double));
		return value;
	}

	/// @brief 文字列を読み取る
	/// @return 読み取った文字列
	[[nodiscard]] std::string readString()
	{
		expectTag(BinaryTag::String);
		const uint32_t len = consumeLE32();
		checkAvailable(len);
		std::string result(reinterpret_cast<const char*>(m_data + m_pos), len);
		m_pos += len;
		return result;
	}

	/// @brief 真偽値を読み取る
	/// @return 読み取った値
	[[nodiscard]] bool readBool()
	{
		expectTag(BinaryTag::Bool);
		checkAvailable(1);
		const bool value = (m_data[m_pos] != 0);
		++m_pos;
		return value;
	}

	/// @brief Vec2を読み取る
	/// @return 読み取ったVec2
	[[nodiscard]] BinVec2 readVec2()
	{
		expectTag(BinaryTag::Vec2);
		BinVec2 v;
		v.x = consumeFloat();
		v.y = consumeFloat();
		return v;
	}

	/// @brief Vec3を読み取る
	/// @return 読み取ったVec3
	[[nodiscard]] BinVec3 readVec3()
	{
		expectTag(BinaryTag::Vec3);
		BinVec3 v;
		v.x = consumeFloat();
		v.y = consumeFloat();
		v.z = consumeFloat();
		return v;
	}

	/// @brief Colorを読み取る
	/// @return 読み取ったColor
	[[nodiscard]] BinColor readColor()
	{
		expectTag(BinaryTag::Color);
		BinColor c;
		c.r = consumeFloat();
		c.g = consumeFloat();
		c.b = consumeFloat();
		c.a = consumeFloat();
		return c;
	}

	/// @brief int32_t配列を読み取る
	/// @return 読み取った配列
	[[nodiscard]] std::vector<int32_t> readIntArray()
	{
		expectTag(BinaryTag::Array);
		expectTag(BinaryTag::Int32);
		const uint32_t count = consumeLE32();
		std::vector<int32_t> result;
		result.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			result.push_back(static_cast<int32_t>(consumeLE32()));
		}
		return result;
	}

	/// @brief float配列を読み取る
	/// @return 読み取った配列
	[[nodiscard]] std::vector<float> readFloatArray()
	{
		expectTag(BinaryTag::Array);
		expectTag(BinaryTag::Float32);
		const uint32_t count = consumeLE32();
		std::vector<float> result;
		result.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			result.push_back(consumeFloat());
		}
		return result;
	}

	/// @brief string配列を読み取る
	/// @return 読み取った配列
	[[nodiscard]] std::vector<std::string> readStringArray()
	{
		expectTag(BinaryTag::Array);
		expectTag(BinaryTag::String);
		const uint32_t count = consumeLE32();
		std::vector<std::string> result;
		result.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			const uint32_t len = consumeLE32();
			checkAvailable(len);
			result.emplace_back(
				reinterpret_cast<const char*>(m_data + m_pos), len);
			m_pos += len;
		}
		return result;
	}

	/// @brief 生バイトを読み取る
	/// @return 読み取ったバイトデータ
	[[nodiscard]] std::vector<uint8_t> readBytes()
	{
		expectTag(BinaryTag::Bytes);
		const uint32_t len = consumeLE32();
		checkAvailable(len);
		std::vector<uint8_t> result(m_data + m_pos, m_data + m_pos + len);
		m_pos += len;
		return result;
	}

	/// @brief 次の値の型タグを覗き見する（読み取り位置は進めない）
	/// @return 次の型タグ（データ末尾の場合nullopt相当として-1を返す）
	[[nodiscard]] BinaryTag peekTag() const
	{
		if (m_pos >= m_size)
		{
			throw std::runtime_error("BinaryReader::peekTag: no more data");
		}
		return static_cast<BinaryTag>(m_data[m_pos]);
	}

private:
	const uint8_t* m_data; ///< データポインタ
	std::size_t m_size;    ///< データサイズ
	std::size_t m_pos;     ///< 現在の読み取り位置

	/// @brief 指定位置からリトルエンディアン32ビット値を読み取る（位置を進めない）
	[[nodiscard]] uint32_t readRawLE32(std::size_t offset) const
	{
		return static_cast<uint32_t>(m_data[offset])
			| (static_cast<uint32_t>(m_data[offset + 1]) << 8)
			| (static_cast<uint32_t>(m_data[offset + 2]) << 16)
			| (static_cast<uint32_t>(m_data[offset + 3]) << 24);
	}

	/// @brief 現在位置からリトルエンディアン32ビット値を読み取り位置を進める
	[[nodiscard]] uint32_t consumeLE32()
	{
		checkAvailable(4);
		const uint32_t value = readRawLE32(m_pos);
		m_pos += 4;
		return value;
	}

	/// @brief 現在位置からリトルエンディアン64ビット値を読み取り位置を進める
	[[nodiscard]] uint64_t consumeLE64()
	{
		checkAvailable(8);
		uint64_t value = 0;
		for (int i = 0; i < 8; ++i)
		{
			value |= static_cast<uint64_t>(m_data[m_pos + i]) << (i * 8);
		}
		m_pos += 8;
		return value;
	}

	/// @brief 現在位置からfloatを読み取り位置を進める
	[[nodiscard]] float consumeFloat()
	{
		const uint32_t bits = consumeLE32();
		float value = 0;
		std::memcpy(&value, &bits, sizeof(float));
		return value;
	}

	/// @brief 型タグを検証し位置を進める
	void expectTag(BinaryTag expected)
	{
		checkAvailable(1);
		const auto actual = static_cast<BinaryTag>(m_data[m_pos]);
		if (actual != expected)
		{
			throw std::runtime_error(
				"BinaryReader: type mismatch, expected tag "
				+ std::to_string(static_cast<int>(expected))
				+ " but got " + std::to_string(static_cast<int>(actual)));
		}
		++m_pos;
	}

	/// @brief 十分なデータが残っているか検証する
	void checkAvailable(std::size_t bytes) const
	{
		if (bytes > m_size || m_pos > m_size - bytes)
		{
			throw std::runtime_error(
				"BinaryReader: unexpected end of data (need "
				+ std::to_string(bytes) + " bytes, have "
				+ std::to_string(remaining()) + ")");
		}
	}
};

} // namespace mitiru::data
