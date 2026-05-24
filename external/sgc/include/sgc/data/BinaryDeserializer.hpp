#pragma once

/// @file BinaryDeserializer.hpp
/// @brief バイナリデシリアライザ（読み込み）
///
/// BinarySerializerで書き込んだバイナリデータを読み戻す。
/// セーブデータのロード、ネットワークパケットの解析等に使用する。
///
/// @code
/// // シリアライズされたデータから読み込み
/// sgc::data::BinaryDeserializer des(buffer);
/// auto id = des.readU32();
/// auto score = des.readF32();
/// auto name = des.readString();
///
/// // ファイルから読み込み
/// auto des2 = sgc::data::BinaryDeserializer::fromFile("save.bin");
/// @endcode

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sgc::data
{

/// @brief バイナリデータの読み込みを行うデシリアライザ
///
/// BinarySerializerと対になる読み込みクラス。
/// データが不足している場合はstd::runtime_errorをスローする。
class BinaryDeserializer
{
public:
	/// @brief バイトスパンからデシリアライザを構築する
	/// @param data 読み込み元のバイトスパン
	explicit BinaryDeserializer(std::span<const uint8_t> data)
		: m_data(data)
	{
	}

	/// @brief 符号なし8ビット整数を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] uint8_t readU8()
	{
		ensureAvailable(1);
		return m_data[m_pos++];
	}

	/// @brief 符号なし16ビット整数を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] uint16_t readU16()
	{
		return readRaw<uint16_t>();
	}

	/// @brief 符号なし32ビット整数を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] uint32_t readU32()
	{
		return readRaw<uint32_t>();
	}

	/// @brief 符号なし64ビット整数を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] uint64_t readU64()
	{
		return readRaw<uint64_t>();
	}

	/// @brief 符号付き32ビット整数を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] int32_t readI32()
	{
		return readRaw<int32_t>();
	}

	/// @brief 符号付き64ビット整数を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] int64_t readI64()
	{
		return readRaw<int64_t>();
	}

	/// @brief 単精度浮動小数点を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] float readF32()
	{
		return readRaw<float>();
	}

	/// @brief 倍精度浮動小数点を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] double readF64()
	{
		return readRaw<double>();
	}

	/// @brief 真偽値を読み込む
	/// @return 読み込んだ値
	[[nodiscard]] bool readBool()
	{
		return readU8() != 0;
	}

	/// @brief 長さプレフィクス付き文字列を読み込む
	/// @return 読み込んだ文字列
	[[nodiscard]] std::string readString()
	{
		const uint32_t len = readU32();
		if (len == 0) return {};
		ensureAvailable(len);
		std::string result(reinterpret_cast<const char*>(m_data.data() + m_pos), len);
		m_pos += len;
		return result;
	}

	/// @brief 生バイト列を読み込む
	/// @param dest 書き込み先ポインタ
	/// @param size 読み込みバイト数
	void readBytes(void* dest, size_t size)
	{
		if (size == 0) return;
		ensureAvailable(size);
		std::memcpy(dest, m_data.data() + m_pos, size);
		m_pos += size;
	}

	/// @brief 未読データが残っているか
	/// @return 残りがあればtrue
	[[nodiscard]] bool hasRemaining() const noexcept
	{
		return m_pos < m_data.size();
	}

	/// @brief 未読バイト数を取得する
	/// @return 残りバイト数
	[[nodiscard]] size_t remaining() const noexcept
	{
		return m_data.size() - m_pos;
	}

	/// @brief 現在の読み込み位置を取得する
	/// @return バイトオフセット
	[[nodiscard]] size_t position() const noexcept
	{
		return m_pos;
	}

	/// @brief ファイルからデシリアライザを構築する
	/// @param path ファイルパス
	/// @return 構築されたデシリアライザ（内部にバッファを保持）
	/// @throw std::runtime_error ファイルを開けない場合
	[[nodiscard]] static BinaryDeserializer fromFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs.is_open())
		{
			throw std::runtime_error("BinaryDeserializer: cannot open file: " + path);
		}
		std::vector<uint8_t> buf((std::istreambuf_iterator<char>(ifs)),
		                          std::istreambuf_iterator<char>());
		return BinaryDeserializer(std::move(buf));
	}

private:
	/// @brief ファイル読み込み用の内部バッファ（fromFileで使用）
	/// @note m_dataより前に宣言し、初期化順序を保証する
	std::vector<uint8_t> m_ownedBuffer;

	std::span<const uint8_t> m_data;
	size_t m_pos = 0;

	/// @brief 内部バッファからデシリアライザを構築する（fromFile用）
	explicit BinaryDeserializer(std::vector<uint8_t>&& buf)
		: m_ownedBuffer(std::move(buf))
		, m_data(m_ownedBuffer)
	{
	}

	/// @brief 指定バイト数が読み込み可能か確認する
	/// @param bytes 必要なバイト数
	/// @throw std::runtime_error データ不足の場合
	void ensureAvailable(size_t bytes) const
	{
		if (m_pos + bytes > m_data.size())
		{
			throw std::runtime_error(
				"BinaryDeserializer: read overflow (need " +
				std::to_string(bytes) + " bytes, " +
				std::to_string(remaining()) + " remaining)");
		}
	}

	/// @brief 型Tのデータを読み込む
	/// @tparam T 読み込む型
	/// @return 読み込んだ値
	template <typename T>
	[[nodiscard]] T readRaw()
	{
		ensureAvailable(sizeof(T));
		T value;
		std::memcpy(&value, m_data.data() + m_pos, sizeof(T));
		m_pos += sizeof(T);
		return value;
	}
};

} // namespace sgc::data
