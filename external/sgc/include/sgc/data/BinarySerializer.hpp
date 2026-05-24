#pragma once

/// @file BinarySerializer.hpp
/// @brief バイナリシリアライザ（書き込み）
///
/// ゲームデータをコンパクトなバイナリ形式に変換する。
/// セーブデータ、ネットワーク通信、リプレイデータ等に使用する。
///
/// @code
/// sgc::data::BinarySerializer ser;
/// ser.writeU32(42);
/// ser.writeF32(3.14f);
/// ser.writeString("hello");
/// ser.writeBool(true);
///
/// // ファイルに書き出し
/// ser.saveToFile("save.bin");
///
/// // バッファを直接取得
/// const auto& buf = ser.data();
/// @endcode

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace sgc::data
{

/// @brief バイナリデータの書き込みを行うシリアライザ
///
/// リトルエンディアンでデータを書き込む（x86/x64ネイティブ）。
/// 文字列はU32長プレフィクス + UTF-8バイト列で格納する。
class BinarySerializer
{
public:
	/// @brief 符号なし8ビット整数を書き込む
	/// @param v 値
	void writeU8(uint8_t v)
	{
		m_buffer.push_back(v);
	}

	/// @brief 符号なし16ビット整数を書き込む
	/// @param v 値
	void writeU16(uint16_t v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 符号なし32ビット整数を書き込む
	/// @param v 値
	void writeU32(uint32_t v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 符号なし64ビット整数を書き込む
	/// @param v 値
	void writeU64(uint64_t v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 符号付き32ビット整数を書き込む
	/// @param v 値
	void writeI32(int32_t v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 符号付き64ビット整数を書き込む
	/// @param v 値
	void writeI64(int64_t v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 単精度浮動小数点を書き込む
	/// @param v 値
	void writeF32(float v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 倍精度浮動小数点を書き込む
	/// @param v 値
	void writeF64(double v)
	{
		writeRaw(&v, sizeof(v));
	}

	/// @brief 真偽値を書き込む（1バイト: 0x00 or 0x01）
	/// @param v 値
	void writeBool(bool v)
	{
		writeU8(v ? 1 : 0);
	}

	/// @brief 長さプレフィクス付き文字列を書き込む
	/// @param s 文字列ビュー
	void writeString(std::string_view s)
	{
		writeU32(static_cast<uint32_t>(s.size()));
		if (!s.empty())
		{
			writeBytes(s.data(), s.size());
		}
	}

	/// @brief 生バイト列を書き込む
	/// @param data データポインタ
	/// @param size バイト数
	void writeBytes(const void* data, size_t size)
	{
		if (size == 0 || data == nullptr) return;
		const auto* bytes = static_cast<const uint8_t*>(data);
		m_buffer.insert(m_buffer.end(), bytes, bytes + size);
	}

	/// @brief 内部バッファへの参照を取得する
	/// @return バイトバッファ
	[[nodiscard]] const std::vector<uint8_t>& data() const noexcept
	{
		return m_buffer;
	}

	/// @brief 書き込み済みバイト数を取得する
	/// @return バイト数
	[[nodiscard]] size_t size() const noexcept
	{
		return m_buffer.size();
	}

	/// @brief バッファ内容をファイルに保存する
	/// @param path ファイルパス
	/// @return 成功時true
	bool saveToFile(const std::string& path) const
	{
		std::ofstream ofs(path, std::ios::binary);
		if (!ofs.is_open()) return false;
		ofs.write(reinterpret_cast<const char*>(m_buffer.data()),
		          static_cast<std::streamsize>(m_buffer.size()));
		return ofs.good();
	}

	/// @brief バッファをクリアする
	void clear()
	{
		m_buffer.clear();
	}

private:
	std::vector<uint8_t> m_buffer;

	/// @brief メモリからバイト列をバッファに追加する
	/// @param data データポインタ
	/// @param size バイト数
	void writeRaw(const void* data, size_t size)
	{
		const auto* bytes = static_cast<const uint8_t*>(data);
		m_buffer.insert(m_buffer.end(), bytes, bytes + size);
	}
};

} // namespace sgc::data
