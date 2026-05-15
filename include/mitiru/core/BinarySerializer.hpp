#pragma once
/// @file BinarySerializer.hpp
/// @brief 高速バイナリシリアライゼーション（JSON補完用）

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru
{

/// @brief バイナリライター
class BinaryWriter
{
public:
	explicit BinaryWriter(const std::string& path)
		: m_ofs(path, std::ios::binary)
	{
		// Magic + version header
		writeRaw("MBIN", 4);
		writeU32(1); // version
	}

	[[nodiscard]] bool isOpen() const { return m_ofs.is_open() && m_ofs.good(); }

	void writeU8(uint8_t v) { m_ofs.write(reinterpret_cast<const char*>(&v), 1); }
	void writeU16(uint16_t v) { m_ofs.write(reinterpret_cast<const char*>(&v), 2); }
	void writeU32(uint32_t v) { m_ofs.write(reinterpret_cast<const char*>(&v), 4); }
	void writeI32(int32_t v) { m_ofs.write(reinterpret_cast<const char*>(&v), 4); }
	void writeF32(float v) { m_ofs.write(reinterpret_cast<const char*>(&v), 4); }
	void writeBool(bool v) { writeU8(v ? 1 : 0); }

	void writeString(const std::string& s)
	{
		writeU32(static_cast<uint32_t>(s.size()));
		if (!s.empty())
		{
			m_ofs.write(s.data(), static_cast<std::streamsize>(s.size()));
		}
	}

	void writeVec3(const float v[3])
	{
		writeF32(v[0]);
		writeF32(v[1]);
		writeF32(v[2]);
	}

	void writeRaw(const void* data, size_t size)
	{
		m_ofs.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	}

	/// @brief サイズプレフィックス付きブロック開始（後でサイズを書き戻す）
	[[nodiscard]] std::streampos beginBlock()
	{
		auto pos = m_ofs.tellp();
		writeU32(0); // placeholder
		return pos;
	}

	/// @brief ブロック終了（サイズを書き戻す）
	void endBlock(std::streampos blockStart)
	{
		auto current = m_ofs.tellp();
		uint32_t size = static_cast<uint32_t>(current - blockStart - 4);
		m_ofs.seekp(blockStart);
		writeU32(size);
		m_ofs.seekp(current);
	}

private:
	std::ofstream m_ofs;
};

/// @brief バイナリリーダー
class BinaryReader
{
public:
	explicit BinaryReader(const std::string& path)
		: m_ifs(path, std::ios::binary)
	{
		if (!m_ifs) return;
		// Verify magic
		char magic[4] = {};
		m_ifs.read(magic, 4);
		if (std::memcmp(magic, "MBIN", 4) != 0)
		{
			m_ifs.setstate(std::ios::failbit);
			return;
		}
		m_version = readU32();
	}

	[[nodiscard]] bool isOpen() const { return m_ifs.is_open() && m_ifs.good(); }
	[[nodiscard]] uint32_t version() const { return m_version; }

	[[nodiscard]] uint8_t readU8() { uint8_t v = 0; m_ifs.read(reinterpret_cast<char*>(&v), 1); return v; }
	[[nodiscard]] uint16_t readU16() { uint16_t v = 0; m_ifs.read(reinterpret_cast<char*>(&v), 2); return v; }
	[[nodiscard]] uint32_t readU32() { uint32_t v = 0; m_ifs.read(reinterpret_cast<char*>(&v), 4); return v; }
	[[nodiscard]] int32_t readI32() { int32_t v = 0; m_ifs.read(reinterpret_cast<char*>(&v), 4); return v; }
	[[nodiscard]] float readF32() { float v = 0; m_ifs.read(reinterpret_cast<char*>(&v), 4); return v; }
	[[nodiscard]] bool readBool() { return readU8() != 0; }

	[[nodiscard]] std::string readString()
	{
		uint32_t len = readU32();
		if (len == 0) return {};
		std::string s(len, '\0');
		m_ifs.read(s.data(), len);
		return s;
	}

	void readVec3(float out[3])
	{
		out[0] = readF32();
		out[1] = readF32();
		out[2] = readF32();
	}

	/// @brief ブロックサイズを読み、スキップ可能にする
	[[nodiscard]] uint32_t readBlockSize() { return readU32(); }

	/// @brief 指定バイト数をスキップする
	void skip(uint32_t bytes) { m_ifs.seekg(bytes, std::ios::cur); }

private:
	std::ifstream m_ifs;
	uint32_t m_version = 0;
};

} // namespace mitiru
