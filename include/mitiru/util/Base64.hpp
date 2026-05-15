#pragma once

/// @file Base64.hpp
/// @brief Base64エンコード・デコードユーティリティ
///
/// バイナリデータとBase64文字列の相互変換を行う。
/// JSONへのバイナリデータ埋め込みなどに使用する。
///
/// @code
/// using mitiru::util::Base64;
/// std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
/// std::string encoded = Base64::encode(data.data(), data.size());
/// auto decoded = Base64::decode(encoded);
/// @endcode

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mitiru::util
{

/// @brief Base64エンコード・デコードユーティリティ
class Base64
{
public:
	/// @brief バイナリデータをBase64文字列にエンコードする
	/// @param data 入力データへのポインタ
	/// @param size 入力データのサイズ（バイト）
	/// @return Base64エンコードされた文字列
	[[nodiscard]] static std::string encode(const uint8_t* data, std::size_t size)
	{
		if (data == nullptr || size == 0)
		{
			return {};
		}

		std::string result;
		result.reserve(((size + 2) / 3) * 4);

		for (std::size_t i = 0; i < size; i += 3)
		{
			const uint32_t b0 = data[i];
			const uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0;
			const uint32_t b2 = (i + 2 < size) ? data[i + 2] : 0;
			const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

			result += kTable[(triple >> 18) & 0x3F];
			result += kTable[(triple >> 12) & 0x3F];
			result += (i + 1 < size) ? kTable[(triple >> 6) & 0x3F] : '=';
			result += (i + 2 < size) ? kTable[triple & 0x3F] : '=';
		}

		return result;
	}

	/// @brief vectorからBase64文字列にエンコードする
	/// @param data 入力データ
	/// @return Base64エンコードされた文字列
	[[nodiscard]] static std::string encode(const std::vector<uint8_t>& data)
	{
		return encode(data.data(), data.size());
	}

	/// @brief Base64文字列をバイナリデータにデコードする
	/// @param base64str Base64エンコードされた文字列
	/// @return デコードされたバイナリデータ
	/// @throws std::invalid_argument 不正なBase64文字列の場合
	[[nodiscard]] static std::vector<uint8_t> decode(const std::string& base64str)
	{
		if (base64str.empty())
		{
			return {};
		}

		/// 空白文字を除去した有効な文字列を構築
		std::string cleaned;
		cleaned.reserve(base64str.size());
		for (char c : base64str)
		{
			if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
			{
				cleaned += c;
			}
		}

		if (cleaned.size() % 4 != 0)
		{
			throw std::invalid_argument("Base64::decode: invalid input length");
		}

		auto padStart = cleaned.find('=');
		if (padStart != std::string::npos)
		{
			if (padStart < cleaned.size() - 2)
			{
				throw std::invalid_argument("Base64::decode: invalid padding position");
			}
			for (std::size_t j = padStart; j < cleaned.size(); ++j)
			{
				if (cleaned[j] != '=')
				{
					throw std::invalid_argument("Base64::decode: invalid padding");
				}
			}
		}

		std::vector<uint8_t> result;
		result.reserve((cleaned.size() / 4) * 3);

		for (std::size_t i = 0; i < cleaned.size(); i += 4)
		{
			const uint32_t a = decodeChar(cleaned[i]);
			const uint32_t b = decodeChar(cleaned[i + 1]);
			const uint32_t c = (cleaned[i + 2] != '=') ? decodeChar(cleaned[i + 2]) : 0;
			const uint32_t d = (cleaned[i + 3] != '=') ? decodeChar(cleaned[i + 3]) : 0;

			const uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;

			result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
			if (cleaned[i + 2] != '=')
			{
				result.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
			}
			if (cleaned[i + 3] != '=')
			{
				result.push_back(static_cast<uint8_t>(triple & 0xFF));
			}
		}

		return result;
	}

private:
	/// @brief Base64エンコーディングテーブル
	static constexpr char kTable[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	/// @brief Base64文字を6ビット値にデコードする
	/// @param c デコードする文字
	/// @return 6ビット値
	/// @throws std::invalid_argument 不正な文字の場合
	[[nodiscard]] static uint32_t decodeChar(char c)
	{
		if (c >= 'A' && c <= 'Z') return static_cast<uint32_t>(c - 'A');
		if (c >= 'a' && c <= 'z') return static_cast<uint32_t>(c - 'a' + 26);
		if (c >= '0' && c <= '9') return static_cast<uint32_t>(c - '0' + 52);
		if (c == '+') return 62;
		if (c == '/') return 63;
		throw std::invalid_argument("Base64::decode: invalid character");
	}
};

} // namespace mitiru::util
